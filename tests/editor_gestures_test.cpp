// Les gestes de la caméra de l'éditeur, les boîtes qu'elle cadre et ses plans de découpe (ADR-0036,
// décisions 6, 8 et 13, morceau 11) : de la logique pure, sans fenêtre. Chaque règle a son cas (les
// contre-tests sont listés dans la PR du morceau).

#include <cmath>
#include <cstdint>
#include <map>

#include <doctest/doctest.h>
#include <flecs.h>
#include <glm/glm.hpp>

#include "levain/editor/camera.hpp"
#include "levain/editor/gestures.hpp"
#include "levain/editor/scene_bounds.hpp"
#include "levain/editor/shortcuts.hpp"
#include "levain/platform/input_script.hpp"
#include "levain/platform/window.hpp"
#include "levain/scene/components.hpp"

using levain::editor::capturesMouse;
using levain::editor::EditorCamera;
using levain::editor::Gesture;
using levain::editor::GestureContext;
using levain::editor::GestureFrame;
using levain::editor::GestureState;
using levain::editor::GestureStep;
using levain::editor::stepGestures;
using levain::platform::Events;
using levain::platform::UiEvent;
using levain::platform::UiEventType;

namespace
{

constexpr std::uint16_t ScanW = 26, ScanZ = 29, ScanS = 22, ScanA = 4, ScanD = 7, ScanE = 8,
                        ScanQ = 20;
constexpr float Dt = 1.0f / 60.0f;

const levain::editor::ShortcutTable& table()
{
    static const auto shortcuts = levain::editor::defaultShortcuts();
    return shortcuts;
}

/// Une scène de 800 × 600 en (100, 50) : ce que la Vue montre.
GestureContext sceneContext()
{
    return {.region = {.x = 100.0f, .y = 50.0f, .width = 800.0f, .height = 600.0f},
            .sceneHovered = true};
}

UiEvent moved(float x, float y)
{
    return {.type = UiEventType::MouseMoved, .x = x, .y = y};
}

UiEvent button(std::uint8_t which, bool down)
{
    return {.type = UiEventType::MouseButton, .button = which, .down = down};
}

UiEvent key(std::uint16_t scancode, bool down, levain::platform::KeyModifiers modifiers = {})
{
    return {.type = UiEventType::Key,
            .keycode = levain::platform::qwertyKeyAtPosition(scancode),
            .scancode = scancode,
            .down = down,
            .modifiers = modifiers};
}

Events eventsOf(std::initializer_list<UiEvent> ui, glm::vec2 motion = {0.0f, 0.0f})
{
    Events events;
    events.ui = ui;
    if (motion != glm::vec2{0.0f})
    {
        events.input.push_back({.type = levain::platform::InputEventType::AxisMotion,
                                .device = levain::platform::InputDevice::Mouse,
                                .code = 0,
                                .value = motion.x});
        events.input.push_back({.type = levain::platform::InputEventType::AxisMotion,
                                .device = levain::platform::InputDevice::Mouse,
                                .code = 1,
                                .value = motion.y});
    }
    return events;
}

GestureStep step(const GestureState& state, const Events& events,
                 const GestureContext& context = sceneContext())
{
    return stepGestures(state, events, context, table(), Dt);
}

/// Un vol commencé au centre de la scène.
GestureState flying()
{
    const GestureStep started =
        step({}, eventsOf({moved(500.0f, 350.0f), button(levain::editor::RightButton, true)}));
    return started.state;
}

EditorCamera tilted()
{
    return {.position = {10.0f, 3.0f, -7.0f},
            .yawDegrees = 35.0f,
            .pitchDegrees = 20.0f,
            .pivotDistance = 40.0f,
            .flySpeed = 8.0f};
}

} // namespace

TEST_CASE("un bouton commence le geste de la décision 6, selon Alt")
{
    using levain::editor::gestureStartedBy;
    CHECK(gestureStartedBy(levain::editor::RightButton, false) == Gesture::Fly);
    CHECK(gestureStartedBy(levain::editor::RightButton, true) == Gesture::DollyDrag);
    CHECK(gestureStartedBy(levain::editor::LeftButton, true) == Gesture::Orbit);
    CHECK(gestureStartedBy(levain::editor::MiddleButton, false) == Gesture::Pan);
    CHECK(gestureStartedBy(levain::editor::MiddleButton, true) == Gesture::Pan);
    // Le clic gauche seul choisit (M7.4), il ne bouge pas la caméra.
    CHECK(gestureStartedBy(levain::editor::LeftButton, false) == Gesture::None);
    CHECK(gestureStartedBy(9, false) == Gesture::None);
}

TEST_CASE("les touches de vol se lisent par position : W, A, S, D, E, Q")
{
    using levain::editor::flightMoveOf;
    std::bitset<levain::platform::KeyCodeCount> keys;
    CHECK(flightMoveOf(table(), keys) == glm::vec3{0.0f});
    keys.set(ScanW);
    CHECK(flightMoveOf(table(), keys) == glm::vec3{0.0f, 0.0f, 1.0f});
    keys.set(ScanD);
    keys.set(ScanE);
    CHECK(flightMoveOf(table(), keys) == glm::vec3{1.0f, 1.0f, 1.0f});
    keys.reset();
    keys.set(ScanS);
    keys.set(ScanA);
    keys.set(ScanQ);
    CHECK(flightMoveOf(table(), keys) == glm::vec3{-1.0f, -1.0f, -1.0f});
    // La touche marquée W d'un AZERTY est à la place du Z d'un QWERTY : elle ne vole pas.
    keys.reset();
    keys.set(ScanZ);
    CHECK(flightMoveOf(table(), keys) == glm::vec3{0.0f});
}

TEST_CASE("un geste ne commence que dans la scène, sous la souris : ailleurs il ne fait rien")
{
    const auto press = eventsOf({moved(500.0f, 350.0f), button(levain::editor::RightButton, true)});
    CHECK(step({}, press).state.active == Gesture::Fly);
    CHECK(capturesMouse(step({}, press).state));

    // Hors du rectangle de la Vue (dans la colonne de gauche).
    const auto outside =
        eventsOf({moved(40.0f, 350.0f), button(levain::editor::RightButton, true)});
    CHECK(step({}, outside).state.active == Gesture::None);
    // Dans le rectangle, mais une autre fenêtre est dessus : pas sous la souris.
    GestureContext covered = sceneContext();
    covered.sceneHovered = false;
    CHECK(step({}, press, covered).state.active == Gesture::None);
    // Sans position connue, l'appui n'est nulle part.
    CHECK(step({}, eventsOf({button(levain::editor::RightButton, true)})).state.active ==
          Gesture::None);
    // Le bord haut gauche en est, le bas droit non, comme un pixel.
    CHECK(step({}, eventsOf({moved(100.0f, 50.0f), button(levain::editor::MiddleButton, true)}))
              .state.active == Gesture::Pan);
    CHECK(step({}, eventsOf({moved(900.0f, 650.0f), button(levain::editor::MiddleButton, true)}))
              .state.active == Gesture::None);
}

TEST_CASE("un geste commencé hors de la Vue ne se continue pas quand la souris y entre")
{
    GestureState state =
        step({}, eventsOf({moved(40.0f, 350.0f), button(levain::editor::RightButton, true)})).state;
    CHECK(state.active == Gesture::None);
    // Le bouton est toujours tenu : la souris entre dans la Vue en bougeant, et la touche de vol
    // est tenue. Rien ne vole.
    const GestureStep next =
        step(state, eventsOf({moved(500.0f, 350.0f), key(ScanW, true)}, {30.0f, 10.0f}));
    CHECK(next.frame.gesture == Gesture::None);
    CHECK(next.frame.motion == glm::vec2{0.0f});
    CHECK(next.frame.move == glm::vec3{0.0f});
}

TEST_CASE("le mouvement de l'image du relâchement revient encore au geste, puis il est fini")
{
    const GestureState state = flying();
    const GestureStep last =
        step(state, eventsOf({button(levain::editor::RightButton, false)}, {12.0f, 0.0f}));
    CHECK(last.frame.gesture == Gesture::Fly);
    CHECK(last.frame.motion.x == 12.0f);
    CHECK(last.state.active == Gesture::None);
    CHECK_FALSE(capturesMouse(last.state));
    // Un autre bouton relâché ne finit pas le geste.
    CHECK(step(state, eventsOf({button(levain::editor::LeftButton, false)})).state.active ==
          Gesture::Fly);
}

TEST_CASE("la souris capturée n'a pas de position : le curseur ne suit pas pendant le vol")
{
    const GestureState state = flying();
    CHECK(step(state, eventsOf({moved(2000.0f, -300.0f)})).state.cursor ==
          glm::vec2{500.0f, 350.0f});
}

TEST_CASE("la fenêtre qui perd le focus lâche le geste et les touches : leurs relâchements ne "
          "viendront pas")
{
    GestureState state = step(flying(), eventsOf({key(ScanW, true)})).state;
    CHECK(state.keys.test(ScanW));
    state = step(state, eventsOf({UiEvent{.type = UiEventType::FocusLost}})).state;
    CHECK(state.active == Gesture::None);
    CHECK(state.keys.none());
    CHECK(state.cursorKnown); // le curseur reste situé
}

TEST_CASE("les boîtes : celle d'une entité, celle de la scène, les plans de découpe")
{
    using levain::scene::WorldTransform;
    flecs::world world;
    const std::map<levain::assets::AssetId, levain::app::ModelGpu> noModels;
    const levain::render::Instances instance;

    // Sans matrice monde : le cube unité à l'origine.
    const flecs::entity bare = world.entity("nu");
    auto box = levain::editor::entityBoxOf(bare, noModels, instance);
    CHECK(box.min == glm::vec3{-0.5f});
    CHECK(box.max == glm::vec3{0.5f});

    // Placée, mise à l'échelle : le cube unité passé par sa matrice.
    glm::mat4 matrix = glm::translate(glm::mat4{1.0f}, glm::vec3{20.0f, 0.0f, -30.0f});
    matrix = glm::scale(matrix, glm::vec3{4.0f});
    const flecs::entity target = world.entity("cible").set(WorldTransform{matrix});
    box = levain::editor::entityBoxOf(target, noModels, instance);
    CHECK(box.min == glm::vec3{18.0f, -2.0f, -32.0f});
    CHECK(box.max == glm::vec3{22.0f, 2.0f, -28.0f});

    // La scène : l'union de toutes ; vide, le cube unité.
    const auto everything = world.query<const WorldTransform>();
    world.entity("loin").set(WorldTransform{glm::translate(glm::mat4{1.0f}, glm::vec3{200.0f})});
    const auto scene = levain::editor::sceneBoxOf(everything, noModels, instance);
    CHECK(scene.min == glm::vec3{18.0f, -2.0f, -32.0f}); // l'entité nue, sans matrice, n'y est pas
    CHECK(scene.max == glm::vec3{200.5f});
}

TEST_CASE("les plans de découpe de l'éditeur : le lointain d'abord, le proche du seul rapport")
{
    const EditorCamera camera = tilted();
    // Un petit objet à 50 m devant l'œil : son lointain seul serait de 60 m, pas le sol derrière.
    const glm::vec3 center = camera.position + (50.0f * levain::editor::viewDirectionOf(camera));
    const levain::render::Box small{.min = center - glm::vec3{1.0f},
                                    .max = center + glm::vec3{1.0f}};
    CHECK(levain::editor::clipPlanesFor(camera, small).farPlane < levain::editor::DefaultFarPlane);
    const auto planes = levain::editor::editorClipPlanes(camera, small);
    CHECK(planes.farPlane >= levain::editor::DefaultFarPlane);
    // Le proche ne suit pas l'entité (à 49 m) : le sol à 5 m devant l'œil resterait rogné.
    CHECK(planes.nearPlane <= levain::editor::DefaultNearPlane);
    // Œil dans une boîte de 10 m (le départ : la caméra de l'éditeur est celle d'une entité).
    const levain::render::Box around{.min = camera.position - glm::vec3{5.0f},
                                     .max = camera.position + glm::vec3{5.0f}};
    const auto inside = levain::editor::editorClipPlanes(camera, around);
    CHECK(inside.farPlane >= levain::editor::DefaultFarPlane);
    CHECK(inside.nearPlane <= levain::editor::DefaultNearPlane);
    // Œil à 100 m de la boîte : le proche ne monte pas à 90 m.
    const glm::vec3 far100 = camera.position + (100.0f * levain::editor::viewDirectionOf(camera));
    const levain::render::Box distant{.min = far100 - glm::vec3{5.0f},
                                      .max = far100 + glm::vec3{5.0f}};
    CHECK(levain::editor::editorClipPlanes(camera, distant).nearPlane <=
          levain::editor::DefaultNearPlane);
    // Une scène plus profonde que le défaut : le lointain la contient, le proche suit le rapport.
    const levain::render::Box valley{.min = glm::vec3{-3000.0f, 0.0f, -3000.0f},
                                     .max = glm::vec3{3000.0f, 40.0f, 3000.0f}};
    const auto deep = levain::editor::editorClipPlanes(camera, valley);
    CHECK(deep.farPlane >= levain::editor::clipPlanesFor(camera, valley).farPlane);
    CHECK(deep.farPlane / deep.nearPlane <= levain::editor::MaxClipRatio * 1.0001f);
    // Le plafond : une scène de 1e7 m ne pousse pas le proche au-delà de 10 m.
    const levain::render::Box huge{.min = glm::vec3{-1.0e7f}, .max = glm::vec3{1.0e7f}};
    const auto capped = levain::editor::editorClipPlanes(camera, huge);
    CHECK(capped.farPlane == levain::editor::MaxEditorFarPlane);
    CHECK(capped.nearPlane <= 10.0f);
}

TEST_CASE("la durée d'un vol est bornée : une image longue ne lance pas la caméra")
{
    using levain::editor::clampFlightSeconds;
    using levain::editor::MaxFlightSeconds;
    CHECK(clampFlightSeconds(Dt) == Dt);
    CHECK(clampFlightSeconds(2.0f) == MaxFlightSeconds);
    CHECK(clampFlightSeconds(-1.0f) == 0.0f);
    CHECK(clampFlightSeconds(std::nanf("")) == 0.0f);
    // Dans le pas : une image de deux secondes compte `MaxFlightSeconds`.
    const GestureStep slow = stepGestures({}, eventsOf({}), sceneContext(), table(), 2.0f);
    CHECK(slow.frame.seconds == MaxFlightSeconds);
}

TEST_CASE("une fenêtre cachée remet les gestes au repos : les relâchements ne viendront pas")
{
    GestureState state = flying();
    state = step(state, eventsOf({key(ScanW, true)})).state;
    REQUIRE(state.active == Gesture::Fly);
    REQUIRE(state.keys.test(ScanW));
    Events hidden = eventsOf({}, {5.0f, 0.0f});
    hidden.window.push_back({.type = levain::platform::WindowEventType::Hidden});
    const GestureStep next = step(state, hidden);
    CHECK(next.state.active == Gesture::None);
    CHECK_FALSE(next.state.keys.test(ScanW));
    CHECK_FALSE(capturesMouse(next.state));
    // Le curseur reste connu : le prochain appui est situé.
    CHECK(next.state.cursorKnown);
    // Visible, la fenêtre ne remet rien (la molette hors vol zoome, elle ne tient pas d'état).
    Events shown = eventsOf({});
    shown.window.push_back({.type = levain::platform::WindowEventType::Shown});
    CHECK(step(state, shown).state.active == Gesture::Fly);
}

TEST_CASE("la boîte de la scène se garde : refaite quand quelque chose a changé, sinon non")
{
    using levain::editor::SceneBoundsCache;
    using levain::editor::SceneBoundsFacts;
    using levain::editor::sceneBoundsStale;
    SceneBoundsCache cache;
    const SceneBoundsFacts facts{.entityCount = 10, .modelCount = 2, .edited = false};
    CHECK(sceneBoundsStale(cache, facts)); // jamais calculée
    cache = {.box = {}, .valid = true, .frameAge = 3, .entityCount = 10, .modelCount = 2};
    CHECK_FALSE(sceneBoundsStale(cache, facts));
    CHECK(sceneBoundsStale(cache, {.entityCount = 11, .modelCount = 2, .edited = false}));
    CHECK(sceneBoundsStale(cache, {.entityCount = 10, .modelCount = 3, .edited = false}));
    CHECK(sceneBoundsStale(cache, {.entityCount = 10, .modelCount = 2, .edited = true}));
    cache.frameAge = levain::editor::SceneBoundsRefreshFrames;
    CHECK(sceneBoundsStale(cache, facts)); // trop ancienne

    // Dans la fonction : un deuxième appel sans changement ne parcourt pas le monde.
    using levain::scene::WorldTransform;
    flecs::world world;
    const std::map<levain::assets::AssetId, levain::app::ModelGpu> noModels;
    const levain::render::Instances instance;
    const auto everything = world.query<const WorldTransform>();
    world.entity("a").set(WorldTransform{glm::mat4{1.0f}});
    SceneBoundsCache kept;
    const auto first = levain::editor::cachedSceneBoxOf(
        kept, {.entityCount = 1, .modelCount = 0, .edited = false}, everything, noModels, instance);
    world.entity("b").set(WorldTransform{glm::translate(glm::mat4{1.0f}, glm::vec3{50.0f})});
    const auto second = levain::editor::cachedSceneBoxOf(
        kept, {.entityCount = 1, .modelCount = 0, .edited = false}, everything, noModels, instance);
    CHECK(second.max == first.max); // le cache : « b » n'y est pas encore
    const auto third = levain::editor::cachedSceneBoxOf(
        kept, {.entityCount = 2, .modelCount = 0, .edited = false}, everything, noModels, instance);
    CHECK(third.max == glm::vec3{50.5f}); // le compte a changé : refaite
}

TEST_CASE("on ne cadre qu'une entité placée : sans matrice monde, F ne cadre rien")
{
    flecs::world world;
    CHECK_FALSE(levain::editor::canFrameEntity(flecs::entity{}));
    CHECK_FALSE(levain::editor::canFrameEntity(world.entity("nue")));
    CHECK(levain::editor::canFrameEntity(
        world.entity("placée").set(levain::scene::WorldTransform{glm::mat4{1.0f}})));
}
