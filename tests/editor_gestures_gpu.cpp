// Les gestes de la caméra de l'éditeur dans la vraie boucle (ADR-0036, décisions 6, 8 et 13,
// morceau 11), un script d'input rejoué hors écran (le banc d'essai de l'App) sous `withEditor`,
// panneaux ouverts :
//   - clic droit tenu dans la Vue + la touche à la place du W : la caméra avance de ce que le pas
//     fixe donne, en QWERTY et en AZERTY (la touche marquée W d'un AZERTY, à la place du Z, ne fait
//     rien) ; la souris n'est capturée que pendant le vol ;
//   - le même geste commencé hors de la Vue ne fait rien, ni ne capture ;
//   - le menu du clic droit s'ouvre à un clic sans vol, pas à la fin d'un vol ;
// clang-format off
//   - F, le double-clic dans la hiérarchie cadrent la sélection : sa boîte tient dans l'image ;
//   - un champ de texte actif n'est plus actif quand le vol commence ; en jeu, rien de tout cela ;
//   - un clic droit glissé vole menu ouvert ; le curseur revient au point de l'appui.
// clang-format on
// Il faut un device, comme `levain_app_script`.
//   levain_editor_gestures [vulkan|d3d12|d3d12-warp]

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <format>
#include <functional>
#include <optional>
#include <print>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <imgui.h>
// L'identifiant actif d'ImGui : son API interne.
#include <imgui_internal.h>

#include "gpu_test_backend.hpp"

#include "levain/app/app.hpp"
#include "levain/editor/camera.hpp"
#include "levain/editor/editor.hpp"
#include "levain/editor/scene_bounds.hpp"
#include "levain/platform/input.hpp"
#include "levain/render/camera.hpp"
#include "levain/scene/components.hpp"

namespace
{

using levain::app::App;
using levain::editor::EditorCamera;

constexpr int WindowWidth = 1280;
constexpr int WindowHeight = 720;
constexpr float Dt = 1.0f / 60.0f;

/// Ce que l'éditeur a fait à une image, relevé à la fin de `ui`.
struct Seen
{
    EditorCamera camera;
    bool captured = false;
    bool capturedOverPanels = false;
    bool menuOpen = false;
    bool flew = false;
    levain::editor::Gesture gesture = levain::editor::Gesture::None;
    levain::app::ScreenRect image{};
    flecs::entity_t selected = 0;
    bool overrideMatchesCamera = false;
    ImGuiID activeId = 0;
    bool playing = false;
    bool overridePresent = false;
    std::unordered_map<flecs::entity_t, levain::editor::RowRect> rows;
    bool targetInView = false; ///< La boîte de la cible tient dans l'image de la caméra.
    std::optional<levain::platform::CursorPosition> returnTo; ///< `App::mouseReturnTo`.
};

struct Run
{
    int exitCode = 0;
    std::vector<Seen> seen;
    App* app = nullptr;
    flecs::entity_t target = 0;
    flecs::entity_t other = 0;
};

/// Une caméra du jeu en (0, 2, 10) qui regarde vers −Z, une cible placée et mise à l'échelle, et
/// une autre entité (la sélection de départ). Avec `field`, une fenêtre d'ImGui au coin haut gauche
/// dont le champ de texte est actif à l'image 6 : ce que le vol doit désactiver.
levain::app::StartFunction programOf(Run& run, bool field)
{
    return [&run, field](App& app) -> levain::core::Result<levain::app::FrameHooks>
    {
        app.world.entity("camera")
            .set(levain::scene::Transform{.position = {0.0f, 2.0f, 10.0f}})
            .set(levain::app::CameraLens{});
        run.target = app.world.entity("cible")
                         .set(levain::scene::Transform{.position = {60.0f, 0.0f, -30.0f},
                                                       .scale = glm::vec3{4.0f}})
                         .id();
        run.other = app.world.entity("autre")
                        .set(levain::scene::Transform{.position = {-5.0f, 0.0f, 0.0f}})
                        .id();
        run.app = &app;
        levain::app::FrameHooks hooks;
        if (field)
        {
            hooks.ui = [text = std::array<char, 32>{"texte"}](App& uiApp) mutable
            {
                ImGui::SetNextWindowPos({4.0f, 4.0f});
                ImGui::SetNextWindowSize({150.0f, 60.0f});
                ImGui::Begin("###champ", nullptr, ImGuiWindowFlags_NoSavedSettings);
                if (uiApp.frameCount == 6)
                {
                    ImGui::SetKeyboardFocusHere();
                }
                ImGui::InputText("###texte", text.data(), text.size());
                ImGui::End();
            };
        }
        return hooks;
    };
}

/// La boîte de `entity` tient dans l'image de `camera` : tous ses coins en avant et dans le volume
/// de vue. Calculée pendant l'exécution (`seenOf`) : l'App n'existe plus ensuite.
bool boxInView(const App& app, flecs::entity_t entity, const EditorCamera& camera,
               const levain::app::ScreenRect& image)
{
    const flecs::entity target = app.world.entity(entity);
    const auto box = levain::editor::entityBoxOf(target, app.models, app.modelInstance);
    const auto planes = levain::editor::editorClipPlanes(camera, box);
    const auto rendered = levain::editor::toRenderCamera(camera, planes);
    const float aspect = image.width / image.height;
    const glm::mat4 viewProjection = levain::render::viewProjectionOf(rendered, aspect);
    for (std::size_t i = 0; i < 8; ++i)
    {
        const glm::vec3 corner{(i & 1U) != 0 ? box.max.x : box.min.x,
                               (i & 2U) != 0 ? box.max.y : box.min.y,
                               (i & 4U) != 0 ? box.max.z : box.min.z};
        const glm::vec4 clip = viewProjection * glm::vec4{corner, 1.0f};
        if (clip.w <= 0.0f || std::abs(clip.x / clip.w) > 1.0f || std::abs(clip.y / clip.w) > 1.0f)
        {
            return false;
        }
    }
    return true;
}

Seen seenOf(const App& app, const levain::editor::Editor& editor, flecs::entity_t target)
{
    Seen seen{.camera = editor.camera,
              .captured = app.mouseCaptured,
              .capturedOverPanels = app.mouseCaptureOverPanels,
              .menuOpen = editor.view.menuOpen,
              .flew = editor.gestures.flew,
              .gesture = editor.gestures.active,
              .image = editor.view.image,
              .selected = editor.selected,
              .activeId = ImGui::GetCurrentContext()->ActiveId,
              .playing = editor.mode.current == levain::editor::Mode::PlayWithoutReturn,
              .overridePresent = app.cameraOverride.has_value(),
              .rows = editor.hierarchy.rects,
              .targetInView = target != 0 && editor.view.image.height > 0.0f &&
                              boxInView(app, target, editor.camera, editor.view.image),
              .returnTo = app.mouseReturnTo};
    seen.overrideMatchesCamera =
        app.cameraOverride && app.cameraOverride->position == editor.camera.position;
    return seen;
}

Run play(const levain::tests::TestBackend& backend, std::string_view script, int frames,
         bool field = false, std::optional<std::string> select = std::nullopt)
{
    auto parsed = levain::platform::parseInputScript(script);
    if (!parsed)
    {
        throw std::runtime_error{parsed.error().message};
    }
    levain::app::AppSettings settings;
    settings.inputScript = std::move(*parsed);
    settings.steps = frames;
    settings.showUiPanels = true;
    settings.width = WindowWidth;
    settings.height = WindowHeight;
    settings.assetRoots = {LEVAIN_DATA_DIR};
    settings.bindingsFile = LEVAIN_DATA_DIR "/input.cfg";
    settings.api = backend.api;
    settings.adapter = backend.adapter;
    Run run;
    levain::editor::EditorOptions options;
    options.select = std::move(select);
    options.afterUi = [&run](const levain::editor::Editor& editor)
    { run.seen.push_back(seenOf(*run.app, editor, run.target)); };
    run.exitCode =
        levain::app::runApp(settings, levain::editor::withEditor(programOf(run, field), options));
    return run;
}

int expect(bool holds, std::string_view what)
{
    if (!holds)
    {
        std::println(stderr, "échec : {}", what);
    }
    return holds ? 0 : 1;
}

bool near(const glm::vec3& a, const glm::vec3& b, float epsilon = 1e-3f)
{
    return glm::length(a - b) <= epsilon;
}

constexpr std::string_view Idle = "0 mouse move 1 1\n";

/// Où sont les choses, relevé sur une exécution sans geste : le centre de la Vue, un point hors de
/// la Vue (la colonne de gauche, sous la fenêtre du champ de texte), et la ligne de la cible.
struct Layout
{
    glm::vec2 center{};
    glm::vec2 outside{};
    glm::vec2 targetRow{};
    levain::app::ScreenRect image{};
};

Layout layoutOf(const levain::tests::TestBackend& backend)
{
    const Run probe = play(backend, Idle, 12);
    if (probe.exitCode != 0 || probe.seen.size() != 12)
    {
        throw std::runtime_error{"la disposition : le relevé a échoué"};
    }
    const Seen& last = probe.seen.back();
    const auto& image = last.image;
    Layout layout{.center = {image.x + (image.width / 2.0f), image.y + (image.height / 2.0f)},
                  .outside = {image.x / 2.0f, image.y + (image.height * 0.9f)},
                  .targetRow = {},
                  .image = image};
    if (const auto row = last.rows.find(probe.target); row != last.rows.end())
    {
        layout.targetRow = {(row->second.min.x + row->second.max.x) / 2.0f,
                            (row->second.min.y + row->second.max.y) / 2.0f};
    }
    if (image.width < 100.0f || image.x < 40.0f || layout.targetRow == glm::vec2{0.0f})
    {
        throw std::runtime_error{"la disposition : la Vue ou la ligne de la cible est introuvable"};
    }
    return layout;
}

std::string at(glm::vec2 point)
{
    return std::format("{} {}", point.x, point.y);
}

/// Les images où l'état du vol (la capture) se lit : de l'appui, 8, à l'image du relâchement.
constexpr int PressFrame = 8;
constexpr int ReleaseFrame = 16;

/// Le vol : clic droit tenu dans la Vue (ou hors d'elle), la touche de vol des images 9 à 14. Le
/// pas fixe donne `speed * Dt` par image tenue ; six images tenues avancent de 0,8.
std::string flightScript(glm::vec2 mouse, std::string_view keyDown, std::string_view keyUp)
{
    return std::format("4 mouse move {}\n8 button down right\n9 {}\n15 {}\n16 button up right\n",
                       at(mouse), keyDown, keyUp);
}

int checkCaptureOnlyDuringFlight(const Run& run, int firstCaptured, int lastCaptured,
                                 std::string_view what)
{
    int failures = 0;
    for (std::size_t i = 0; i < run.seen.size(); ++i)
    {
        const int frame = static_cast<int>(i);
        const bool wanted = frame >= firstCaptured && frame <= lastCaptured;
        failures +=
            expect(run.seen.at(i).captured == wanted && run.seen.at(i).capturedOverPanels == wanted,
                   std::format("{} : image {}, la souris est capturée {}", what, frame,
                               wanted ? "pendant le vol" : "seulement pendant le vol"));
    }
    return failures;
}

/// En QWERTY et en AZERTY : clic droit tenu dans la Vue, la touche à la place du W avance.
int playFlight(const levain::tests::TestBackend& backend, const Layout& layout)
{
    int failures = 0;

    struct Case
    {
        const char* name;
        const char* down;
        const char* up;
        bool moves;
    };

    // La paire AZERTY : la touche à la place du W tape « z » ; la touche marquée W y est à la
    // place du Z. Par position, la première avance et la seconde non ; sur un QWERTY, W avance et Z
    // non. Une lecture par lettre inverserait l'AZERTY.
    constexpr std::array<Case, 4> Cases = {
        {{"QWERTY, W", "key down W", "key up W", true},
         {"AZERTY, Z à la place du W", "key down W as z", "key up W as z", true},
         {"AZERTY, W à la place du Z", "key down Z as w", "key up Z as w", false},
         {"QWERTY, Z", "key down Z", "key up Z", false}}};
    for (const Case& c : Cases)
    {
        const Run run = play(backend, flightScript(layout.center, c.down, c.up), 22);
        if (run.exitCode != 0 || run.seen.size() != 22)
        {
            failures += expect(false, std::format("{} : code 0 et une image par image", c.name));
            continue;
        }
        const EditorCamera start = run.seen.front().camera;
        const EditorCamera end = run.seen.back().camera;
        const glm::vec3 forward = levain::editor::viewDirectionOf(start);
        const glm::vec3 expected = start.position + (forward * (start.flySpeed * Dt * 6.0f));
        failures +=
            expect(c.moves ? near(end.position, expected) : near(end.position, start.position),
                   std::format("{} : la caméra {} de 6 images à la vitesse de vol", c.name,
                               c.moves ? "avance" : "n'avance pas"));
        failures +=
            expect(end.yawDegrees == start.yawDegrees && end.pitchDegrees == start.pitchDegrees,
                   std::format("{} : sans mouvement de souris, le regard est le même", c.name));
        failures += checkCaptureOnlyDuringFlight(run, PressFrame, ReleaseFrame - 1, c.name);
        failures += expect(run.seen.at(PressFrame).gesture == levain::editor::Gesture::Fly &&
                               run.seen.at(ReleaseFrame).gesture == levain::editor::Gesture::None,
                           std::format("{} : le vol dure de l'appui au relâchement", c.name));
        // Le menu du clic droit : pas à la fin d'un vol que la touche a mené ; une touche qui ne
        // vole pas laisse un simple clic, dont le menu s'ouvre.
        failures += expect(
            std::ranges::any_of(run.seen, [](const Seen& s) { return s.menuOpen; }) == !c.moves,
            std::format("{} : le menu {} à la fin du clic droit", c.name,
                        c.moves ? "ne s'ouvre pas, un vol l'a précédé,"
                                : "s'ouvre, rien n'a volé,"));
        failures +=
            expect(near(run.seen.at(8).camera.position, start.position) &&
                       near(run.seen.at(15).camera.position, c.moves ? expected : start.position),
                   std::format("{} : la caméra ne bouge qu'entre l'appui et le relâchement "
                               "de la touche",
                               c.name));
        // La caméra de l'App est celle de l'éditeur, chaque image.
        failures += expect(
            std::ranges::all_of(run.seen, [](const Seen& s) { return s.overrideMatchesCamera; }),
            std::format("{} : App::cameraOverride suit la caméra de l'éditeur", c.name));
    }
    return failures;
}

/// Le même geste commencé hors de la Vue (dans la hiérarchie) ne fait rien et ne capture pas.
int playOutside(const levain::tests::TestBackend& backend, const Layout& layout)
{
    const Run run = play(backend, flightScript(layout.outside, "key down W", "key up W"), 22);
    const EditorCamera start = run.seen.front().camera;
    return expect(run.exitCode == 0 && run.seen.size() == 22, "hors de la Vue : code 0") +
           expect(near(run.seen.back().camera.position, start.position) &&
                      run.seen.back().camera.yawDegrees == start.yawDegrees,
                  "hors de la Vue : la caméra ne bouge pas") +
           checkCaptureOnlyDuringFlight(run, 1000, 1000, "hors de la Vue (jamais capturée)") +
           expect(std::ranges::none_of(run.seen, [](const Seen& s) { return s.menuOpen; }),
                  "hors de la Vue : aucun menu de la Vue");
}

/// Un clic droit pressé hors de la Vue (dans la hiérarchie) et relâché dans la Vue n'ouvre pas son
/// menu : il n'est pas parti d'elle, ImGui y voit pourtant un relâchement.
int playPressedOutside(const levain::tests::TestBackend& backend, const Layout& layout)
{
    const Run run = play(backend,
                         std::format("4 mouse move {}\n8 button down right\n10 mouse move {}\n"
                                     "12 button up right\n",
                                     at(layout.outside), at(layout.center)),
                         16);
    return expect(run.exitCode == 0 && run.seen.size() == 16, "pressé hors de la Vue : code 0") +
           expect(std::ranges::none_of(run.seen, [](const Seen& s) { return s.menuOpen; }),
                  "un clic droit pressé hors de la Vue, relâché dans la Vue, n'ouvre pas le menu") +
           checkCaptureOnlyDuringFlight(run, 1000, 1000, "pressé hors de la Vue");
}

/// Le menu du clic droit : il s'ouvre à un clic sans vol, pas à la fin d'un vol (même relâchement,
/// vu d'ImGui comme un clic), et la souris n'est capturée que le temps du clic.
int playMenu(const levain::tests::TestBackend& backend, const Layout& layout)
{
    const Run click = play(
        backend,
        std::format("4 mouse move {}\n8 button down right\n9 button up right\n", at(layout.center)),
        14);
    const Run moved = play(backend,
                           std::format("4 mouse move {}\n8 button down right\n9 mouse motion 12 5\n"
                                       "10 button up right\n",
                                       at(layout.center)),
                           14);
    int failures = expect(click.exitCode == 0 && moved.exitCode == 0, "le menu : code 0");
    failures += expect(std::ranges::any_of(click.seen, [](const Seen& s) { return s.menuOpen; }),
                       "un clic droit sans vol ouvre le menu de la Vue");
    failures +=
        expect(std::ranges::none_of(moved.seen, [](const Seen& s) { return s.menuOpen; }),
               "la fin d'un vol, relâchement vu comme un clic par ImGui, n'ouvre pas le menu");
    failures += expect(moved.seen.at(9).camera.yawDegrees != moved.seen.at(8).camera.yawDegrees,
                       "le mouvement relatif du vol tourne le regard");
    failures += checkCaptureOnlyDuringFlight(click, PressFrame, PressFrame, "le clic sans vol");
    return failures;
}

/// F cadre la sélection quand la Vue est survolée ; sans survol ni focus, rien. Le double-clic sur
/// une ligne de la hiérarchie la choisit et la cadre.
int playFraming(const levain::tests::TestBackend& backend, const Layout& layout)
{
    int failures = 0;
    const Run framed =
        play(backend, std::format("4 mouse move {}\n8 key down F\n9 key up F\n", at(layout.center)),
             14, false, "cible");
    const EditorCamera start = framed.seen.front().camera;
    const EditorCamera end = framed.seen.back().camera;
    failures += expect(framed.exitCode == 0 && framed.seen.back().selected == framed.target,
                       "F : la cible est la sélection (--select)");
    failures += expect(!framed.seen.at(7).targetInView,
                       "avant F, la cible n'est pas dans l'image (le test peut échouer)");
    failures +=
        expect(framed.seen.back().targetInView, "après F, la boîte de la cible tient dans l'image");
    failures +=
        expect(end.yawDegrees == start.yawDegrees && end.pitchDegrees == start.pitchDegrees &&
                   !near(end.position, start.position),
               "F place l'œil sans changer le regard");

    // F ailleurs : un clic dans la hiérarchie (sa zone vide) lui donne le focus, la souris y reste.
    const Run elsewhere = play(backend,
                               std::format("4 mouse move {}\n5 button down left\n6 button up left\n"
                                           "8 key down F\n9 key up F\n",
                                           at(layout.outside)),
                               14, false, "cible");
    failures += expect(elsewhere.exitCode == 0 &&
                           near(elsewhere.seen.back().camera.position, start.position),
                       "F, la Vue ni survolée ni focalisée : la caméra ne bouge pas");

    // Le menu du clic droit cadre aussi.
    const Run menu = play(
        backend,
        std::format("4 mouse move {}\n8 button down right\n9 button up right\n", at(layout.center)),
        14, false, "cible");
    failures += expect(menu.seen.back().menuOpen,
                       "le menu est ouvert (précondition du cadrage par le menu)");

    // Le double-clic dans la hiérarchie : « autre » est choisie au départ, la ligne de « cible »
    // reçoit deux clics.
    const Run dbl = play(backend,
                         std::format("4 mouse move {}\n8 button down left\n9 button up left\n"
                                     "10 button down left\n11 button up left\n",
                                     at(layout.targetRow)),
                         16, false, "autre");
    failures += expect(dbl.exitCode == 0 && dbl.seen.back().selected == dbl.target,
                       "le double-clic choisit la ligne");
    failures += expect(dbl.seen.back().targetInView && !dbl.seen.at(7).targetInView,
                       "le double-clic dans la hiérarchie cadre l'entité");
    const Run single = play(backend,
                            std::format("4 mouse move {}\n8 button down left\n9 button up left\n",
                                        at(layout.targetRow)),
                            16, false, "autre");
    failures +=
        expect(single.seen.back().selected == single.target &&
                   near(single.seen.back().camera.position, single.seen.front().camera.position),
               "un seul clic choisit la ligne sans cadrer");
    return failures;
}

/// Un champ de texte actif ne l'est plus quand le vol commence : Retour arrière et les flèches ne
/// l'éditent pas pendant qu'on vole.
int playActiveField(const levain::tests::TestBackend& backend, const Layout& layout)
{
    const Run run = play(backend, flightScript(layout.center, "key down W", "key up W"), 22, true);
    int failures = expect(run.exitCode == 0 && run.seen.size() == 22, "le champ : code 0");
    failures += expect(run.seen.at(7).activeId != 0, "avant le vol, le champ de texte est actif");
    failures += expect(run.seen.at(PressFrame).activeId == 0 && run.seen.at(9).activeId == 0,
                       "le vol désactive le champ de texte actif");
    return failures;
}

/// Un clic droit glissé alors que le menu de la Vue est ouvert vole : le menu est bloquant pour
/// ImGui, qui ne dit plus la Vue survolée (`IsItemHovered` sans drapeau), et le geste ne commençait
/// pas ; le menu se rouvrait au relâchement. Le menu ne rouvre pas non plus à la fin du vol.
int playFlightWithMenuOpen(const levain::tests::TestBackend& backend, const Layout& layout)
{
    // Le menu naît sous le curseur, vers le bas et la droite : l'appui suivant est à gauche.
    const glm::vec2 away = layout.center - glm::vec2{120.0f, 0.0f};
    const Run run = play(backend,
                         std::format("4 mouse move {}\n8 button down right\n9 button up right\n"
                                     "10 mouse move {}\n12 button down right\n"
                                     "13 mouse motion 40 0\n15 button up right\n",
                                     at(layout.center), at(away)),
                         20);
    int failures = expect(run.exitCode == 0 && run.seen.size() == 20, "menu ouvert : code 0");
    failures += expect(run.seen.at(11).menuOpen, "le menu est ouvert avant le glissé");
    failures +=
        expect(run.seen.at(13).gesture == levain::editor::Gesture::Fly && run.seen.at(13).captured,
               "un clic droit glissé, menu ouvert, vole et capture");
    failures += expect(run.seen.at(16).camera.yawDegrees != run.seen.at(12).camera.yawDegrees,
                       "le glissé tourne le regard");
    failures += expect(!run.seen.at(14).menuOpen && !run.seen.at(19).menuOpen,
                       "le vol ferme le menu, qui ne se rouvre pas au relâchement");
    return failures;
}

/// Après un vol, le curseur revient au point où le clic droit a été pressé : l'éditeur le donne à
/// l'App au début du vol (`App::mouseReturnTo`), qui le consomme en quittant la capture
/// (`platform::warpMouse` avant le mode relatif). Sans cela SDL le replace décalé de tout le
/// glissé, peut-être sur un panneau. **Le banc ne voit pas le warp de SDL** : le pilote hors écran
/// n'a pas de focus de souris, `SDL_WarpMouseInWindow` n'y fait rien et `SDL_GetMouseState` reste
/// à (0, 0). Le test épingle donc ce que l'App fait avant l'appel : le point est posé, est celui de
/// l'appui, et est consommé exactement à la fin de la capture.
int playCursorReturn(const levain::tests::TestBackend& backend, const Layout& layout)
{
    const Run run = play(backend,
                         std::format("4 mouse move {}\n8 button down right\n9 mouse motion 90 40\n"
                                     "12 button up right\n",
                                     at(layout.center)),
                         18);
    int failures = expect(run.exitCode == 0 && run.seen.size() == 18, "curseur : code 0");
    const auto at8 = run.seen.at(PressFrame).returnTo;
    failures += expect(!run.seen.at(7).returnTo, "avant le vol, aucun point de retour");
    failures += expect(at8 && glm::length(glm::vec2{at8->x, at8->y} - layout.center) <= 0.5f,
                       "au début du vol, le point de retour est celui de l'appui");
    const auto same = [&](const Seen& s)
    { return s.returnTo && at8 && s.returnTo->x == at8->x && s.returnTo->y == at8->y; };
    failures += expect(same(run.seen.at(10)) && same(run.seen.at(11)),
                       "il tient pendant tout le vol, malgré le glissé");
    failures += expect(run.seen.at(11).captured && !run.seen.at(12).captured &&
                           !run.seen.at(12).returnTo && !run.seen.back().returnTo,
                       "à la fin de la capture, l'App l'a consommé (le curseur a été remis)");
    return failures;
}

/// En jeu, les gestes de l'éditeur ne font rien : la caméra du jeu voit, l'éditeur n'est pas
/// touché.
int playPlay(const levain::tests::TestBackend& backend, const Layout& layout)
{
    const Run run =
        play(backend,
             std::format("4 mouse move {}\n6 key down Left Alt\n6 key down P\n7 key up P\n"
                         "7 key up Left Alt\n10 button down right\n11 key down W\n"
                         "14 key up W\n15 button up right\n",
                         at(layout.center)),
             20);
    int failures = expect(run.exitCode == 0 && run.seen.size() == 20, "jouer : code 0");
    failures += expect(std::ranges::none_of(run.seen, [](const Seen& s) { return s.menuOpen; }),
                       "en jeu, le relâchement du clic droit dans la Vue n'ouvre pas son menu");
    const Seen& last = run.seen.back();
    failures += expect(last.playing, "jouer : Alt+P a joué");
    failures += expect(!last.overridePresent,
                       "en jeu, la caméra de l'éditeur n'est plus imposée : celle du jeu voit");
    failures += expect(near(last.camera.position, run.seen.at(2).camera.position),
                       "en jeu, les gestes ne bougent pas la caméra de l'éditeur");
    failures += expect(std::ranges::none_of(run.seen, [](const Seen& s) { return s.captured; }),
                       "en jeu, ce clic droit n'est pas un vol : la souris n'est pas capturée");

    // Le clic droit dans la Vue, en jeu, après un clic dans l'inspecteur (la route passe à *UI* :
    // ImGui voit alors le clic) : pas de menu « Cadrer la sélection » sur le jeu.
    const glm::vec2 inspector{layout.image.x + layout.image.width + 60.0f, layout.image.y + 100.0f};
    const Run uiRoute = play(backend,
                             std::format("4 mouse move {}\n6 key down Left Alt\n6 key down P\n"
                                         "7 key up P\n7 key up Left Alt\n"
                                         "9 mouse move {}\n10 button down left\n"
                                         "11 button up left\n13 mouse move {}\n"
                                         "14 button down right\n15 button up right\n",
                                         at(layout.center), at(inspector), at(layout.center)),
                             20);
    failures += expect(uiRoute.exitCode == 0 && uiRoute.seen.at(15).playing,
                       "jouer, clic dans l'inspecteur : code 0, toujours en jeu");
    failures += expect(std::ranges::none_of(uiRoute.seen, [](const Seen& s) { return s.menuOpen; }),
                       "en jeu, même route *UI*, le clic droit dans la Vue n'ouvre pas son menu");

    // Le menu ouvert en Édition ne reste pas ouvert sur le jeu (Alt+P).
    const Run opened = play(backend,
                            std::format("4 mouse move {}\n8 button down right\n9 button up right\n"
                                        "11 key down Left Alt\n11 key down P\n12 key up P\n"
                                        "12 key up Left Alt\n",
                                        at(layout.center)),
                            18);
    failures +=
        expect(opened.exitCode == 0 && opened.seen.at(10).menuOpen && opened.seen.at(14).playing &&
                   !opened.seen.at(14).menuOpen && !opened.seen.back().menuOpen,
               "le menu ouvert en Édition se ferme en passant en jeu (Alt+P)");

    // Un double-clic sur une ligne de la hiérarchie, en jeu, ne cadre pas : au retour en Édition
    // (Échap), la caméra est celle d'avant.
    const Run dbl = play(backend,
                         std::format("4 mouse move {}\n6 key down Left Alt\n6 key down P\n"
                                     "7 key up P\n7 key up Left Alt\n"
                                     "10 mouse move {}\n11 button down left\n12 button up left\n"
                                     "13 button down left\n14 button up left\n"
                                     "17 key down Escape\n18 key up Escape\n",
                                     at(layout.center), at(layout.targetRow)),
                         24, false, "autre");
    failures += expect(dbl.exitCode == 0 && dbl.seen.size() == 24, "jouer, double-clic : code 0");
    failures += expect(dbl.seen.at(15).playing && !dbl.seen.back().playing,
                       "jouer, double-clic : en jeu à l'image 15, de retour en Édition à la fin");
    failures += expect(dbl.seen.back().selected == dbl.target,
                       "jouer, double-clic : la ligne est choisie (précondition du cadrage)");
    failures += expect(
        std::ranges::all_of(dbl.seen, [&](const Seen& s)
                            { return near(s.camera.position, dbl.seen.front().camera.position); }),
        "jouer, double-clic dans la hiérarchie : la caméra de l'éditeur ne bouge pas");
    return failures;
}

} // namespace

int main(int argc, char** argv)
{
    try
    {
        const std::span arguments{argv, static_cast<std::size_t>(argc)};
        const auto backend =
            levain::tests::testBackendNamed(arguments.size() == 2 ? arguments[1] : "vulkan");
        if (arguments.size() > 2 || !backend)
        {
            std::println(stderr, "usage : levain_editor_gestures [vulkan|d3d12|d3d12-warp]");
            return 2;
        }
        const Layout layout = layoutOf(*backend);
        const int failures = playFlight(*backend, layout) + playOutside(*backend, layout) +
                             playPressedOutside(*backend, layout) + playMenu(*backend, layout) +
                             playFraming(*backend, layout) + playActiveField(*backend, layout) +
                             playPlay(*backend, layout) + playFlightWithMenuOpen(*backend, layout) +
                             playCursorReturn(*backend, layout);
        return failures == 0 ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        std::fputs(e.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
}
