#pragma once

#include "levain/scene/components.hpp"

namespace levain::scene
{

/// Ce qui règle une caméra libre, et où elle regarde. Les angles sont **la source** : la rotation
/// du `Transform` en est calculée à chaque pas. L'inverse — retrouver le lacet et le tangage depuis
/// un quaternion — serait ambigu (`docs/QA.md`).
struct FpsController
{
    float moveSpeed = 12.0f;         ///< Unités par seconde.
    float sprintMultiplier = 4.0f;   ///< Ce que « sprint » multiplie.
    float lookDegreesPerUnit = 1.0f; ///< Degrés par seconde et par unité d'axe de regard.
    float minPitchDegrees = -85.0f;  ///< On ne passe pas par-dessus la tête.
    float maxPitchDegrees = 85.0f;
    float yawDegrees = 0.0f;   ///< Autour de l'axe vertical. 0 : la caméra regarde vers -Z.
    float pitchDegrees = 0.0f; ///< Positif vers le haut.
};

/// Les intentions du joueur pour cette image, déjà lues : ce composant est un **singleton** du
/// monde, posé par l'application depuis `engine/input`. C'est ce qui permet à `scene` d'ignorer
/// jusqu'à l'existence du module d'input (SPECS §7).
struct FpsInput
{
    glm::vec2 move{0.0f}; ///< x : la droite, y : l'avant. Entre -1 et 1.
    float up = 0.0f;      ///< Monter ou descendre, indépendamment du regard.
    glm::vec2 look{0.0f}; ///< x : tourner à droite, y : lever les yeux. Une vitesse (ADR-0017).
    bool sprint = false;
};

/// Une direction unitaire, ou zéro si le vecteur est nul. Deux pièges d'un coup : pas de division
/// par zéro quand personne n'appuie, et la diagonale n'avance pas plus vite que la ligne droite.
[[nodiscard]] glm::vec3 normalizeOrZero(glm::vec3 direction);

/// Le tangage, borné : sans ça, la caméra passe par-dessus la tête et le monde se retourne. Des
/// bornes inversées (`FpsController` les laisse régler séparément) sont remises dans l'ordre.
[[nodiscard]] float clampPitch(float pitchDegrees, float minDegrees, float maxDegrees);

/// L'angle à tourner pour aller de `from` à `to`, en radians, par le plus court chemin : entre −π
/// et π. Le piège : de 350° à 10°, l'écart brut est de −340°, et un lacet qui le suivrait ferait
/// presque un tour complet au lieu de 20°. Le personnage qui se tourne vers sa marche et la caméra
/// qui se recentre derrière lui (ADR-0030) s'en servent.
[[nodiscard]] float shortestYawDelta(float fromRadians, float toRadians);

/// L'avant et la droite **à plat**, pour le lacet donné : le tangage ne participe pas au
/// déplacement, sinon regarder le ciel ferait décoller la caméra.
struct HorizontalBasis
{
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
    glm::vec3 right{1.0f, 0.0f, 0.0f};
};

[[nodiscard]] HorizontalBasis horizontalBasisFrom(float yawDegrees);

/// Avance la caméra et son regard de ce que le joueur a demandé pendant `seconds`. Toutes les
/// dépendances sont dans la signature (ADR-0011) : ni monde, ni singleton caché, ni SDL.
void applyFpsInput(Transform& transform, FpsController& controller, const FpsInput& input,
                   float seconds);

/// Vers où pointe la caméra : l'avant du repère, tangage compris.
[[nodiscard]] glm::vec3 forwardOf(const FpsController& controller);

} // namespace levain::scene
