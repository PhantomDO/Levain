#include "levain/app/app.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <format>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

#include "shader_reload.hpp"

#include "levain/app/texture_reload.hpp"
#include "levain/assets/image.hpp"
#include "levain/core/assert.hpp"
#include "levain/core/frame_time.hpp"
#include "levain/core/log.hpp"
#include "levain/core/profile.hpp"
#include "levain/platform/input.hpp"
#include "levain/render/readback.hpp"
#include "levain/render/sky.hpp"
#include "levain/scene/components.hpp"
#include "levain/scene/scene.hpp"

namespace levain::app
{

namespace
{

using Clock = std::chrono::steady_clock;

/// Durée sur laquelle le frame time du titre est résumé.
constexpr double FrameTimePeriodSeconds = 1.0;

/// Validation en Debug seulement (règle n°4) : elle coûte cher, et c'est là qu'on développe.
constexpr bool EnableValidation = LEVAIN_ASSERTIONS_ENABLED != 0;

/// Le soleil sans HDRI : haut, de biais, légèrement chaud.
constexpr render::Sun DefaultSun{
    .direction = {-0.7f, 0.45f, 0.5f}, .color = {1.0f, 0.95f, 0.85f}, .intensity = 3.0f};

/// `--sky none` : ni HDRI, ni ambiance.
constexpr std::string_view NoSky = "none";

double secondsBetween(Clock::time_point start, Clock::time_point end)
{
    return std::chrono::duration<double>(end - start).count();
}

struct LoopState
{
    bool isRunning = true;
    bool isVisible = true;
};

/// Le piège du temps masqué : sous le web, le navigateur cesse d'appeler la boucle d'un onglet
/// caché, et l'horloge des images n'est pas remise à l'heure. Au retour (`Shown`), l'intervalle
/// repart de cette image ; sinon la première durerait toute l'absence, et le calque de la page
/// (#294) afficherait 0 image/s pendant une seconde.
void forgetHiddenTime(Clock::time_point& previousFrameEnd, const platform::WindowEvent& event,
                      Clock::time_point frameStart)
{
    if (event.type == platform::WindowEventType::Shown)
    {
        previousFrameEnd = frameStart;
    }
}

void applyWindowEvent(LoopState& state, const platform::WindowEvent& event)
{
    using platform::WindowEventType;

    switch (event.type)
    {
    case WindowEventType::CloseRequested:
        state.isRunning = false;
        break;
    // Les deux arrivent en double : sous Wayland, SDL renvoie EXPOSED après chaque
    // redimensionnement. On ne journalise donc que les changements d'état.
    case WindowEventType::Hidden:
        if (state.isVisible)
        {
            core::log("app", core::LogLevel::Info, "masquée : boucle en pause");
        }
        state.isVisible = false;
        break;
    case WindowEventType::Shown:
        if (!state.isVisible)
        {
            core::log("app", core::LogLevel::Info, "visible : boucle relancée");
        }
        state.isVisible = true;
        break;
    case WindowEventType::Resized:
        core::log("app", core::LogLevel::Info, "redimensionnée : {} × {} px", event.pixelSize.width,
                  event.pixelSize.height);
        break;
    }
}

/// Ce que la boucle mesure d'une période d'images (#294) : le titre de la fenêtre et le calque de
/// la page web l'affichent.
struct FrameReport
{
    core::FrameTimeSummary times;
    double imagesPerSecond = 0.0;
    /// Le temps d'une image passé dans le moteur, hors attente de l'écran, en moyenne.
    double engineMs = 0.0;
    /// Sa part de l'intervalle entre deux images, en %.
    double enginePercent = 0.0;
    platform::PixelSize pixels;
};

/// Le résumé d'une période, et la part du moteur : ce qui s'approche le plus d'une occupation du
/// CPU, que le navigateur ne donne pas (docs/QA.md, 06/10/2026). Le reste de l'intervalle, c'est
/// l'attente de l'écran, ou ailleurs le navigateur et les autres programmes.
FrameReport frameReportOf(const core::FrameTimeSummary& summary, double engineSeconds,
                          platform::PixelSize pixels)
{
    // averageMs n'est jamais nul : un résumé couvre au moins FrameTimePeriodSeconds.
    const double engineMs = engineSeconds * 1000.0 / summary.frameCount;
    return {.times = summary,
            .imagesPerSecond = 1000.0 / summary.averageMs,
            .engineMs = engineMs,
            .enginePercent = 100.0 * engineMs / summary.averageMs,
            .pixels = pixels};
}

std::string describeFrameTimes(std::string_view title, const FrameReport& report, double gpuMs)
{
    // Tirets ASCII : setWindowTitle refuse le reste (voir window.hpp).
    return std::format("{} - {:.3f} ms (min {:.3f}, max {:.3f}) - {:.0f} images/s - moteur "
                       "{:.3f} ms ({:.0f} %) - GPU {:.3f} ms",
                       title, report.times.averageMs, report.times.minMs, report.times.maxMs,
                       report.imagesPerSecond, report.engineMs, report.enginePercent, gpuMs);
}

#ifdef __EMSCRIPTEN__
// Le calque de la page web (#294) : le moteur appelle `Module.onFrameReport` s'il existe, une fois
// par période. EM_JS écrit une fonction JavaScript appelable depuis le C++ (documentation
// d'Emscripten, « Interacting with code », section « Calling JavaScript from C/C++ »).
// clang-format off
EM_JS(void, reportFrameToPage, (double imagesPerSecond, double averageMs, double maxMs,
                                double engineMs, double enginePercent, int width, int height), {
    if (Module.onFrameReport) {
        Module.onFrameReport({imagesPerSecond, averageMs, maxMs, engineMs, enginePercent, width,
                              height});
    }
});
// clang-format on

void reportFrame(const FrameReport& report)
{
    reportFrameToPage(report.imagesPerSecond, report.times.averageMs, report.times.maxMs,
                      report.engineMs, report.enginePercent, report.pixels.width,
                      report.pixels.height);
}
#endif

#ifdef LEVAIN_ENABLE_EXPLORER
/// L'explorer web de flecs (https://www.flecs.dev/explorer) lit et modifie le monde par l'addon
/// REST, sur le port 27750. Debug seulement, et **sur la boucle locale** : par défaut, flecs écoute
/// sur toutes les interfaces, et son API distante sait aussi supprimer des entités et exécuter des
/// scripts (https://www.flecs.dev/flecs/FlecsRemoteApi.html).
void enableExplorerOnLoopback(flecs::world& world)
{
    world.import<flecs::stats>(); // les statistiques de l'onglet « Stats » de l'explorer
    // ipaddr doit venir de l'allocateur de flecs : EcsRest en prend la propriété et le libère à la
    // destruction du monde (src/addons/rest.c, ECS_DTOR(EcsRest)). Une chaîne statique finissait en
    // « double free » à la sortie du programme.
    world.set<flecs::Rest>(
        {.port = ECS_REST_DEFAULT_PORT, .ipaddr = ecs_os_strdup("127.0.0.1"), .impl = nullptr});
    core::log("app", core::LogLevel::Info,
              "explorer : https://www.flecs.dev/explorer (REST sur 127.0.0.1:{})",
              ECS_REST_DEFAULT_PORT);
}
#endif

/// Ce que le scan des assets a changé sur le disque (ADR-0019) : les .meta créés et rattachés sont
/// à versionner, les orphelins à regarder.
void logScanReport(const assets::ScanReport& report)
{
    for (const auto& created : report.created)
    {
        core::log("assets", core::LogLevel::Info, "nouveau .meta : {}", created.string());
    }
    for (const auto& reattached : report.reattached)
    {
        core::log("assets", core::LogLevel::Info, "renommé hors du moteur, GUID conservé : {}",
                  reattached.string());
    }
    for (const auto& orphan : report.orphans)
    {
        core::log("assets", core::LogLevel::Warning, ".meta orphelin, asset disparu : {}",
                  orphan.string());
    }
}

/// Les racines d'assets des réglages (ADR-0019) : la première doit exister, ses .meta se
/// commitent avec les fichiers ; les suivantes, des assets téléchargés, sont lues si elles sont là.
/// Les fichiers cuits de chaque racine sont dans son `.cooked/` (ADR-0020).
core::Result<void> scanAssetRoots(const AppSettings& settings, assets::AssetRegistry& registry)
{
    // Le premier temps du chargement (le critère de M4.3) : le scan hache tous les assets.
    const Clock::time_point start = Clock::now();
    for (std::size_t root = 0; root < settings.assetRoots.size(); ++root)
    {
        const std::filesystem::path& directory = settings.assetRoots[root];
        std::error_code error;
        if (root > 0 && !std::filesystem::is_directory(directory, error))
        {
            continue;
        }
        auto report = assets::scanAssets(directory, registry);
        if (!report)
        {
            return std::unexpected(report.error());
        }
        logScanReport(*report);
    }
    core::log("assets", core::LogLevel::Info, "scan des racines d'assets : {:.0f} ms",
              secondsBetween(start, Clock::now()) * 1000.0);
    return {};
}

/// Le ciel qui éclaire la scène, et son soleil.
struct Sky
{
    render::Environment environment;
    render::Sun sun;
};

/// Tourne le ciel de `degrees` autour de la verticale : chaque ligne de l'image équirectangulaire
/// glisse d'autant de colonnes, la longitude faisant le tour de l'image.
void turnSkyAroundUp(assets::HdrImage& image, float degrees)
{
    const auto columns = static_cast<std::ptrdiff_t>(
        std::lround(static_cast<double>(image.width) * degrees / 360.0));
    const auto rowLength = static_cast<std::ptrdiff_t>(image.width) * 4;
    for (auto row = image.rgba.begin(); row != image.rgba.end(); row += rowLength)
    {
        std::rotate(row, row + rowLength - (columns * 4), row + rowLength);
    }
}

/// Le ciel des réglages, ou sans HDRI un ciel uniforme et sombre, l'ambiance d'avant l'IBL, sous le
/// soleil par défaut. Le soleil de l'HDRI en est retiré, pour devenir celui de la scène, qui jette
/// les ombres, sauf si les réglages le laissent dans l'éclairage ambiant (`skySunCastsShadows`).
/// Le temps de calcul est donné : il se paie à chaque chargement.
core::Result<Sky> loadSky(nvrhi::IDevice& device, const std::optional<std::filesystem::path>& path,
                          const AppSettings& settings)
{
    if (!path || path->native() == NoSky)
    {
        // `--sky none` : aucune lumière du ciel, pas même l'ambiance (#125, le viewer sans IBL).
        auto uniform = render::createUniformEnvironment(device, glm::vec3{path ? 0.0f : 0.1f});
        if (!uniform)
        {
            return std::unexpected(uniform.error());
        }
        return Sky{.environment = std::move(*uniform), .sun = DefaultSun};
    }
    auto image = assets::loadHdrImage(*path);
    if (!image)
    {
        return std::unexpected(image.error());
    }
    if (settings.skyTurnDegrees != 0.0f)
    {
        turnSkyAroundUp(*image, settings.skyTurnDegrees);
    }
    std::optional<render::Sun> sun;
    if (!settings.extractSkySun)
    {
        sun = render::Sun{.direction = {0.0f, 1.0f, 0.0f}, .color{1.0f}, .intensity = 0.0f};
        core::log("app", core::LogLevel::Info, "le ciel seul éclaire, tourné de {:.0f}°",
                  settings.skyTurnDegrees);
    }
    else
    {
        sun = render::extractSun(image->width, image->height, image->rgba);
        if (sun)
        {
            core::log("app", core::LogLevel::Info,
                      "soleil de l'HDRI : direction ({:.2f}, {:.2f}, {:.2f}), intensité {:.2f}",
                      sun->direction.x, sun->direction.y, sun->direction.z, sun->intensity);
        }
        else
        {
            core::log("app", core::LogLevel::Warning,
                      "pas de soleil dans l'HDRI : le soleil par défaut éclaire la scène");
        }
    }
    const auto start = Clock::now();
    auto environment = render::createEnvironment(
        device, {.width = image->width, .height = image->height, .rgba = image->rgba});
    device.waitForIdle();
#ifdef __EMSCRIPTEN__
    // Le navigateur ne laisse pas attendre le GPU (waitForIdle n'y fait rien) : le temps ne compte
    // que l'enregistrement des passes, pas leur calcul.
    constexpr std::string_view Measured = "enregistré";
#else
    constexpr std::string_view Measured = "calculé";
#endif
    core::log("app", core::LogLevel::Info, "ciel : {} ({} × {}), environnement {} en {:.1f} ms",
              path->filename().string(), image->width, image->height, Measured,
              secondsBetween(start, Clock::now()) * 1000.0);
    if (!environment)
    {
        return std::unexpected(environment.error());
    }
    return Sky{.environment = std::move(*environment), .sun = sun.value_or(DefaultSun)};
}

/// Le ciel à charger : celui des réglages, ou le ciel par défaut s'il a été téléchargé. Sans lui,
/// un ciel uniforme, et la ligne du journal dit pourquoi.
std::optional<std::filesystem::path> skyPathOf(const AppSettings& settings)
{
    if (settings.sky || !settings.defaultSky)
    {
        return settings.sky;
    }
    std::error_code error;
    if (std::filesystem::exists(*settings.defaultSky, error))
    {
        return settings.defaultSky;
    }
    core::log("app", core::LogLevel::Info,
              "pas de ciel : {} absent (tools/fetch-assets.sh), ambiance uniforme",
              settings.defaultSky->string());
    return std::nullopt;
}

/// Tout ce qui précède la fonction de démarrage : les assets, le ciel, le renderer, le monde et
/// l'input. Sur le tas : les fonctions d'étape et les systèmes du programme gardent `App` par
/// référence.
core::Result<std::unique_ptr<App>> createApp(platform::Window& window, gpu::GpuDevice& gpu,
                                             const AppSettings& settings)
{
    assets::AssetRegistry registry;
    if (auto scanned = scanAssetRoots(settings, registry); !scanned)
    {
        return std::unexpected(scanned.error());
    }
    auto skinning = render::createSkinningPass(*gpu.nvrhi);
    if (!skinning)
    {
        return std::unexpected(skinning.error());
    }
    const std::optional<std::filesystem::path> skyPath = skyPathOf(settings);
    auto sky = loadSky(*gpu.nvrhi, skyPath, settings);
    if (!sky)
    {
        return std::unexpected(sky.error());
    }
    // Le ciel en fond, avec une HDRI seulement : sans elle, le fond reste celui de `App`.
    auto renderer =
        render::createRenderer(*gpu.nvrhi, gpu::swapchainFormat(gpu), std::move(sky->environment),
                               skyPath && skyPath->native() != NoSky);
    if (!renderer)
    {
        return std::unexpected(renderer.error());
    }
#if defined(LEVAIN_PROFILING_ENABLED) && LEVAIN_PROFILING_ENABLED
    // Le temps GPU de chaque fonction d'étape (#295), en build profilé seulement : ailleurs, il
    // coûterait ce qu'il mesure (engine/render/README.md).
    renderer->stages.timeFunctions = true;
#endif
    // Les liaisons d'entrée : changer une touche ne demande aucune recompilation (ADR-0017). Un
    // nom inconnu échoue ici, avec son numéro de ligne.
    auto bindings = input::loadBindings(settings.bindingsFile);
    if (!bindings)
    {
        return std::unexpected(bindings.error());
    }
    input::InputState inputState = input::makeInputState(*bindings);
    const render::SamplerSettings sampler{.maxAnisotropy = settings.maxAnisotropy};
    core::log("app", core::LogLevel::Info, "filtrage anisotrope : {}",
              render::clampAnisotropy(sampler.maxAnisotropy));

    auto app = std::make_unique<App>(
        App{.window = window,
            .gpu = gpu,
            .settings = settings,
            .renderer = std::move(*renderer),
            .skinning = std::move(*skinning),
            .skinningState = createSkinningState(*gpu.nvrhi),
            .sampler = render::createSampler(*gpu.nvrhi, sampler),
            .world = flecs::world{},
            .fixedStep = {},
            .cameraEntity = {},
            .camera = {},
            .registry = std::move(registry),
            .modelCache = {},
            .models = {},
            .sun = settings.sunDirection ? render::Sun{.direction = *settings.sunDirection,
                                                       .color = glm::vec3{1.0f},
                                                       .intensity = 1.0f}
                                         : sky->sun,
            .lights = {},
            .bindings = std::move(*bindings),
            .input = std::move(inputState),
            .frameTimer = render::createGpuTimer(*gpu.nvrhi),
            .totalGpu = {},
            .frameCount = 0,
            .hooks = {}});
    app->world.import<scene::SceneModule>();
    app->world.import<assets::AssetsModule>();
#ifdef LEVAIN_ENABLE_EXPLORER
    enableExplorerOnLoopback(app->world);
#endif
    core::log("app", core::LogLevel::Info, "liaisons : {} actions et {} axes ({})",
              app->bindings.actions.size(), app->bindings.axes.size(),
              settings.bindingsFile.filename().string());
    return app;
}

/// La caméra du rendu, relue sur son entité : sa **matrice monde** porte la position et le regard
/// déjà interpolés entre deux pas de simulation (ADR-0016). Lire le `Transform` ferait saccader le
/// regard dès que le rendu va plus vite que la simulation.
void updateRenderCamera(render::Camera& camera, const flecs::entity& cameraEntity)
{
    const glm::mat4& world = cameraEntity.get<scene::WorldTransform>().matrix;
    camera.position = glm::vec3(world[3]);
    camera.target = camera.position + glm::vec3(glm::mat3(world) * glm::vec3{0.0f, 0.0f, -1.0f});
}

/// Enregistre l'image, la présente, et rend le temps GPU d'une image précédente, dès qu'il est
/// lisible. Avec `capture`, l'image est aussi copiée pour être relue (`render::readBack`).
/// `displayWait`, s'il est donné, reçoit le temps passé dans `beginFrame` et `presentFrame` :
/// l'attente de l'écran ou du GPU, qui n'est pas du travail du moteur (#294). La recréation de la
/// swapchain et le ramasse-miettes de NVRHI, courts, y sont comptés aussi.
std::optional<double> renderFrame(App& app, nvrhi::ICommandList& commandList, double seconds,
                                  nvrhi::StagingTextureHandle* capture = nullptr,
                                  double* displayWait = nullptr)
{
    const Clock::time_point acquireStart = Clock::now();
    nvrhi::ITexture* backBuffer = gpu::beginFrame(app.gpu, app.window);
    const Clock::time_point acquireEnd = Clock::now();
    if (backBuffer == nullptr)
    {
        return std::nullopt;
    }

    std::optional<double> gpuMs;

    {
        // Le travail CPU d'une image, hors attente de l'écran (critère de M1.3,
        // tools/tracy-capture.sh).
        LEVAIN_PROFILE_SCOPE_NAMED("commandes");

        commandList.open();
        gpuMs = render::beginGpuTimer(*app.gpu.nvrhi, commandList, app.frameTimer);
        if (app.hooks.record)
        {
            app.hooks.record(app, commandList, seconds);
        }
        render::renderFrame(*app.gpu.nvrhi, commandList, app.renderer,
                            {.camera = app.camera,
                             .sun = app.sun,
                             .environmentIntensity = 1.0f,
                             .lights = app.lights,
                             .tonemap = app.settings.tonemap,
                             .background = app.background,
                             .seconds = seconds},
                            *backBuffer);
        if (capture != nullptr)
        {
            *capture = render::copyForReadback(*app.gpu.nvrhi, commandList, *backBuffer);
        }
        render::endGpuTimer(commandList, app.frameTimer);
        commandList.close();
        app.gpu.nvrhi->executeCommandList(&commandList);
    }

    LEVAIN_PROFILE_SCOPE_NAMED("présentation");
    const Clock::time_point presentStart = Clock::now();
    gpu::presentFrame(app.gpu);
    if (displayWait != nullptr)
    {
        *displayWait =
            secondsBetween(acquireStart, acquireEnd) + secondsBetween(presentStart, Clock::now());
    }
    return gpuMs;
}

#ifndef __EMSCRIPTEN__
/// Rend une dernière image et l'écrit en PNG. Un échec est bruyant (règle n°7) : une capture
/// demandée et absente ferait croire à une image qui n'existe pas.
bool captureFrame(App& app, nvrhi::ICommandList& commandList, double seconds,
                  const std::filesystem::path& path)
{
    nvrhi::StagingTextureHandle staging;
    // std::addressof et non « & » : le RefCountPtr de NVRHI surcharge l'opérateur & (il rend
    // l'adresse du pointeur brut, comme les ComPtr de COM).
    static_cast<void>(renderFrame(app, commandList, seconds, std::addressof(staging)));
    if (!staging)
    {
        core::log("app", core::LogLevel::Error,
                  "capture impossible : aucune image rendue (fenêtre masquée ?)");
        return false;
    }
    auto image = render::readBack(*app.gpu.nvrhi, *staging);
    auto saved = image ? assets::savePng(path, image->width, image->height, image->rgba)
                       : std::unexpected(image.error());
    if (!saved)
    {
        core::log("app", core::LogLevel::Error, "capture : {}", saved.error().message);
        return false;
    }
    core::log("app", core::LogLevel::Info, "capture : {} ({} × {})", path.string(), image->width,
              image->height);
    return true;
}
#endif

/// Ce que la boucle garde d'une image à l'autre. Une image est une fonction (`runFrame`) : en
/// natif, la boucle l'appelle ; dans le navigateur, c'est lui, à chaque image (ADR-0023, point 3).
struct Loop
{
    App& app;
    nvrhi::CommandListHandle commandList;
    LoopState state;
    core::FrameTimeAccumulator frameTimes;
    render::GpuTimeAverage periodGpu; ///< Depuis la dernière mise à jour du titre.
    /// Le temps passé dans le moteur, hors attente de l'écran, depuis la dernière mise à jour du
    /// titre (#294).
    double periodEngineSeconds = 0.0;
    Clock::time_point loopStart;
    Clock::time_point previousFrameEnd;
    ShaderReload shaderReload;
    TextureReload textureReload;
    nvrhi::FramebufferInfo sceneTarget;
    /// La durée de l'image précédente. La première n'en a pas : un pas de simulation, pour
    /// démarrer.
    double lastFrameSeconds;
};

Loop startLoop(App& app)
{
    const Clock::time_point now = Clock::now();
    return Loop{.app = app,
                .commandList = app.gpu.nvrhi->createCommandList(),
                .state = {},
                .frameTimes = {},
                .periodGpu = {},
                .periodEngineSeconds = 0.0,
                .loopStart = now,
                .previousFrameEnd = now,
                .shaderReload = startShaderReload(app.settings.shaderBuild),
                .textureReload = startTextureReload(app.registry),
                .sceneTarget = render::sceneTargetInfo(),
                .lastFrameSeconds = app.fixedStep.stepSeconds};
}

/// Le temps de la scène : celui de la boucle, ou celui de `--time`, figé.
double sceneSecondsOf(const Loop& loop)
{
    return loop.app.settings.frozenSeconds.value_or(secondsBetween(loop.loopStart, Clock::now()));
}

/// Une image de la boucle ; `false` quand elle s'arrête (fenêtre fermée, `--seconds` écoulées,
/// `--steps` joués).
bool runFrame(Loop& loop)
{
    App& app = loop.app;
    const AppSettings& settings = app.settings;
    if (!loop.state.isRunning ||
        secondsBetween(loop.loopStart, Clock::now()) >= settings.loopSeconds ||
        (settings.steps && app.frameCount >= *settings.steps))
    {
        return false;
    }

    if (!loop.state.isVisible)
    {
#ifdef __EMSCRIPTEN__
        // Le navigateur n'appelle plus un onglet caché : rien à attendre, et rien ne s'y attend.
        return true;
#endif
        // Pas au-delà de --seconds : masquée sans événement (bureau verrouillé), la boucle
        // dormirait sinon indéfiniment. L'infini par défaut attend sans limite.
        const double remainingSeconds =
            settings.loopSeconds - secondsBetween(loop.loopStart, Clock::now());
        for (const auto& event : platform::waitEvents(app.window, remainingSeconds).window)
        {
            applyWindowEvent(loop.state, event);
        }

        // Le temps passé masquée n'est pas une image. Sans cette remise à l'heure, la première
        // image après la restauration durerait toute la minimisation, et le maximum affiché serait
        // de plusieurs secondes.
        loop.previousFrameEnd = Clock::now();
        return true;
    }

    const Clock::time_point frameStart = Clock::now();
    double displayWait = 0.0;
    {
        LEVAIN_PROFILE_SCOPE_NAMED("événements");

        const platform::Events events = platform::pollEvents(app.window);
        for (const auto& event : events.window)
        {
            applyWindowEvent(loop.state, event);
            forgetHiddenTime(loop.previousFrameEnd, event, frameStart);
        }
        input::updateInput(app.input, app.bindings, events.input,
                           static_cast<float>(loop.lastFrameSeconds));
        // Ce que le joueur demande, posé pour le prochain pas de simulation.
        if (app.hooks.frame)
        {
            app.hooks.frame(app);
        }
    }

    reloadChangedShaders(loop.shaderReload, *app.gpu.nvrhi, loop.sceneTarget,
                         app.renderer.meshPass);
    reloadChangedTextures(loop.textureReload, *app.gpu.nvrhi, app.registry, app.modelCache,
                          app.models, app.renderer.meshPass, *app.sampler);

    {
        // Un pas du monde : les pas de simulation que la dernière image a mérités, puis une passe
        // de rendu qui interpole et compose les matrices monde (ADR-0016). La durée passée est
        // celle de l'image précédente : celle-ci n'est pas encore finie.
        LEVAIN_PROFILE_SCOPE_NAMED("monde");
        scene::advanceWorld(app.world, app.fixedStep,
                            settings.steps ? app.fixedStep.stepSeconds
                                           : static_cast<float>(loop.lastFrameSeconds));
        updateRenderCamera(app.camera, app.cameraEntity);
    }

    {
        LEVAIN_PROFILE_SCOPE_NAMED("rendu");
        if (const auto gpuMs =
                renderFrame(app, *loop.commandList, sceneSecondsOf(loop), nullptr, &displayWait))
        {
            loop.periodGpu.totalMs += *gpuMs;
            ++loop.periodGpu.samples;
            app.totalGpu.totalMs += *gpuMs;
            ++app.totalGpu.samples;
        }
    }

    // Fin d'image : ce que plus aucune entité n'utilise se décharge, du CPU et du GPU
    // (ADR-0019). NVRHI garde vivantes les ressources qu'une command list en vol utilise encore.
    for (const assets::AssetId& unused : assets::takeUnusedAssets(app.world))
    {
        app.models.erase(unused);
        app.modelCache.models.erase(unused);
    }

    const Clock::time_point frameEnd = Clock::now();
    const double frameSeconds = secondsBetween(loop.previousFrameEnd, frameEnd);
    loop.previousFrameEnd = frameEnd;
    loop.lastFrameSeconds = frameSeconds;
    loop.periodEngineSeconds += secondsBetween(frameStart, frameEnd) - displayWait;

    if (const auto summary =
            core::recordFrame(loop.frameTimes, frameSeconds, FrameTimePeriodSeconds))
    {
        LEVAIN_PROFILE_SCOPE_NAMED("titre");
        const FrameReport report = frameReportOf(*summary, loop.periodEngineSeconds,
                                                 platform::windowPixelSize(app.window));
        platform::setWindowTitle(app.window, describeFrameTimes(settings.title, report,
                                                                render::averageOf(loop.periodGpu)));
#ifdef __EMSCRIPTEN__
        reportFrame(report);
#endif
        loop.periodGpu = {};
        loop.periodEngineSeconds = 0.0;
    }

    ++app.frameCount;
    LEVAIN_PROFILE_FRAME();
    return true;
}

/// Le bilan de la boucle, celui du programme, puis la capture demandée ; `false` si l'un a échoué.
bool finishLoop(Loop& loop)
{
    App& app = loop.app;
    // Lu par la CI, qui échoue si la boucle a tourné moins d'une seconde : un démarrage lent
    // (lavapipe, validation, sanitizers) peut sinon manger tout le délai sans que rien ne rougisse.
    core::log("app", core::LogLevel::Info,
              "boucle arrêtée après {:.1f} s et {} frames ; GPU : {:.3f} ms en moyenne sur {} "
              "mesures",
              secondsBetween(loop.loopStart, Clock::now()), app.frameCount,
              render::averageOf(app.totalGpu), app.totalGpu.samples);
    // Arrêtée avant ses `--steps` (fenêtre fermée, `--seconds` écoulées), la boucle n'a pas joué
    // ce que la CI vérifie ensuite : elle échoue (règle n°7).
    if (app.settings.steps && app.frameCount < *app.settings.steps)
    {
        core::log("app", core::LogLevel::Error, "--steps : {} pas simulés sur les {} demandés",
                  app.frameCount, *app.settings.steps);
        return false;
    }
    // Le critère de M5.3 : le temps GPU de la passe d'ombres, quatre cascades.
    const auto& passTimes = app.renderer.passTimes;
    const render::GpuTimeAverage& shadowGpu = passTimes[1]; // RendererPassNames : « ombres »
    core::log("app", core::LogLevel::Info, "ombres : {:.3f} ms GPU en moyenne sur {} mesures",
              render::averageOf(shadowGpu), shadowGpu.samples);
    // Le critère de #133 : le temps GPU de chaque passe. Une étape où rien n'est inscrit n'est pas
    // chronométrée, et n'apparaît pas.
    std::string passes;
    for (std::size_t pass = 0; pass < render::RendererPassNames.size(); ++pass)
    {
        if (passTimes[pass].samples > 0)
        {
            passes +=
                std::format("{}{} {:.3f} ms", passes.empty() ? "" : ", ",
                            render::RendererPassNames[pass], render::averageOf(passTimes[pass]));
        }
    }
    core::log("app", core::LogLevel::Info, "passes, GPU en moyenne : {}", passes);
    // Le détail des étapes (#295), en build profilé : ce que coûte chaque fonction inscrite,
    // terrain, herbe, eau, toutes cascades d'ombres comprises.
    if (app.renderer.stages.timeFunctions)
    {
        core::log("app", core::LogLevel::Info, "étapes, GPU en moyenne : {}",
                  render::describeStageTimes(app.renderer.stages));
    }
    if (app.hooks.finish && !app.hooks.finish(app))
    {
        return false;
    }
#ifdef __EMSCRIPTEN__
    // Le navigateur ne laisse pas attendre le GPU (ADR-0023) : la relecture de l'image ne s'y fait
    // pas. La capture se demande au build natif, `--gpu webgpu` compris.
    if (app.settings.capturePath)
    {
        core::log("app", core::LogLevel::Warning, "--capture ignoré dans le navigateur");
    }
    return true;
#else
    return !app.settings.capturePath ||
           captureFrame(app, *loop.commandList, sceneSecondsOf(loop), *app.settings.capturePath);
#endif
}

/// Après le device : `App`, puis la fonction de démarrage, qui doit poser la caméra.
core::Result<std::unique_ptr<App>> startApp(platform::Window& window, gpu::GpuDevice& gpu,
                                            const AppSettings& settings, const StartFunction& start)
{
    auto app = createApp(window, gpu, settings);
    if (!app)
    {
        return std::unexpected(app.error());
    }
    auto hooks = start(**app);
    if (!hooks)
    {
        return std::unexpected(hooks.error());
    }
    (*app)->hooks = std::move(*hooks);
    if (!(*app)->cameraEntity)
    {
        return core::makeError(core::ErrorCode::InvalidData,
                               "le démarrage n'a pas posé de caméra (App::cameraEntity)");
    }
    // Les matrices monde, avant la première image.
    scene::advanceWorld((*app)->world, (*app)->fixedStep, 0.0f);
    return app;
}

#ifdef __EMSCRIPTEN__
/// Dans le navigateur, `runApp` rend la main avant que le device n'arrive (ADR-0023, point 2) : ce
/// que la boucle utilise vit ici, jusqu'à la fermeture de l'onglet.
struct WebApp
{
    AppSettings settings;
    StartFunction start;
    std::optional<platform::Window> window;
    std::optional<gpu::GpuDevice> gpu;
    std::unique_ptr<App> app;
    std::optional<Loop> loop;
};

WebApp& webApp()
{
    static WebApp instance;
    return instance;
}

void runWebFrame()
{
    WebApp& web = webApp();
    if (!runFrame(*web.loop))
    {
        std::ignore = finishLoop(*web.loop);
        emscripten_cancel_main_loop();
    }
}

/// La suite de `runApp`, quand le navigateur a donné le device.
void startWebApp(core::Result<gpu::GpuDevice> gpu)
{
    WebApp& web = webApp();
    if (!gpu)
    {
        core::log("app", core::LogLevel::Critical, "{}", gpu.error().message);
        return;
    }
    web.gpu.emplace(std::move(*gpu));
    auto app = startApp(*web.window, *web.gpu, web.settings, web.start);
    if (!app)
    {
        core::log("app", core::LogLevel::Critical, "{}", app.error().message);
        return;
    }
    web.app = std::move(*app);
    web.loop.emplace(startLoop(*web.app));
    // 0 : au rythme de requestAnimationFrame, celui de l'écran.
    emscripten_set_main_loop(runWebFrame, 0, false);
}
#endif

} // namespace

std::optional<double> parsePositive(std::string_view text)
{
    double value = 0.0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || !(value > 0.0))
    {
        return std::nullopt;
    }
    return value;
}

std::optional<glm::vec3> parseVector(std::string_view text)
{
    glm::vec3 vector{0.0f};
    const char* cursor = text.data();
    const char* const end = text.data() + text.size();
    for (int axis = 0; axis < 3; ++axis)
    {
        const auto [next, error] = std::from_chars(cursor, end, vector[axis]);
        const bool last = axis == 2;
        if (error != std::errc{} || (last ? next != end : (next == end || *next != ',')))
        {
            return std::nullopt;
        }
        cursor = next + 1;
    }
    return vector;
}

OptionUse parseCommonOption(AppSettings& settings, std::string_view name, std::string_view value)
{
    if (name == "--capture" || name == "--sky")
    {
        (name == "--capture" ? settings.capturePath : settings.sky) = std::filesystem::path{value};
        return OptionUse::Taken;
    }
    if (name == "--gpu")
    {
        if (value != "vulkan" && value != "webgpu")
        {
            return OptionUse::Invalid;
        }
        settings.api = value == "webgpu" ? nvrhi::GraphicsAPI::WEBGPU : nvrhi::GraphicsAPI::VULKAN;
        return OptionUse::Taken;
    }
    if (name == "--tonemap")
    {
        constexpr std::array<std::pair<std::string_view, render::Tonemapper>, 4> Tonemappers{
            {{"clip", render::Tonemapper::Clip},
             {"aces", render::Tonemapper::Aces},
             {"agx", render::Tonemapper::Agx},
             {"neutral", render::Tonemapper::KhronosPbrNeutral}}};
        const auto found = std::ranges::find(
            Tonemappers, value, &std::pair<std::string_view, render::Tonemapper>::first);
        if (found == Tonemappers.end())
        {
            return OptionUse::Invalid;
        }
        settings.tonemap.tonemapper = found->second;
        return OptionUse::Taken;
    }
    if (name == "--sun")
    {
        settings.sunDirection = parseVector(value);
        return settings.sunDirection ? OptionUse::Taken : OptionUse::Invalid;
    }
    if (name == "--steps")
    {
        const std::optional<double> count = parsePositive(value);
        if (!count || *count != std::floor(*count) || *count > 1e6)
        {
            return OptionUse::Invalid;
        }
        settings.steps = static_cast<int>(*count);
        return OptionUse::Taken;
    }
    if (name != "--seconds" && name != "--exposure" && name != "--anisotropy" && name != "--time")
    {
        return OptionUse::NotMine;
    }
    const std::optional<double> number = parsePositive(value);
    if (!number)
    {
        return OptionUse::Invalid;
    }
    if (name == "--seconds")
    {
        settings.loopSeconds = *number;
    }
    else if (name == "--exposure")
    {
        settings.tonemap.exposure = static_cast<float>(*number);
    }
    else if (name == "--anisotropy")
    {
        settings.maxAnisotropy = static_cast<float>(*number);
    }
    else
    {
        settings.frozenSeconds = number;
    }
    return OptionUse::Taken;
}

int runApp(const AppSettings& settings, const StartFunction& start)
{
    auto window = platform::createWindow(settings.title, settings.width, settings.height);
    if (!window)
    {
        core::log("app", core::LogLevel::Critical, "{}", window.error().message);
        return 1;
    }

#ifdef __EMSCRIPTEN__
    WebApp& web = webApp();
    web.settings = settings;
    web.start = start;
    web.window.emplace(std::move(*window));
    gpu::requestGpuDevice(*web.window, {.enableValidation = EnableValidation}, startWebApp);
    return 0;
#else
    // Déclaré après window, gpu sera détruit avant elle : la surface Vulkan doit disparaître
    // avant la fenêtre SDL qui la porte. Et `app`, déclarée après gpu, avant lui.
    const Clock::time_point deviceStart = Clock::now();
    auto gpu =
        gpu::createGpuDevice(*window, {.enableValidation = EnableValidation, .api = settings.api});
    if (!gpu)
    {
        core::log("app", core::LogLevel::Critical, "{}", gpu.error().message);
        return 1;
    }
    core::log("app", core::LogLevel::Info, "device créé en {:.1f} ms",
              secondsBetween(deviceStart, Clock::now()) * 1000.0);

    auto app = startApp(*window, *gpu, settings, start);
    if (!app)
    {
        core::log("app", core::LogLevel::Critical, "{}", app.error().message);
        return 1;
    }
    Loop loop = startLoop(**app);
    while (runFrame(loop))
    {
    }
    if (!finishLoop(loop))
    {
        return 1;
    }
    core::log("app", core::LogLevel::Info, "fenêtre fermée");
    return 0;
#endif
}

} // namespace levain::app
