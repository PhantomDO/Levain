#include <cstdint>
#include <string>
#include <string_view>

#include <doctest/doctest.h>

#include "levain/platform/input_script.hpp"

using levain::platform::addScriptedEvents;
using levain::platform::Events;
using levain::platform::InputDevice;
using levain::platform::InputEventType;
using levain::platform::InputScript;
using levain::platform::parseInputScript;
using levain::platform::scriptLength;

namespace
{

constexpr std::uint16_t ScancodeW = 26;

InputScript parsed(std::string_view text)
{
    auto script = parseInputScript(text);
    REQUIRE_MESSAGE(script.has_value(), script.error().message);
    return *script;
}

Events eventsAt(const InputScript& script, int frame)
{
    Events events;
    addScriptedEvents(events, script, frame);
    return events;
}

/// Le script est refusé, et le message nomme la ligne et le champ.
void refused(std::string_view text, std::string_view line, std::string_view field)
{
    const auto script = parseInputScript(text);
    REQUIRE_FALSE(script.has_value());
    CHECK_MESSAGE(script.error().message.starts_with(std::string{line} + ", " + std::string{field}),
                  script.error().message);
}

} // namespace

TEST_CASE("un script ajoute ses événements à ceux de l'image, après les vrais, et à elle seule")
{
    const InputScript script = parsed("# un commentaire\n"
                                      "\n"
                                      "2 key down W   # la position W\n");
    CHECK(scriptLength(script) == 3); // l'image du dernier événement, plus une

    Events real;
    real.input.push_back(
        {.type = InputEventType::ButtonDown, .device = InputDevice::Keyboard, .code = 7});
    addScriptedEvents(real, script, 1);
    CHECK(real.input.size() == 1); // l'image 1 n'a rien de scripté
    addScriptedEvents(real, script, 2);
    REQUIRE(real.input.size() == 2);
    CHECK(real.input[0].code == 7); // le vrai d'abord
    CHECK(real.input[1].code == ScancodeW);
    CHECK(real.ui.size() == 1); // l'interface reçoit la même touche, par sa lettre
    CHECK(scriptLength(InputScript{}) == 0);
}

TEST_CASE("une touche a un scancode pour le jeu et un keycode pour ImGui")
{
    const Events qwerty = eventsAt(parsed("0 key down W\n"), 0);
    REQUIRE(qwerty.ui.size() == 1);
    CHECK(qwerty.ui[0].scancode == ScancodeW);
    CHECK(qwerty.ui[0].keycode == 'w');

    // Une touche qui ne tape rien a pour keycode son scancode, bit 30 posé (F1 : 58).
    CHECK(eventsAt(parsed("0 key down F1\n"), 0).ui[0].keycode == (58U | (1U << 30U)));
}

TEST_CASE("un script refuse ce qu'il ne saurait pas rejouer, en nommant la ligne et le champ")
{
    // Le contre-test de la règle n°7 : une touche inconnue ne se rejoue pas en silence.
    refused("0 key down W\n1 key down Wxyz\n", "ligne 2", "touche");
    refused("0 key down\n", "ligne 1", "touche");
    refused("0 key sideways W\n", "ligne 1", "sens");
    refused("0 key down W\n\n1x key down W\n", "ligne 3", "image");
    refused("-1 key down W\n", "ligne 1", "image");
    refused("5 key down W\n4 key up W\n", "ligne 2", "image"); // revient en arrière
    refused("7\n", "ligne 1", "type");
    refused("0 jump W\n", "ligne 1", "type");
    refused("2147483647 key down W\n", "ligne 1",
            "image"); // « dernière image plus une » déborderait
    refused("1000001 key down W\n", "ligne 1", "image");
}

TEST_CASE("un script sans aucun événement est refusé : il ne rejouerait rien")
{
    // Le contre-test de la règle n°7 : un fichier vidé par erreur ne passe pas pour un test vert.
    for (const std::string_view text : {"", "\n\n", "# rien que des commentaires\n   \n"})
    {
        const auto script = parseInputScript(text);
        REQUIRE_FALSE(script.has_value());
        CHECK(script.error().message.starts_with("aucun événement"));
    }
    // Un script construit à la main par un test passe par la même porte.
    CHECK_FALSE(levain::platform::checkInputScript(InputScript{}).has_value());
    CHECK_FALSE(levain::platform::checkInputScript(InputScript{.frames = {{-1, Events{}}}}));
    CHECK(levain::platform::checkInputScript(InputScript{.frames = {{0, Events{}}}}));
}
