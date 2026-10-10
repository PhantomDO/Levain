#include <algorithm>
#include <vector>

#include <doctest/doctest.h>
#include <imgui.h>
// Un nœud d'ancrage et sa fenêtre : l'API interne d'ImGui, comme DockBuilder.
#include <imgui_internal.h>

#include "panels.hpp"

#include "levain/app/ui_layer.hpp"
#include "levain/ui/context.hpp"

using levain::platform::InputDevice;
using levain::platform::InputEvent;
using levain::platform::InputEventType;

namespace
{

InputEvent key(std::uint16_t code, bool down)
{
    return {.type = down ? InputEventType::ButtonDown : InputEventType::ButtonUp,
            .device = InputDevice::Keyboard,
            .code = code};
}

bool contains(const std::vector<InputEvent>& events, const InputEvent& wanted)
{
    return std::ranges::any_of(events,
                               [&](const InputEvent& event)
                               {
                                   return event.type == wanted.type &&
                                          event.device == wanted.device &&
                                          event.code == wanted.code;
                               });
}

} // namespace

TEST_CASE("quand l'UI prend le clavier, le jeu ne voit pas les appuis, mais voit les relâchements")
{
    // W (scancode 26) est tenu depuis l'image d'avant ; on clique dans un champ de texte, puis on
    // tape 7 (scancode 36) et on relâche W.
    levain::input::RawInput held;
    held.keys.set(26);
    const std::vector<InputEvent> events{key(36, true), key(26, false)};
    const auto game = levain::app::gameInputOf(events, held, {.keyboard = true});
    CHECK_FALSE(contains(game, key(36, true))); // taper un nombre ne fait pas marcher le renard
    CHECK(contains(game, key(26, false)));      // jamais un relâchement perdu
    // Sans clavier pris, tout passe.
    CHECK(levain::app::gameInputOf(events, held, {}).size() == 2);
}

TEST_CASE("quand l'UI prend un appareil, ce qui y était tenu est relâché pour le jeu")
{
    levain::input::RawInput held;
    held.keys.set(26);
    held.mouseButtons.set(1);
    const auto keyboard = levain::app::gameInputOf({}, held, {.keyboard = true});
    CHECK(contains(keyboard, key(26, false)));
    CHECK(keyboard.size() == 1); // la souris n'est pas prise : son bouton reste tenu
    const auto mouse = levain::app::gameInputOf({}, held, {.mouse = true});
    CHECK(contains(mouse,
                   {.type = InputEventType::ButtonUp, .device = InputDevice::Mouse, .code = 1}));
    // Un mouvement de souris pris par l'UI ne tourne pas la caméra.
    const std::vector<InputEvent> motion{{.type = InputEventType::AxisMotion,
                                          .device = InputDevice::Mouse,
                                          .code = 0,
                                          .value = 12.0f}};
    CHECK(levain::app::gameInputOf(motion, {}, {.mouse = true}).empty());
}

TEST_CASE("F1 est la touche de l'UI : le jeu ne la voit jamais appuyée")
{
    const std::vector<InputEvent> events{key(levain::app::PanelsKey, true)};
    CHECK(levain::app::gameInputOf(events, {}, {}).empty());
}

TEST_CASE("la souris n'est capturée que si le programme la veut et que les panneaux sont fermés")
{
    CHECK(levain::app::mouseShouldBeCaptured(true, false));
    CHECK_FALSE(levain::app::mouseShouldBeCaptured(true, true));
    CHECK_FALSE(levain::app::mouseShouldBeCaptured(false, false));
}

TEST_CASE("le coût de l'UI garde sa moyenne et son pire")
{
    levain::app::UiCost cost;
    levain::app::recordUiCpu(cost, 0.2);
    levain::app::recordUiCpu(cost, 0.6);
    CHECK(cost.cpuSamples == 2);
    CHECK(cost.cpuTotalMs / cost.cpuSamples == doctest::Approx(0.4));
    CHECK(cost.cpuMaxMs == doctest::Approx(0.6));
}

TEST_CASE("une image sans mesure GPU garde la précédente dans la courbe, au lieu d'un 0")
{
    levain::app::FrameHistory history;
    levain::app::recordHistory(history, 16.0f, 2.5f);
    levain::app::recordHistory(history, 17.0f, std::nullopt);
    CHECK(history.frameMs.at(1) == 17.0f);
    CHECK(history.gpuMs.at(1) == 2.5f);
    CHECK(history.next == 2);
}

TEST_CASE("la route décide ce que l'UI prend : l'éditeur tout, le jeu rien du clavier, la souris "
          "hors de la scène")
{
    using levain::app::devicesTakenBy;
    using levain::app::InputRoute;
    constexpr levain::app::RouteFacts Wanted{.imguiWantsMouse = true, .imguiWantsKeyboard = true};
    // Ui : ce que veut ImGui, comme avant M7.7 (le sandbox et Rando gardent cette route).
    CHECK(devicesTakenBy(InputRoute::Ui, Wanted).mouse);
    CHECK(devicesTakenBy(InputRoute::Ui, Wanted).keyboard);
    CHECK_FALSE(devicesTakenBy(InputRoute::Ui, {}).mouse);
    CHECK_FALSE(devicesTakenBy(InputRoute::Ui, Wanted).gamepad);
    // Éditeur : rien au jeu, quoi que veuille ImGui.
    const auto editor = devicesTakenBy(InputRoute::Editor, {});
    CHECK((editor.mouse && editor.keyboard && editor.gamepad));
    // Jeu : le clavier lui va même si ImGui le voudrait ; la souris lui va dans la scène, ou
    // capturée, même si ImGui la veut (la Vue sera une fenêtre d'ImGui) ; dehors, ImGui la garde.
    CHECK_FALSE(devicesTakenBy(InputRoute::Game, Wanted).keyboard);
    CHECK_FALSE(
        devicesTakenBy(InputRoute::Game, {.imguiWantsMouse = true, .mouseInScene = true}).mouse);
    CHECK_FALSE(
        devicesTakenBy(InputRoute::Game, {.imguiWantsMouse = true, .mouseCaptured = true}).mouse);
    CHECK(devicesTakenBy(InputRoute::Game, {}).mouse); // dehors, même si ImGui ne veut rien
}

TEST_CASE("un rectangle contient son bord haut gauche, pas son bord bas droit")
{
    constexpr levain::app::ScreenRect Rect{
        .x = 10.0f, .y = 20.0f, .width = 100.0f, .height = 50.0f};
    CHECK(levain::app::rectContains(Rect, 10.0f, 20.0f));
    CHECK(levain::app::rectContains(Rect, 109.9f, 69.9f));
    CHECK_FALSE(levain::app::rectContains(Rect, 110.0f, 30.0f));
    CHECK_FALSE(levain::app::rectContains(Rect, 50.0f, 70.0f));
    CHECK_FALSE(levain::app::rectContains(Rect, 9.9f, 30.0f));
    // La souris d'ImGui avant tout mouvement : hors de tout rectangle.
    CHECK_FALSE(levain::app::rectContains(Rect, -3.4e38f, -3.4e38f));
}

TEST_CASE("quand l'UI prend la manette, ses boutons et ses sticks tenus sont rendus au repos")
{
    levain::input::RawInput held;
    held.padButtons.set(3);
    held.padAxes.at(1) = 0.7f;
    const std::vector<InputEvent> events{
        {.type = InputEventType::AxisMotion,
         .device = InputDevice::Gamepad,
         .code = 1,
         .value = 0.9f},
        {.type = InputEventType::ButtonDown, .device = InputDevice::Gamepad, .code = 4}};
    const auto game = levain::app::gameInputOf(events, held, {.gamepad = true});
    CHECK_FALSE(contains(
        game, {.type = InputEventType::ButtonDown, .device = InputDevice::Gamepad, .code = 4}));
    CHECK(contains(game,
                   {.type = InputEventType::ButtonUp, .device = InputDevice::Gamepad, .code = 3}));
    REQUIRE(game.size() == 2);
    CHECK(game[1].type == InputEventType::AxisMotion);
    CHECK(game[1].value == 0.0f); // le stick est remis au centre, non pas simplement ignoré
    // Sans la manette prise, tout passe.
    CHECK(levain::app::gameInputOf(events, {}, {}).size() == 2);
}

TEST_CASE(
    "sous la route jeu, ImGui ne reçoit du clavier que les relâchements et F1, et pas le texte")
{
    using levain::platform::UiEvent;
    using levain::platform::UiEventType;
    const auto keyEvent = [](std::uint16_t scancode, bool down)
    {
        return UiEvent{
            .type = UiEventType::Key, .keycode = 'w', .scancode = scancode, .down = down};
    };
    levain::platform::Events events;
    events.ui = {keyEvent(26, true),
                 keyEvent(26, false),
                 keyEvent(levain::app::PanelsKey, true),
                 {.type = UiEventType::MouseMoved, .x = 5.0f, .y = 6.0f},
                 {.type = UiEventType::MouseButton, .button = 1, .down = true}};
    events.text = "w";

    const auto sorted = levain::app::uiEventsOf(levain::app::InputRoute::Game, events);
    REQUIRE(sorted.has_value());
    // (clang-tidy ne suit pas le REQUIRE : l'accès explicite.)
    const levain::platform::Events inGame = sorted ? *sorted : levain::platform::Events{};
    REQUIRE(inGame.ui.size() == 4);
    CHECK_FALSE(inGame.ui[0].down);                         // l'appui de W n'est plus là
    CHECK(inGame.ui[0].scancode == 26);                     // son relâchement, si
    CHECK(inGame.ui[1].scancode == levain::app::PanelsKey); // F1 reste à l'UI
    CHECK(inGame.ui[2].type == UiEventType::MouseMoved);    // la souris, toujours
    CHECK(inGame.text.empty());
    // Hors du jeu, rien n'est retiré, et rien n'est copié : pas de résultat, ImGui reçoit les
    // événements eux-mêmes, ce que le sandbox et *Rando* font à chaque image.
    for (const auto route : {levain::app::InputRoute::Ui, levain::app::InputRoute::Editor})
    {
        CHECK_FALSE(levain::app::uiEventsOf(route, events).has_value());
    }
}

TEST_CASE("la scène est le trou du nœud central, et rien tant qu'une fenêtre y est ancrée")
{
    const levain::ui::UiContext context = levain::ui::createUiContext(1.0f);
    levain::ui::prepareUiFrame(ImGui::GetIO(), {.width = 320, .height = 180}, 1.0 / 60.0);
    ImGui::NewFrame();
    ImGui::Begin("ancrée");
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImGui::End();
    ImGuiDockNode node{1};
    node.Pos = {10.0f, 20.0f};
    node.Size = {100.0f, 50.0f};
    constexpr levain::app::ScreenRect Whole{
        .x = 0.0f, .y = 0.0f, .width = 320.0f, .height = 180.0f};

    const levain::app::ScreenRect hole = levain::app::sceneRectOf(&node, Whole);
    CHECK((hole.x == 10.0f && hole.y == 20.0f && hole.width == 100.0f && hole.height == 50.0f));
    // Sans nœud central, la scène est la fenêtre.
    CHECK(levain::app::sceneRectOf(nullptr, Whole).width == 320.0f);
    // Une fenêtre ancrée dans le centre : plus de trou, le clic est à elle. L'origine reste celle
    // du nœud, où la barre de mode s'accroche.
    node.Windows.push_back(window);
    const levain::app::ScreenRect covered = levain::app::sceneRectOf(&node, Whole);
    CHECK_FALSE(levain::app::rectContains(covered, 50.0f, 40.0f));
    CHECK((covered.x == 10.0f && covered.y == 20.0f));
    ImGui::EndFrame();
}
