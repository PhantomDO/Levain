#include "levain/app/app.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

#include <imgui.h>

#include "shader_reload.hpp"

#include "levain/app/camera.hpp"
#include "levain/app/player_input.hpp"
#include "levain/app/texture_reload.hpp"
#include "levain/assets/image.hpp"
#include "levain/core/assert.hpp"
#include "levain/core/frame_time.hpp"
#include "levain/core/log.hpp"
#include "levain/core/profile.hpp"
#include "levain/platform/input.hpp"
#include "levain/render/culling.hpp"
#include "levain/render/mesh_pass.hpp"
#include "levain/render/readback.hpp"
#include "levain/render/shadows.hpp"
#include "levain/render/sky.hpp"
#include "levain/render/stages.hpp"
#include "levain/scene/components.hpp"
#include "levain/scene/scene.hpp"
#include "levain/ui/context.hpp"
#include "levain/ui/input.hpp"

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
                                double engineMs, double enginePercent, int width, int height,
                                double uiMs, int uiDraws), {
    if (Module.onFrameReport) {
        Module.onFrameReport({imagesPerSecond, averageMs, maxMs, engineMs, enginePercent, width,
                              height, uiMs, uiDraws});
    }
});
// clang-format on

/// `uiMs` : le temps CPU moyen de l'UI depuis le départ (ADR-0032) ; `uiDraws`, les commandes de
/// sa dernière image, que `tools/web-smoke.mjs` veut non nulles quand les panneaux sont ouverts.
/// Son temps GPU n'y est pas : le backend WebGPU ne relit pas les minuteurs.
void reportFrame(const FrameReport& report, double uiMs, std::uint32_t uiDraws)
{
    reportFrameToPage(report.imagesPerSecond, report.times.averageMs, report.times.maxMs,
                      report.engineMs, report.enginePercent, report.pixels.width,
                      report.pixels.height, uiMs, static_cast<int>(uiDraws));
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
/// les ombres, sauf si les réglages le laissent dans l'éclairage ambiant (`extractSkySun`).
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

/// Un modèle sans mouvement donné par le programme reste au repos.
animation::CharacterMotion restingMotion(const assets::AssetId&, double)
{
    return {};
}

/// Les dessins de l'étape « modèles » : chaque primitive de chaque entité qui porte un `MeshRef`, à
/// sa matrice monde, si elle touche `frustum`. Les autres sont comptées dans `count`, sans être
/// soumises au GPU (#132).
template <typename Draw>
void forEachModelDraw(App& app, const render::Frustum& frustum, DrawCount& count, Draw&& draw)
{
    app.modelParts.each(
        [&](const assets::MeshRef& part, const scene::WorldTransform& world)
        {
            const auto found = app.models.find(part.mesh.asset);
            if (found == app.models.end())
            {
                // Le contrat de l'étape : un modèle chargé par `loadModel`. Une entité instanciée
                // sans lui n'a rien sur le GPU ; le dire une fois, plutôt que de lever au milieu
                // d'une étape du rendu (règle n°7).
                if (!app.warnedUnloadedModel)
                {
                    core::log("app", core::LogLevel::Error,
                              "{} : son modèle n'a pas été chargé par loadModel, rien à dessiner",
                              assets::toString(part.mesh.asset));
                    app.warnedUnloadedModel = true;
                }
                return;
            }
            const ModelGpu& model = found->second;
            for (const ModelPrimitiveGpu& primitive : model.meshes[part.mesh.sub])
            {
                if (const auto box =
                        render::worldBoundsOf(primitive.mesh, app.modelInstance, world.matrix);
                    box && render::isOutside(frustum, *box))
                {
                    ++count.culled;
                    continue;
                }
                ++count.drawn;
                count.triangles += std::uint64_t{primitive.mesh.indexCount / 3};
                draw(primitive.mesh,
                     primitive.material ? *model.materials[*primitive.material]
                                        : *app.defaultMaterial,
                     world.matrix);
            }
        });
}

/// Ce que l'étape « modèles » demande avant la première image : sa requête, son instance unique,
/// le matériau par défaut, et ses deux fonctions d'étape, l'ombre et la couleur (ADR-0025). Elles
/// gardent `app` par référence : il ne bouge pas. Inscrites avant la fonction de démarrage, elles
/// passent avant celles du programme.
void prepareModels(App& app)
{
    nvrhi::IDevice& device = *app.gpu.nvrhi;
    app.modelParts = app.world.query<const assets::MeshRef, const scene::WorldTransform>();
    const nvrhi::CommandListHandle upload = device.createCommandList();
    upload->open();
    const std::array<render::InstancePose, 1> origin{};
    app.modelInstance = render::createInstances(device, *upload, origin);
    app.defaultMaterial = render::createMaterialBindings(
        device, *upload, app.renderer.meshPass,
        {.baseColorFactor = glm::vec4{1.0f},
         .metallicFactor = 0.0f,
         .roughnessFactor = 0.8f,
         .normalScale = 1.0f,
         .padding = 0.0f},
        render::withDefaults({}, render::createMaterialDefaults(device, *upload)), *app.sampler);
    upload->close();
    device.executeCommandList(upload);

    using render::RenderStage;
    using render::StageContext;
    render::addStageFunction(
        app.renderer.stages, RenderStage::ShadowCasters, "modèles",
        [&app](const StageContext& context)
        {
            forEachModelDraw(
                app, context.frustum, app.modelsShadows,
                [&](const render::Mesh& mesh, nvrhi::IBindingSet&, const glm::mat4& matrix)
                {
                    render::drawShadowCaster(context.commandList, context.shadows, context.cascade,
                                             *context.cascadeView, mesh, app.modelInstance, matrix);
                });
        });
    render::addStageFunction(
        app.renderer.stages, RenderStage::Opaque, "modèles",
        [&app](const StageContext& context)
        {
            forEachModelDraw(
                app, context.frustum, app.modelsCamera,
                [&](const render::Mesh& mesh, nvrhi::IBindingSet& material, const glm::mat4& matrix)
                {
                    render::drawMesh(context.commandList, app.renderer.meshPass, context.frame,
                                     context.target, mesh, app.modelInstance, material,
                                     {.viewProjection = context.viewProjection, .model = matrix});
                });
        });
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
    // L'UI dessine sur l'image finale, après le tonemapping (ADR-0032).
    auto uiPass = ui::createUiPass(
        *gpu.nvrhi, nvrhi::FramebufferInfo().addColorFormat(gpu::swapchainFormat(gpu)));
    if (!uiPass)
    {
        return std::unexpected(uiPass.error());
    }
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
            .camera = {},
            .registry = std::move(registry),
            .modelCache = {},
            .models = {},
            .modelParts = {},
            .modelInstance = {},
            .defaultMaterial = {},
            .modelsCamera = {},
            .modelsShadows = {},
            .cameras = {},
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
            .mouseCaptureWanted = false,
            .mouseCaptured = false,
            .ui = UiLayer{.context = ui::createUiContext(platform::displayScale(window)),
                          .pass = std::move(*uiPass),
                          .timer = render::createGpuTimer(*gpu.nvrhi),
                          .panelsOpen = settings.showUiPanels},
            .hooks = {}});
    app->world.import<scene::SceneModule>();
    app->world.import<assets::AssetsModule>();
#ifdef LEVAIN_ENABLE_EXPLORER
    enableExplorerOnLoopback(app->world);
#endif
    app->world.set<PlayerInput>(
        {.bindings = &app->bindings, .state = app->input, .pressesUntilNextStep = {}});
    forgetPressesAtEachStep(app->world);
    app->cameras = app->world.query<const CameraLens, const scene::WorldTransform>();
    prepareModels(*app);
    core::log("app", core::LogLevel::Info, "liaisons : {} actions et {} axes ({})",
              app->bindings.actions.size(), app->bindings.axes.size(),
              settings.bindingsFile.filename().string());
    return app;
}

/// Le début d'une image d'UI (ADR-0032, « L'ordre d'une image ») : ImGui reçoit l'input, puis
/// `NewFrame` le traite ; ce n'est qu'après que `WantCaptureMouse` et `WantCaptureKeyboard` sont à
/// jour. F1 ouvre ou ferme les panneaux ; la saisie de texte suit ce que veut ImGui.
void beginUiFrame(App& app, const platform::Events& events, double frameSeconds)
{
    ImGuiIO& io = ImGui::GetIO();
    ui::feedInput(io, events, app.ui.pressedKeys);
    ui::prepareUiFrame(io, platform::windowPixelSize(app.window), frameSeconds);
    ImGui::NewFrame();
    if (ImGui::IsKeyPressed(ImGuiKey_F1, false))
    {
        app.ui.panelsOpen = !app.ui.panelsOpen;
    }
    ui::followTextInput(app.window, io.WantTextInput, app.ui.textInputActive);
}

/// La fin d'une image d'UI : les fenêtres du programme, puis `ImGui::Render`. Le rendu de l'image
/// la dessinera.
void endUiFrame(App& app)
{
    if (app.hooks.ui)
    {
        app.hooks.ui(app);
    }
    ImGui::Render();
    app.ui.frameReady = true;
}

/// La souris, capturée ou non selon ce que veut le programme et ce que montre l'UI. La boucle seule
/// appelle `setMouseCaptured` : un programme qui la capturait lui-même se croirait encore capturé
/// quand l'UI la libère, et ne la reprendrait jamais.
void applyMouseCapture(App& app)
{
    const bool captured = mouseShouldBeCaptured(app.mouseCaptureWanted, app.ui.panelsOpen);
    if (captured != app.mouseCaptured)
    {
        platform::setMouseCaptured(app.window, captured);
        app.mouseCaptured = captured;
    }
}

/// L'UI de l'image, dans la command list, sur l'image finale : ses textures, ses sommets, ses
/// dessins, et son minuteur GPU quand elle dessine quelque chose. Son temps CPU s'ajoute aux
/// tranches de l'image, que `runFrame` compte.
void recordUiFrame(App& app, nvrhi::ICommandList& commandList, nvrhi::ITexture& backBuffer)
{
    const Clock::time_point start = Clock::now();
    ImDrawData& drawData = *ImGui::GetDrawData();
    const bool drawsSomething = drawData.TotalVtxCount > 0;
    std::optional<double> gpuMs;
    if (drawsSomething)
    {
        gpuMs = render::beginGpuTimer(*app.gpu.nvrhi, commandList, app.ui.timer);
    }
    const nvrhi::FramebufferHandle target =
        app.gpu.nvrhi->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(&backBuffer));
    app.ui.lastDraw = ui::recordUi(*app.gpu.nvrhi, commandList, app.ui.pass, drawData, *target);
    if (drawsSomething)
    {
        render::endGpuTimer(commandList, app.ui.timer);
    }
    if (gpuMs)
    {
        app.ui.cost.gpu.totalMs += *gpuMs;
        ++app.ui.cost.gpu.samples;
        app.ui.cost.gpuMaxMs = std::max(app.ui.cost.gpuMaxMs, *gpuMs);
    }
    app.ui.totalDraws += app.ui.lastDraw.draws;
    app.ui.frameCpuMs += secondsBetween(start, Clock::now()) * 1000.0;
    app.ui.frameReady = false;
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
        // Les poses des modèles skinnés, avant les dessins qui lisent leurs sommets déformés.
        animateModels(*app.gpu.nvrhi, commandList, app.models, app.skinning, app.skinningState,
                      app.hooks.motionOf ? app.hooks.motionOf : MotionOf{restingMotion}, seconds);
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
        // Après le tonemapping, sur l'image finale, et avant la copie d'une capture (ADR-0032).
        if (app.ui.frameReady)
        {
            recordUiFrame(app, commandList, *backBuffer);
        }
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
    // Une image d'UI pour la capture, sans input : avec `--ui on`, les panneaux s'y voient.
    beginUiFrame(app, {}, app.fixedStep.stepSeconds);
    endUiFrame(app);
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
    /// Une image a échoué (la caméra a disparu) : la boucle s'arrête, et le programme aussi.
    bool failed = false;
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
                .failed = false,
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
        // ImGui d'abord : ce qu'il garde de l'input, le jeu ne le voit pas (`gameInputOf`).
        const Clock::time_point uiStart = Clock::now();
        beginUiFrame(app, events, loop.lastFrameSeconds);
        const ImGuiIO& io = ImGui::GetIO();
        const std::vector<platform::InputEvent> gameEvents =
            gameInputOf(events.input, app.input.raw, io.WantCaptureMouse, io.WantCaptureKeyboard);
        app.ui.frameCpuMs += secondsBetween(uiStart, Clock::now()) * 1000.0;
        input::updateInput(app.input, app.bindings, gameEvents,
                           static_cast<float>(loop.lastFrameSeconds));
        takeFrameInput(app.world.get_mut<PlayerInput>(), app.input);
        // Ce que le joueur demande, posé pour le prochain pas de simulation.
        if (app.hooks.frame)
        {
            app.hooks.frame(app);
        }
        applyMouseCapture(app);
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
        auto camera = renderCameraOf(app.cameras);
        if (!camera)
        {
            core::log("app", core::LogLevel::Error, "{}", camera.error().message);
            loop.failed = true;
            return false;
        }
        app.camera = *camera;
    }

    {
        LEVAIN_PROFILE_SCOPE_NAMED("interface");
        const Clock::time_point uiStart = Clock::now();
        endUiFrame(app);
        app.ui.frameCpuMs += secondsBetween(uiStart, Clock::now()) * 1000.0;
    }

    std::optional<float> frameGpuMs;
    {
        LEVAIN_PROFILE_SCOPE_NAMED("rendu");
        if (const auto gpuMs =
                renderFrame(app, *loop.commandList, sceneSecondsOf(loop), nullptr, &displayWait))
        {
            frameGpuMs = static_cast<float>(*gpuMs);
            loop.periodGpu.totalMs += *gpuMs;
            ++loop.periodGpu.samples;
            app.totalGpu.totalMs += *gpuMs;
            ++app.totalGpu.samples;
        }
    }
    // Le coût CPU de l'UI (le critère de M7.1), compté à chaque image, présentée ou non : sans
    // image à dessiner (fenêtre réduite, swapchain à recréer), ses tranches s'ajouteraient sinon
    // à l'image suivante, qui paraîtrait coûter toute l'attente.
    recordUiCpu(app.ui.cost, app.ui.frameCpuMs);
    app.ui.frameCpuMs = 0.0;

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
    recordHistory(app.ui.history, static_cast<float>(frameSeconds * 1000.0), frameGpuMs);

    if (const auto summary =
            core::recordFrame(loop.frameTimes, frameSeconds, FrameTimePeriodSeconds))
    {
        LEVAIN_PROFILE_SCOPE_NAMED("titre");
        const FrameReport report = frameReportOf(*summary, loop.periodEngineSeconds,
                                                 platform::windowPixelSize(app.window));
        platform::setWindowTitle(app.window, describeFrameTimes(settings.title, report,
                                                                render::averageOf(loop.periodGpu)));
#ifdef __EMSCRIPTEN__
        const UiCost& uiCost = app.ui.cost;
        reportFrame(report, uiCost.cpuSamples > 0 ? uiCost.cpuTotalMs / uiCost.cpuSamples : 0.0,
                    app.ui.lastDraw.draws);
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
    if (loop.failed)
    {
        return false;
    }
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
    // Le critère de M7.1 (ADR-0032) : le coût de l'UI. Sous WebGPU, notre backend ne relit pas les
    // minuteurs : « non mesuré », jamais 0.
    const UiCost& ui = app.ui.cost;
    const std::string uiGpu = ui.gpu.samples > 0
                                  ? std::format("{:.3f} ms en moyenne, {:.3f} au pire",
                                                render::averageOf(ui.gpu), ui.gpuMaxMs)
                                  : std::string{"non mesuré"};
    core::log("app", core::LogLevel::Info,
              "ui : CPU {:.3f} ms en moyenne, {:.3f} au pire ; GPU {} ; {} commandes dessinées",
              ui.cpuSamples > 0 ? ui.cpuTotalMs / ui.cpuSamples : 0.0, ui.cpuMaxMs, uiGpu,
              app.ui.totalDraws);
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
    // Les matrices monde, avant la première image, et la caméra par laquelle elle se verra.
    scene::advanceWorld((*app)->world, (*app)->fixedStep, 0.0f);
    auto camera = renderCameraOf((*app)->cameras);
    if (!camera)
    {
        return std::unexpected(camera.error());
    }
    (*app)->camera = *camera;
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
    if (name == "--ui")
    {
        if (value != "on" && value != "off")
        {
            return OptionUse::Invalid;
        }
        settings.showUiPanels = value == "on";
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
