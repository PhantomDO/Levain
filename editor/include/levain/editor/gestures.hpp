#pragma once

// Les gestes de la caméra de l'éditeur (ADR-0036, décisions 6, 8 et 13). La logique est en
// fonctions libres dont toutes les dépendances sont des paramètres (ADR-0011) : ici, le geste
// qu'un bouton commence, la durée qu'un vol compte et les touches de vol, lues par position dans
// la table des raccourcis.
// Équivalents : les gestes de la vue de niveau d'Unreal (documenté, ADR-0036 [1]) et de la vue
// Scène d'Unity ; le Viewport 3D de Godot, au bouton du milieu pour l'orbite.

#include <bitset>
#include <cstdint>

#include <glm/glm.hpp>

#include "levain/editor/shortcuts.hpp"
#include "levain/platform/input.hpp"

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

/// Une image plus longue que cela (le débogueur, un déplacement de fenêtre, une fenêtre cachée) ne
/// fait pas voler la caméra de toute sa durée : `clampFlightSeconds`.
inline constexpr float MaxFlightSeconds = 0.1f;

/// **La durée qu'un vol compte pour une image** : au plus `MaxFlightSeconds`, jamais négative ni
/// NaN (zéro). Sans ce plafond, une image de deux secondes lance la caméra à 2 × la vitesse de vol.
[[nodiscard]] constexpr float clampFlightSeconds(float seconds)
{
    return seconds > 0.0f ? (seconds < MaxFlightSeconds ? seconds : MaxFlightSeconds) : 0.0f;
}

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

/// **Les déplacements du vol** par position : les six actions de vol de la table, tenues (Maj ne
/// les coupe pas : `positionHeld`). Une diagonale n'a pas de norme particulière ici, `flyCamera`
/// la ramène. Le piège : la position, non la lettre, sinon un AZERTY volerait avec Q à gauche... et
/// A en avant ; on lit les scancodes de la table.
[[nodiscard]] glm::vec3 flightMoveOf(const ShortcutTable& table,
                                     const std::bitset<platform::KeyCodeCount>& keys);

} // namespace levain::editor
