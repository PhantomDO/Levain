#include <chrono>

#include <doctest/doctest.h>

#include "levain/platform/window.hpp"

using namespace std::chrono_literals;

// SDL_VIDEO_DRIVER=offscreen (tests/CMakeLists.txt) : une vraie fenêtre SDL, sans écran.

TEST_CASE("waitEvents rend la main à l'échéance quand aucun événement n'arrive")
{
    // Le cas d'une fenêtre masquée sur un bureau verrouillé : plus rien n'arrive, et sans limite
    // l'attente ne reviendrait jamais.
    auto window = levain::platform::createWindow("Levain", 64, 64);
    REQUIRE(window.has_value());
    // Ce que la création a mis dans la file : sinon l'attente reviendrait tout de suite.
    (void)levain::platform::pollEvents(*window);

    const auto start = std::chrono::steady_clock::now();
    const levain::platform::Events events = levain::platform::waitEvents(*window, 0.05);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    CHECK(events.window.empty());
    CHECK(elapsed >= 40ms); // a bien dormi, pas une attente nulle
    CHECK(elapsed < 5s);
}

TEST_CASE("ce qu'une interface demande à la fenêtre : l'échelle, le presse-papiers, la saisie")
{
    auto window = levain::platform::createWindow("Levain", 64, 64);
    REQUIRE(window.has_value());
    // Une échelle lisible, jamais nulle : une interface à l'échelle 0 serait invisible.
    CHECK(levain::platform::displayScale(*window) >= 1.0f);
    // Le texte du presse-papiers revient tel quel, accents compris.
    levain::platform::setClipboardText("Lève-toi, renard");
    CHECK(levain::platform::clipboardText() == "Lève-toi, renard");
    // La saisie s'ouvre et se ferme sans erreur, sans clavier physique.
    levain::platform::startTextInput(*window);
    const levain::platform::Events events = levain::platform::pollEvents(*window);
    CHECK(events.text.empty());
    levain::platform::stopTextInput(*window);
}
