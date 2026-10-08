#include <chrono>
#include <cstdlib>
#include <optional>
#include <string>

#include <doctest/doctest.h>

#include "levain/core/environment.hpp"
#include "levain/platform/window.hpp"

using namespace std::chrono_literals;

// SDL_VIDEO_DRIVER=offscreen (tests/CMakeLists.txt) : une vraie fenêtre SDL, sans écran.

namespace
{

void setEnvironmentVariable(const char* name, const std::optional<std::string>& value)
{
#ifdef _WIN32
    (void)_putenv_s(name, value ? value->c_str() : ""); // une valeur vide retire la variable
#else
    if (value)
    {
        (void)setenv(name, value->c_str(), 1);
    }
    else
    {
        (void)unsetenv(name);
    }
#endif
}

/// Le pilote vidéo « offscreen » de SDL pour la durée d'une portée : aucun événement du système
/// n'y arrive. Le test le pose lui-même plutôt que de s'en remettre à l'ENVIRONMENT de ctest : un
/// exe Windows lancé depuis WSL ne reçoit que les variables que WSLENV nomme (build/GOTCHA.md), et
/// sa fenêtre s'ouvre alors sur le vrai bureau, où arrivent des événements du système, que le
/// moteur ne traduit pas tous. Mesurés, plusieurs tests lancés ensemble :
/// SDL_EVENT_WINDOW_FOCUS_LOST et SDL_EVENT_MOUSE_ADDED, qui font revenir SDL_WaitEventTimeout bien
/// avant l'échéance.
struct ScopedOffscreenDriver
{
    ScopedOffscreenDriver() { setEnvironmentVariable("SDL_VIDEO_DRIVER", "offscreen"); }

    ~ScopedOffscreenDriver() { setEnvironmentVariable("SDL_VIDEO_DRIVER", previous); }

    ScopedOffscreenDriver(const ScopedOffscreenDriver&) = delete;
    ScopedOffscreenDriver& operator=(const ScopedOffscreenDriver&) = delete;

    std::optional<std::string> previous = levain::core::environmentVariable("SDL_VIDEO_DRIVER");
};

} // namespace

TEST_CASE("waitEvents rend la main à l'échéance quand aucun événement n'arrive")
{
    // Le cas d'une fenêtre masquée sur un bureau verrouillé : plus rien n'arrive, et sans limite
    // l'attente ne reviendrait jamais. Un événement du système, même un que le moteur ne traduit
    // pas, ferait revenir `waitEvents` plus tôt, à raison (« jusqu'au premier événement ») : le
    // test ne mesure l'échéance que sans événement possible, d'où le pilote offscreen.
    const ScopedOffscreenDriver offscreen;
    REQUIRE(levain::core::environmentVariable("SDL_VIDEO_DRIVER") == "offscreen");
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
