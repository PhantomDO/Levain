#pragma once

#include <cstdint>
#include <memory>
#include <string>
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
    CloseRequested, ///< Croix, Alt+F4, Ctrl+C ou SIGTERM.
    Resized,        ///< Nouvelle taille dans `WindowEvent::pixelSize`.
    Hidden,         ///< Minimisée ou entièrement recouverte : plus rien à dessiner.
    Shown,          ///< De nouveau visible.
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

/// Démarre SDL et ouvre une fenêtre redimensionnable. La taille demandée est en **points** :
/// c'est le compositeur qui applique l'échelle de l'écran, et `Resized` donnera les pixels.
///
/// Échoue si aucun serveur d'affichage n'est joignable (ni Wayland, ni X11) : c'est une cause
/// extérieure, pas un bug (ADR-0008).
[[nodiscard]] core::Result<Window> createWindow(const std::string& title, int width, int height);

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

} // namespace levain::platform
