#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include <doctest/doctest.h>

#include "levain/assets/asset_ref.hpp"
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
                                      "2 key down W   # la position W\n"
                                      "2 button down right\n");
    CHECK(scriptLength(script) == 3); // l'image du dernier événement, plus une

    Events real;
    real.input.push_back(
        {.type = InputEventType::ButtonDown, .device = InputDevice::Keyboard, .code = 7});
    addScriptedEvents(real, script, 1);
    CHECK(real.input.size() == 1); // l'image 1 n'a rien de scripté
    addScriptedEvents(real, script, 2);
    REQUIRE(real.input.size() == 3);
    CHECK(real.input[0].code == 7); // le vrai d'abord
    CHECK(real.input[1].code == ScancodeW);
    CHECK(real.input[2].device == InputDevice::Mouse);
    CHECK(real.input[2].code == 3); // le bouton droit de SDL
    CHECK(real.ui.size() == 2);     // l'interface reçoit les mêmes, par la lettre et le bouton
    CHECK(scriptLength(InputScript{}) == 0);
}

TEST_CASE("une touche a un scancode pour le jeu et un keycode pour ImGui : la paire AZERTY")
{
    const Events qwerty = eventsAt(parsed("0 key down W\n"), 0);
    REQUIRE(qwerty.ui.size() == 1);
    CHECK(qwerty.ui[0].scancode == ScancodeW);
    CHECK(qwerty.ui[0].keycode == 'w');

    // Sur un AZERTY, la touche à la place du W tape « z » : même scancode, autre lettre.
    const Events azerty = eventsAt(parsed("0 key down W as z\n"), 0);
    REQUIRE((azerty.input.size() == 1 && azerty.ui.size() == 1));
    CHECK(azerty.input[0].code == ScancodeW);
    CHECK(azerty.ui[0].scancode == ScancodeW);
    CHECK(azerty.ui[0].keycode == 'z');

    // Une touche qui ne tape rien a pour keycode son scancode, bit 30 posé (F1 : 58).
    CHECK(eventsAt(parsed("0 key down F1\n"), 0).ui[0].keycode == (58U | (1U << 30U)));
}

TEST_CASE("les modificateurs tenus accompagnent les touches suivantes, comme chez SDL")
{
    const InputScript script = parsed("0 key down Left Ctrl\n"
                                      "1 key down Z\n"
                                      "2 key up Left Ctrl\n"
                                      "3 key down Z\n");
    CHECK(eventsAt(script, 0).ui[0].modifiers.ctrl); // la touche Ctrl se compte elle-même
    CHECK(eventsAt(script, 1).ui[0].modifiers.ctrl);
    CHECK_FALSE(eventsAt(script, 2).ui[0].modifiers.ctrl);
    CHECK_FALSE(eventsAt(script, 3).ui[0].modifiers.ctrl);
}

TEST_CASE("un script refuse ce qu'il ne saurait pas rejouer, en nommant la ligne et le champ")
{
    // Le contre-test de la règle n°7 : une touche inconnue ne se rejoue pas en silence.
    refused("0 key down W\n1 key down Wxyz\n", "ligne 2", "touche");
    refused("0 key down\n", "ligne 1", "touche");
    refused("0 key down W as\n", "ligne 1", "lettre");
    refused("0 key down W as zz\n", "ligne 1", "lettre");
    refused("0 key sideways W\n", "ligne 1", "sens");
    refused("0 key down W\n\n1x key down W\n", "ligne 3", "image");
    refused("-1 key down W\n", "ligne 1", "image");
    refused("5 key down W\n4 key up W\n", "ligne 2", "image"); // revient en arrière
    refused("7\n", "ligne 1", "type");
    refused("0 jump W\n", "ligne 1", "type");
    refused("0 button down thumb\n", "ligne 1", "bouton");
    refused("0 button down\n", "ligne 1", "champs");
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

TEST_CASE("un script d'un éditeur de texte : tabulations, marque UTF-8 et fins de ligne de Windows")
{
    const InputScript script =
        parsed("\xEF\xBB\xBF" // un « 0 » collé à la marque serait lu comme un chiffre hexadécimal
               "0\tkey\tdown\tLeft Ctrl\r\n"
               "1 key\tdown W\r\n");
    CHECK(scriptLength(script) == 2);
    CHECK(eventsAt(script, 1).ui[0].modifiers.ctrl);
}

TEST_CASE("un script se lit dans un fichier, et un refus nomme ce fichier")
{
    // Un nom à lui : deux worktrees lancent ctest en même temps sur cette machine.
    const std::filesystem::path file =
        std::filesystem::temp_directory_path() /
        ("levain-input-script-" + levain::assets::toString(levain::assets::generateAssetId()) +
         ".txt");
    std::ofstream(file) << "0 key down W\r\n1 key down Wxyz\r\n"; // les fins de ligne de Windows
    const auto script = levain::platform::loadInputScript(file);
    std::filesystem::remove(file);
    REQUIRE_FALSE(script.has_value());
    CHECK(script.error().message.find(file.string()) != std::string::npos);
    CHECK(script.error().message.find("ligne 2, touche") != std::string::npos);
    CHECK_FALSE(levain::platform::loadInputScript(file).has_value()); // effacé
}
