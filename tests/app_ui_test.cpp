#include <algorithm>
#include <vector>

#include <doctest/doctest.h>

#include "levain/app/ui_layer.hpp"

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
    const auto game = levain::app::gameInputOf(events, held, false, true);
    CHECK_FALSE(contains(game, key(36, true))); // taper un nombre ne fait pas marcher le renard
    CHECK(contains(game, key(26, false)));      // jamais un relâchement perdu
    // Sans clavier pris, tout passe.
    CHECK(levain::app::gameInputOf(events, held, false, false).size() == 2);
}

TEST_CASE("quand l'UI prend un appareil, ce qui y était tenu est relâché pour le jeu")
{
    levain::input::RawInput held;
    held.keys.set(26);
    held.mouseButtons.set(1);
    const auto keyboard = levain::app::gameInputOf({}, held, false, true);
    CHECK(contains(keyboard, key(26, false)));
    CHECK(keyboard.size() == 1); // la souris n'est pas prise : son bouton reste tenu
    const auto mouse = levain::app::gameInputOf({}, held, true, false);
    CHECK(contains(mouse,
                   {.type = InputEventType::ButtonUp, .device = InputDevice::Mouse, .code = 1}));
    // Un mouvement de souris pris par l'UI ne tourne pas la caméra.
    const std::vector<InputEvent> motion{{.type = InputEventType::AxisMotion,
                                          .device = InputDevice::Mouse,
                                          .code = 0,
                                          .value = 12.0f}};
    CHECK(levain::app::gameInputOf(motion, {}, true, false).empty());
}

TEST_CASE("F1 est la touche de l'UI : le jeu ne la voit jamais appuyée")
{
    const std::vector<InputEvent> events{key(levain::app::PanelsKey, true)};
    CHECK(levain::app::gameInputOf(events, {}, false, false).empty());
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
