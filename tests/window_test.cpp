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

/// Une variable d'environnement posée pour la durée d'une portée, puis remise comme elle était.
/// SDL 3.4 lit sa propre copie de l'environnement (SDL_GetEnvironment), prise à SDL_Init et jetée
/// par SDL_Quit : une variable posée ici compte parce que chaque fenêtre initialise puis arrête SDL
/// (engine/platform/src/window.cpp).
struct ScopedVariable
{
    ScopedVariable(const char* variable, const std::string& value)
        : name(variable), previous(levain::core::environmentVariable(variable))
    {
        setEnvironmentVariable(name, value);
    }

    ~ScopedVariable() { setEnvironmentVariable(name, previous); }

    ScopedVariable(const ScopedVariable&) = delete;
    ScopedVariable& operator=(const ScopedVariable&) = delete;

    const char* name;
    std::optional<std::string> previous;
};

/// Le pilote vidéo « offscreen » de SDL pour la durée d'une portée : aucun événement du système
/// n'y arrive. Le test le pose lui-même plutôt que de s'en remettre à l'ENVIRONMENT de ctest : un
/// exe Windows lancé depuis WSL ne reçoit que les variables que WSLENV nomme (build/GOTCHA.md), et
/// sa fenêtre s'ouvre alors sur le vrai bureau, où arrivent des événements du système, que le
/// moteur ne traduit pas tous. Mesurés, plusieurs tests lancés ensemble :
/// SDL_EVENT_WINDOW_FOCUS_LOST et SDL_EVENT_MOUSE_ADDED, qui font revenir SDL_WaitEventTimeout bien
/// avant l'échéance. Le REQUIRE vérifie la variable, pas le pilote que SDL retient.
struct ScopedOffscreenDriver : ScopedVariable
{
    ScopedOffscreenDriver() : ScopedVariable("SDL_VIDEO_DRIVER", "offscreen") {}
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
    auto window =
        levain::platform::createWindow("Levain", 64, 64, levain::platform::GraphicsSurface::Vulkan);
    REQUIRE(window.has_value());
    // Ce que la création a mis dans la file : sinon l'attente reviendrait tout de suite.
    (void)levain::platform::pollEvents(*window);

    // `waitEvents` rend la main au premier événement SDL, même un que le moteur ne traduit pas (le
    // pilote offscreen sonde toutes les millisecondes, et une souris branchée ou un focus perdu
    // arrive dans la file) : une attente écourtée n'a rien à dire de l'échéance (`Events::woken`).
    // On recommence jusqu'à une attente que rien n'a écourtée, et on exige qu'il y en ait une : le
    // test ne devient pas muet quand des événements parasites arrivent (règle n°7).
    bool slept = false;
    for (int attempt = 0; attempt < 20 && !slept; ++attempt)
    {
        const auto start = std::chrono::steady_clock::now();
        const levain::platform::Events events = levain::platform::waitEvents(*window, 0.05);
        const auto elapsed = std::chrono::steady_clock::now() - start;

        CHECK(events.window.empty());
        if (!events.woken)
        {
            CHECK(elapsed >= 40ms); // a bien dormi, pas une attente nulle
            CHECK(elapsed < 5s);
            slept = true;
        }
    }
    REQUIRE(slept);
}

TEST_CASE("ce qu'une interface demande à la fenêtre : l'échelle, le presse-papiers, la saisie")
{
    auto window =
        levain::platform::createWindow("Levain", 64, 64, levain::platform::GraphicsSurface::Vulkan);
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

TEST_CASE("une fenêtre n'annonce Vulkan qu'à la demande : sans chargeur, seule celle-là échoue")
{
    // SDL charge la bibliothèque Vulkan pour toute fenêtre qui annonce Vulkan (SDL_video.c,
    // SDL_CreateWindow). Le chargeur est ici remplacé par un fichier qui n'existe pas, comme sur un
    // runner Windows sans vulkan-1.dll : la fenêtre Vulkan doit échouer, ce qui prouve que le
    // chargeur est bien lu, et celle qui n'annonce rien doit s'ouvrir quand même (Direct3D 12,
    // #19).
    const ScopedOffscreenDriver offscreen;
    const ScopedVariable missingLoader("SDL_VULKAN_LIBRARY", "levain-absent-vulkan-loader");
    REQUIRE(levain::core::environmentVariable("SDL_VULKAN_LIBRARY") ==
            "levain-absent-vulkan-loader");

    // Des temporaires : une seule fenêtre à la fois (createWindow), la première doit être détruite
    // avant la seconde, même quand le test est rouge.
    CHECK_FALSE(
        levain::platform::createWindow("Levain", 64, 64, levain::platform::GraphicsSurface::Vulkan)
            .has_value());
    CHECK(levain::platform::createWindow("Levain", 64, 64, levain::platform::GraphicsSurface::None)
              .has_value());
}

TEST_CASE("asciiTitleOf : l'ASCII passe, chaque caractère hors ASCII devient un seul « ? »")
{
    using levain::platform::asciiTitleOf;
    CHECK(asciiTitleOf("Levain - 3.2 ms (min 3.0)") == "Levain - 3.2 ms (min 3.0)");
    CHECK(asciiTitleOf("") == "");
    CHECK(asciiTitleOf("Sc\xC3\xA8ne") == "Sc?ne");               // « è », deux octets
    CHECK(asciiTitleOf("A \xE2\x80\x94 B") == "A ? B");           // « — », trois octets
    CHECK(asciiTitleOf("\xF0\x9F\xA6\x8A renard") == "? renard"); // un émoji, quatre octets
    CHECK(asciiTitleOf("\xA9x") == "x");                          // une suite isolée est perdue
    // Ce que setWindowTitle accepte : le résultat est toujours de l'ASCII.
    for (const unsigned char byte : {0x80, 0xC3, 0xFF})
    {
        const std::string raw{static_cast<char>(byte)};
        for (const char c : asciiTitleOf(raw))
        {
            CHECK(static_cast<unsigned char>(c) < 0x80);
        }
    }
}

TEST_CASE(
    "la croix d'une fenêtre ne quitte pas : l'indice de SDL est posé, même contre l'environnement")
{
    // Sans lui, la croix enverrait CLOSE_REQUESTED puis QUIT, et l'éditeur quitterait sans modale
    // : aucun test de la boucle ne le voit, la croix y est scriptée.
    const ScopedOffscreenDriver offscreen;
    // Une variable d'environnement SDL_QUIT_ON_LAST_WINDOW_CLOSE ferait échouer un SDL_SetHint
    // ordinaire, en silence : l'indice est posé avec la priorité OVERRIDE.
    const ScopedVariable environment("SDL_QUIT_ON_LAST_WINDOW_CLOSE", "1");
    auto window =
        levain::platform::createWindow("Levain", 64, 64, levain::platform::GraphicsSurface::None);
    REQUIRE(window.has_value());
    CHECK_FALSE(levain::platform::closeSendsQuit());
}
