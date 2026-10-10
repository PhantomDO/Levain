#pragma once

// La caméra de l'éditeur (ADR-0036, décision 6) : un état, et des fonctions libres qui rendent le
// suivant (ADR-0011). Ce n'est pas une entité : elle ne vit ni dans la scène ni dans le pas fixe,
// et ne s'enregistre jamais. Ici aucune entrée n'est lue : les gestes (morceau 11) donnent des
// deltas en pixels, en crans de molette ou en unités, la durée de l'image, des multiplicateurs ; ce
// fichier ne connaît ni SDL, ni ImGui, ni flecs.

#include <glm/glm.hpp>

namespace levain::editor
{

/// Le tangage ne va pas jusqu'à ±90° : `lookAtRH` (render/src/camera.cpp:10) cherche la droite du
/// regard par le produit de son avant et de la verticale, nul quand les deux sont parallèles, et
/// au-delà de 90° l'image se retourne.
inline constexpr float MaxEditorPitchDegrees = 89.0f;

/// Les bornes de la vitesse de vol, en unités par seconde : la vue Scène d'Unity borne la sienne
/// (documenté, ADR-0036 [4]). Un cran de molette la multiplie par `FlySpeedPerNotch`, car 1 → 2
/// compte plus que 100 → 101.
inline constexpr float MinFlySpeed = 0.1f;
inline constexpr float MaxFlySpeed = 1000.0f;
inline constexpr float DefaultFlySpeed = 8.0f;
inline constexpr float FlySpeedPerNotch = 1.25f;

/// La distance de l'œil au pivot. Le plancher empêche l'orbite de s'effondrer sur son centre ; le
/// plafond tient une vallée de 512 m avec de la marge.
inline constexpr float MinPivotDistance = 0.05f;
inline constexpr float MaxPivotDistance = 1.0e6f;

/// Ce que la caméra de l'éditeur garde d'une image à l'autre. Les angles sont **la source**, comme
/// `scene::FpsController` : retrouver un lacet et un tangage depuis une matrice serait ambigu.
/// Le pivot, le point que l'orbite et le zoom gardent, n'est pas stocké : c'est le point à
/// `pivotDistance` devant l'œil (`pivotOf`).
struct EditorCamera
{
    glm::vec3 position{0.0f, 2.0f, 6.0f}; ///< L'œil.
    float yawDegrees = 0.0f;              ///< 0 : vers −Z ; croît vers la gauche (`scene`).
    float pitchDegrees = 0.0f;            ///< Positif vers le haut, dans ±`MaxEditorPitchDegrees`.
    float pivotDistance = 6.0f;
    float flySpeed = DefaultFlySpeed;
};

/// Le tangage borné à ±89°. Un NaN ressort à 0, car `std::clamp` rend NaN pour NaN : il se
/// propagerait dans l'avant, puis dans la position, et la vue resterait vide pour de bon.
[[nodiscard]] float clampEditorPitch(float pitchDegrees);

/// Le lacet ramené entre −180° et 180° : après des heures de vol il ne croîtrait pas sans fin, et
/// la précision d'un `float` ne tournerait plus. Un NaN ou un infini ressort à 0.
[[nodiscard]] float wrapYawDegrees(float yawDegrees);

/// La vitesse de vol bornée à [`MinFlySpeed`, `MaxFlySpeed`] ; un NaN rend la vitesse par défaut.
[[nodiscard]] float clampFlySpeed(float speed);

/// L'avant unitaire de la caméra, tangage compris : vers où elle regarde.
[[nodiscard]] glm::vec3 viewDirectionOf(const EditorCamera& camera);

/// La droite de la caméra, à plat : le regard n'a pas de roulis.
[[nodiscard]] glm::vec3 rightOf(const EditorCamera& camera);

/// Le haut de l'écran, incliné par le tangage : ce que le pan suit, contrairement à la verticale du
/// monde que suivent les touches Monter et Descendre.
[[nodiscard]] glm::vec3 upOf(const EditorCamera& camera);

/// Le pivot : le point devant l'œil que l'orbite entoure et que le zoom rapproche.
[[nodiscard]] glm::vec3 pivotOf(const EditorCamera& camera);

/// La vitesse après `notches` crans de molette (positifs : plus vite), la molette en vol.
[[nodiscard]] EditorCamera scaleFlySpeed(EditorCamera camera, float notches);

} // namespace levain::editor
