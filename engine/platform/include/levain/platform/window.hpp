#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "levain/core/error.hpp"
#include "levain/platform/input.hpp"

// Déclaration anticipée : c'est tout ce que ce fichier sait de SDL. Aucun en-tête SDL ne sort
// de `platform/` (ADR-0003), sauf dans `gpu/` pour créer la surface Vulkan à partir du handle.
struct SDL_Window;

namespace levain::platform
{

/// Taille en **pixels**, pas en points : sur un écran à 200 %, une fenêtre de 1280 × 720 points
/// fait 2560 × 1440 pixels. C'est la taille en pixels dont la swapchain aura besoin (M1.2).
struct PixelSize
{
    int width = 0;
    int height = 0;
};

enum class WindowEventType : std::uint8_t
{
    CloseRequested, ///< Croix ou Alt+F4 : le programme peut demander avant de quitter.
    /// Ctrl+C ou SIGTERM (`SDL_EVENT_QUIT`) : on quitte sans demander, comme tout programme que
    /// l'on arrête de l'extérieur (ADR-0036, décision 11).
    QuitSignal,
    Resized, ///< Nouvelle taille dans `WindowEvent::pixelSize`.
    Hidden,  ///< Minimisée ou entièrement recouverte : plus rien à dessiner.
    Shown,   ///< De nouveau visible.
};

struct WindowEvent
{
    WindowEventType type;
    PixelSize pixelSize{}; ///< Renseignée pour `Resized` uniquement, zéro sinon.
};

/// Détruit la fenêtre puis arrête SDL, dans cet ordre. Défini dans `window.cpp`, le seul
/// fichier du moteur qui inclut SDL.
struct WindowDeleter
{
    void operator()(SDL_Window* window) const noexcept;
};

/// Une fenêtre du système. Déplaçable, pas copiable. SDL vit aussi longtemps qu'elle : il n'y a
/// donc **qu'une fenêtre à la fois** (voir le README du module).
struct Window
{
    std::unique_ptr<SDL_Window, WindowDeleter> handle;
};

/// Ce que la fenêtre annonce à SDL pour la surface où le GPU dessinera. SDL charge la bibliothèque
/// Vulkan (`vulkan-1.dll` sous Windows) dès qu'une fenêtre annonce Vulkan, et en refuse la création
/// si elle manque (`SDL_video.c`, `SDL_CreateWindow`) : la fenêtre de Direct3D 12 n'annonce rien,
/// pour qu'une machine sans chargeur Vulkan le lance quand même. Celle de WebGPU en natif non plus,
/// mais Dawn charge `vulkan-1.dll` lui-même pour son backend Vulkan : seul Direct3D 12 s'en passe.
/// `gpu::surfaceFor` choisit d'après le backend. Ignoré dans le navigateur, où la fenêtre est le
/// canvas de la page.
enum class GraphicsSurface : std::uint8_t
{
    Vulkan, ///< La fenêtre porte une surface Vulkan (`SDL_WINDOW_VULKAN`).
    None,   ///< Aucune surface : le backend présente autrement, ou ne présente pas.
};

/// Démarre SDL et ouvre une fenêtre redimensionnable. La taille demandée est en **points** :
/// c'est le compositeur qui applique l'échelle de l'écran, et `Resized` donnera les pixels.
///
/// Échoue si aucun serveur d'affichage n'est joignable (ni Wayland, ni X11) : c'est une cause
/// extérieure, pas un bug (ADR-0008). Échoue aussi pour `GraphicsSurface::Vulkan` sans chargeur
/// Vulkan. **Le titre est ASCII** (`asciiTitleOf`) : assertion en Debug.
[[nodiscard]] core::Result<Window> createWindow(const std::string& title, int width, int height,
                                                GraphicsSurface surface);

/// Taille actuelle de la zone de dessin, en pixels. Sous Wayland, c'est la seule source fiable : la
/// surface Vulkan n'y connaît pas sa propre taille, et c'est à l'application de la donner.
[[nodiscard]] PixelSize windowPixelSize(const Window& window);

/// Ce qu'une pompe d'événements a récolté : ce qui concerne la fenêtre, ce qui vient des
/// périphériques, et ce que demande une interface (ADR-0032). Tous sortent du même appel parce
/// qu'ils sortent de la même file SDL.
struct Events
{
    std::vector<WindowEvent> window;
    std::vector<InputEvent> input;
    std::vector<UiEvent> ui;
    std::string text; ///< Le texte tapé, en UTF-8, entre `startTextInput` et `stopTextInput`.
    /// `waitEvents` : un événement SDL l'a réveillée avant l'échéance, y compris un que le moteur
    /// ne traduit pas (une souris branchée, le focus) et qui ne laisse rien dans les listes
    /// ci-dessus. Faux à l'échéance, et pour `pollEvents`.
    bool woken = false;
};

/// Les événements arrivés depuis le dernier appel, sans attendre.
[[nodiscard]] Events pollEvents(const Window& window);

/// Comme `pollEvents`, mais dort jusqu'au premier événement, ou au plus `maxSeconds` : l'infini
/// pour attendre sans limite, 0 ou moins pour ne pas attendre. Pour une fenêtre masquée : sans
/// rien à dessiner, la boucle tournerait à vide à 100 % d'un cœur. La limite sert à une boucle qui
/// a une échéance : sans elle, une fenêtre masquée qui ne reçoit plus rien dormirait au-delà.
[[nodiscard]] Events waitEvents(const Window& window, double maxSeconds);

/// Le titre doit être en **ASCII**, et une assertion le vérifie. Sous X11, SDL 3.4.12 abandonne
/// sans rien dire tout titre qu'il ne sait pas convertir dans la locale C, et fuit au passage
/// (`SDL_x11window.c:2300`). « — » et « × » y échouent, « é » passe : trop imprévisible pour
/// autoriser quoi que ce soit hors ASCII tant que ce n'est pas corrigé en amont.
void setWindowTitle(Window& window, const std::string& title);

/// Une fermeture de la dernière fenêtre enverrait-elle aussi `SDL_EVENT_QUIT` ? Faux tant qu'une
/// fenêtre créée par `createWindow` vit (SDL_Quit efface l'indice à sa destruction). **Piège** :
/// vrai, la croix quitterait l'éditeur sans modale, et aucun autre test ne le verrait (la croix
/// scriptée n'en passe pas par SDL).
[[nodiscard]] bool closeSendsQuit();

/// Sort la fenêtre de la minimisation et la met devant : un programme qui répond à la croix par une
/// question ne doit pas la poser à une fenêtre que personne ne voit.
void restoreAndRaise(Window& window);

/// Le titre que la fenêtre porte (celui de SDL) : ce que le banc d'essai relit pour prouver
/// qu'aucun texte hors ASCII n'y est arrivé.
[[nodiscard]] std::string windowTitle(const Window& window);

/// Le texte, rendu acceptable par `setWindowTitle` : chaque caractère hors ASCII (un caractère
/// UTF-8 entier, pas chacun de ses octets) devient « ? ». **Tout texte qui n'est pas écrit à la
/// main dans le code passe par elle avant d'atteindre le titre** (un nom de scène, un nom de
/// fichier) : l'assertion de `setWindowTitle` n'est qu'un filet, absent en Release. Le nom de la
/// scène et son « * » vont dans la barre de menus d'ImGui, qui sait l'UTF-8 (ADR-0036, décision
/// 11).
[[nodiscard]] std::string asciiTitleOf(std::string_view text);

} // namespace levain::platform
