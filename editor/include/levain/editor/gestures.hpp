#pragma once

// Les gestes de la caméra de l'éditeur (ADR-0036, décisions 6, 8 et 13), lus sur l'input brut :
// `App::rawFrameEvents`, avant le filtre de route qui retire la souris au jeu et celui de la
// capture qui la retire à ImGui. La logique est en fonctions libres dont toutes les dépendances
// sont des paramètres (ADR-0011) : `stepGestures` plie les événements d'une image dans un état et
// dans ce que la caméra doit faire (`GestureFrame`). Elle ne lit ni ImGui ni flecs (ce qu'ImGui a
// dit de la Vue lui vient dans `GestureContext`, seul `ScreenRect` est emprunté à `app`) :
// `withEditor` n'en aura que la glu.
// Équivalents : les gestes de la vue de niveau d'Unreal (documenté, ADR-0036 [1]) et de la vue
// Scène d'Unity ; le Viewport 3D de Godot, au bouton du milieu pour l'orbite.

#include <bitset>
#include <cstdint>

#include <glm/glm.hpp>

#include "levain/app/ui_layer.hpp"
#include "levain/editor/shortcuts.hpp"
#include "levain/platform/input.hpp"
#include "levain/platform/window.hpp"

namespace levain::editor
{

/// Le geste en cours. Un seul à la fois : celui du bouton qui l'a commencé.
enum class Gesture : std::uint8_t
{
    None,
    Fly,   ///< Clic droit tenu : le regard à la souris, les touches de vol, la molette = vitesse.
    Orbit, ///< Alt + clic gauche : tourne autour du pivot.
    Pan,   ///< Bouton du milieu : fait glisser la vue.
    DollyDrag, ///< Alt + clic droit : avance et recule vers le pivot.
};

/// Les numéros de boutons de souris des événements (ceux de SDL et de `mouseButtonCodeFromName`).
inline constexpr std::uint8_t LeftButton = 1;
inline constexpr std::uint8_t MiddleButton = 2;
inline constexpr std::uint8_t RightButton = 3;

/// Les scancodes d'USB des modificateurs que les gestes lisent : Maj accélère, Alt change le geste.
inline constexpr std::uint16_t LeftShiftScancode = 225;
inline constexpr std::uint16_t LeftAltScancode = 226;
inline constexpr std::uint16_t RightShiftScancode = 229;
inline constexpr std::uint16_t RightAltScancode = 230;

/// Une image plus longue que cela (le débogueur, un déplacement de fenêtre, une fenêtre cachée) ne
/// fait pas voler la caméra de toute sa durée : `clampFlightSeconds`.
inline constexpr float MaxFlightSeconds = 0.1f;

/// **La durée qu'un vol compte pour une image** : au plus `MaxFlightSeconds`, jamais négative ni
/// NaN (zéro). Sans ce plafond, une image de deux secondes lance la caméra à 2 × la vitesse de vol.
[[nodiscard]] constexpr float clampFlightSeconds(float seconds)
{
    return seconds > 0.0f ? (seconds < MaxFlightSeconds ? seconds : MaxFlightSeconds) : 0.0f;
}

/// Maj tenu pendant le vol multiplie la vitesse (documenté pour Unity, ADR-0036 [4] ; supposé pour
/// Unreal).
inline constexpr float RunMultiplier = 3.0f;

/// **Le geste qu'un bouton commence**, selon Alt tenu. Le clic gauche seul ne commence rien : il
/// choisit (M7.4), il ne bouge pas la caméra.
[[nodiscard]] constexpr Gesture gestureStartedBy(std::uint8_t button, bool altHeld)
{
    switch (button)
    {
    case RightButton:
        return altHeld ? Gesture::DollyDrag : Gesture::Fly;
    case LeftButton:
        return altHeld ? Gesture::Orbit : Gesture::None;
    case MiddleButton:
        return Gesture::Pan;
    default:
        return Gesture::None;
    }
}

/// Ce que les gestes gardent d'une image à l'autre.
struct GestureState
{
    Gesture active = Gesture::None;
    std::uint8_t button = 0; ///< Le bouton qui a commencé `active` : son relâchement le finit.
    std::bitset<platform::KeyCodeCount> keys; ///< Les touches tenues, par position (scancode).
    glm::vec2 cursor{0.0f};
    /// Au moins une position reçue : sans elle, un appui n'est nulle part.
    bool cursorKnown = false;
};

/// Où l'image se déroule : la région où un geste peut commencer et ce qu'ImGui dit des fenêtres.
struct GestureContext
{
    /// Le rectangle de la scène, en pixels de l'image : la Vue (panneaux ouverts) ou la fenêtre
    /// entière (fermés). Vide : aucun geste.
    app::ScreenRect region{};
    /// La scène est la fenêtre sous la souris : la Vue n'est pas cachée par une autre, ni sa barre
    /// de mode survolée. Lu à l'image d'avant (ImGui ne le sait qu'à la fin de `ui`).
    bool sceneHovered = false;
};

/// Ce que la caméra doit faire à cette image.
struct GestureFrame
{
    Gesture gesture = Gesture::None;
    glm::vec2 motion{0.0f}; ///< Le mouvement relatif de la souris, en pixels (y vers le bas).
    float wheelNotches = 0.0f;
    glm::vec3 move{0.0f}; ///< Comme `FlyInput::move` : x la droite, y le haut, z l'avant.
    float speedMultiplier = 1.0f;
    float seconds = 0.0f;
};

/// Le résultat d'une image de gestes.
struct GestureStep
{
    GestureState state;
    GestureFrame frame;
};

/// Le vol est en cours : la souris est capturée (`App::mouseCaptureWanted`), panneaux ouverts
/// compris (décision 8). **Seul le vol capture** : l'orbite, le pan et le dolly lisent le mouvement
/// relatif sans prendre la souris, et un clic sans vol non plus.
[[nodiscard]] constexpr bool capturesMouse(const GestureState& state)
{
    return state.active == Gesture::Fly;
}

/// L'état au repos : ce qu'on remet quand le mode quitte l'Édition, que la fenêtre perd le focus ou
/// qu'elle est cachée (les relâchements reçus pendant ce temps se perdent).
/// Le curseur reste connu, pour que le prochain appui soit situé.
[[nodiscard]] GestureState idleGestures(const GestureState& state);

/// **Les déplacements du vol** par position : les six actions de vol de la table, tenues (Maj ne
/// les coupe pas : `positionHeld`). Une diagonale n'a pas de norme particulière ici, `flyCamera`
/// la ramène. Le piège : la position, non la lettre, sinon un AZERTY volerait avec Q à gauche... et
/// A en avant ; on lit les scancodes de la table.
[[nodiscard]] glm::vec3 flightMoveOf(const ShortcutTable& table,
                                     const std::bitset<platform::KeyCodeCount>& keys);

/// **Plie les événements d'une image** dans l'état des gestes, dans l'ordre où ils sont arrivés
/// pour l'interface (positions, boutons, molette, touches) : un geste ne commence que si le curseur
/// est dans `context.region` et la scène sous la souris ; un autre geste commencé hors de la Vue
/// ne fait rien, ni ne se continue au retour du curseur. Le mouvement relatif (`events.input`) va
/// au geste en cours à la fin de l'image, ou commencé dans cette image. Le vol suit la position
/// des touches ; la molette règle sa vitesse, et zoome sinon (la scène sous la souris). Les
/// positions reçues pendant un vol sont ignorées : la souris capturée n'en a pas de vraie. Une
/// fenêtre cachée (`WindowEventType::Hidden`) remet tout au repos.
[[nodiscard]] GestureStep stepGestures(GestureState state, const platform::Events& events,
                                       const GestureContext& context, const ShortcutTable& table,
                                       float seconds);

} // namespace levain::editor
