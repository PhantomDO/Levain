#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace levain::platform
{

struct Window;

/// D'où vient une entrée brute. Ce module ne sait rien des intentions de jeu : « la touche 44 est
/// enfoncée », pas « le joueur saute ». La traduction est le travail d'`engine/input` (ADR-0017).
enum class InputDevice : std::uint8_t
{
    Keyboard,
    Mouse,
    Gamepad,
};

enum class InputEventType : std::uint8_t
{
    ButtonDown, ///< Touche du clavier, bouton de souris ou de manette.
    ButtonUp,
    AxisMotion, ///< Axe de manette (valeur absolue) ou mouvement de souris (déplacement relatif).
};

/// Une entrée brute, dans la numérotation de SDL. `code` est un scancode de touche, un numéro de
/// bouton ou un numéro d'axe, selon `device` et `type`.
///
/// **Un axe de manette et un mouvement de souris ne se lisent pas pareil** : le premier donne une
/// position, toujours la même tant que le joueur ne bouge pas le stick ; le second donne un
/// déplacement, qui n'existe que le temps d'une image et doit être accumulé puis remis à zéro.
struct InputEvent
{
    InputEventType type;
    InputDevice device;
    std::uint16_t code = 0;
    float value = 0.0f; ///< Axe de manette : [-1, 1]. Souris : le déplacement, en pixels.
};

/// Bornes des codes de SDL, pour dimensionner les tableaux d'état d'`engine/input`. Un
/// `static_assert` dans `input.cpp` vérifie qu'elles couvrent bien celles de SDL : si SDL en
/// ajoute, le build casse au lieu de déborder en silence.
inline constexpr std::uint16_t KeyCodeCount = 512;
inline constexpr std::uint16_t MouseButtonCount = 8;
inline constexpr std::uint16_t MouseAxisCount = 2; ///< 0 : horizontal, 1 : vertical.
inline constexpr std::uint16_t PadButtonCount = 32;
inline constexpr std::uint16_t PadAxisCount = 8;

/// Le code de SDL pour un nom écrit dans un fichier de liaisons : « Space », « Left Shift », « D »
/// pour les touches ; « left », « right », « middle » pour la souris ; « a », « start » pour les
/// boutons de manette ; « leftx », « righty », « righttrigger » pour ses axes ; « x » et « y » pour
/// la souris.
///
/// Rien si le nom n'existe pas : un fichier mal écrit doit échouer au chargement, pas donner une
/// action qui ne répond jamais (règle n°7). Attention, la table de noms de SDL est restée celle
/// d'une manette Xbox : « a » et « b », pas « south » et « east ».
[[nodiscard]] std::optional<std::uint16_t> keyCodeFromName(const std::string& name);
[[nodiscard]] std::optional<std::uint16_t> mouseButtonCodeFromName(const std::string& name);
[[nodiscard]] std::optional<std::uint16_t> mouseAxisCodeFromName(const std::string& name);
[[nodiscard]] std::optional<std::uint16_t> padButtonCodeFromName(const std::string& name);
[[nodiscard]] std::optional<std::uint16_t> padAxisCodeFromName(const std::string& name);

/// Où est le curseur, en pixels depuis le coin haut gauche de l'image : les coordonnées de la
/// fenêtre, multipliées par sa densité de pixels. Sans elle, un clic sur un écran HiDPI viserait
/// la moitié haute gauche de l'image.
struct CursorPosition
{
    float x = 0.0f;
    float y = 0.0f;
};

[[nodiscard]] CursorPosition cursorPosition(const Window& window);

/// Capture la souris : curseur caché, mouvements relatifs, sans butée d'écran. C'est ce qu'il faut
/// pour faire tourner une caméra ; sans ça, le regard s'arrête au bord de l'écran.
void setMouseCaptured(const Window& window, bool captured);

/// Ce qu'une interface demande en plus des entrées du jeu (ADR-0032) : où est la souris, la
/// molette, les touches selon la disposition du clavier, la sortie de la fenêtre, le focus. Ce
/// module ne sait rien d'ImGui : `engine/ui` traduit.
enum class UiEventType : std::uint8_t
{
    MouseMoved,  ///< `x`, `y` : la position, en pixels de l'image, densité comprise.
    MouseButton, ///< `button`, `down`.
    MouseWheel,  ///< `x` vers la droite, `y` vers le haut, en crans, sens du système compris.
    Key,         ///< `keycode`, `scancode`, `down`, `modifiers` ; répétitions comprises.
    MouseLeft,   ///< La souris a quitté la fenêtre : rien n'est plus survolé.
    FocusGained,
    FocusLost, ///< Les touches tenues ne recevront pas leur relâchement.
};

struct KeyModifiers
{
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
    bool super = false; ///< La touche Windows, ou Commande sur un Mac.
};

/// Un événement pour l'interface. **`keycode` est la touche selon la disposition du clavier** : sur
/// un AZERTY, la touche marquée A donne « a » (97), là où son scancode est celui du Q d'un QWERTY.
/// Ce sont les `SDLK_` de SDL : le point de code Unicode du caractère de la touche, ou, pour une
/// touche qui n'en tape pas (F1, les flèches), son scancode avec le bit 30 posé.
struct UiEvent
{
    UiEventType type = UiEventType::MouseMoved;
    float x = 0.0f;
    float y = 0.0f;
    std::uint32_t keycode = 0;
    std::uint16_t scancode = 0;
    /// Comme `mouseButtonCodeFromName` : 1 à gauche, 2 au milieu, 3 à droite.
    std::uint8_t button = 0;
    bool down = false;
    KeyModifiers modifiers{};
};

/// Le texte tapé n'arrive qu'entre ces deux appels : SDL n'en envoie pas sans, et sur un
/// téléphone, c'est ce qui ouvre le clavier virtuel.
void startTextInput(const Window& window);
void stopTextInput(const Window& window);

/// Le presse-papiers du système, en UTF-8 ; vide s'il ne contient pas de texte.
[[nodiscard]] std::string clipboardText();
void setClipboardText(const std::string& text);

/// Le facteur d'échelle de l'écran de la fenêtre : 1 sur un écran ordinaire, 2 ou 3 sur un écran
/// dense. Ce que l'interface applique à ses polices pour rester lisible.
[[nodiscard]] float displayScale(const Window& window);

} // namespace levain::platform
