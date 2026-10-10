#pragma once

// La caméra de l'éditeur (ADR-0036, décision 6) : un état, et des fonctions libres qui rendent le
// suivant (ADR-0011). Ce n'est pas une entité : elle ne vit ni dans la scène ni dans le pas fixe,
// et ne s'enregistre jamais. Ici aucune entrée n'est lue : les gestes (morceau 11) donnent des
// deltas en pixels, en crans de molette ou en unités, la durée de l'image, des multiplicateurs ; ce
// fichier ne connaît ni SDL, ni ImGui, ni flecs.

#include <glm/glm.hpp>

#include "levain/render/camera.hpp"
#include "levain/render/culling.hpp"

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

/// Le champ vertical se règle dans le même intervalle qu'un `CameraLens` (app/camera.hpp) : hors de
/// lui, la projection et le pan divisent par zéro ou s'inversent.
inline constexpr float DefaultFovDegrees = 60.0f;
inline constexpr float MinFovDegrees = 1.0f;
inline constexpr float MaxFovDegrees = 179.0f;

/// Les plans de découpe suivent le cadrage, comme le *Dynamic Clipping* d'Unity (documenté,
/// ADR-0036 [4]) : le lointain par défaut de `render::Camera` est 100, pour une vallée de 512 m. Le
/// rapport lointain sur proche est borné des deux côtés : trop grand, le tampon de profondeur (D32,
/// profondeur standard : `mesh_pass.hpp`) n'a plus la précision de départager des surfaces voisines
/// et lointaines, qui scintillent ; sous 2, `app::farBeyondNear` rehausserait le lointain de toute
/// façon.
inline constexpr float MinClipRatio = 2.0f;
inline constexpr float MaxClipRatio = 10000.0f;
inline constexpr float MinNearPlane = 0.01f;
inline constexpr float DefaultNearPlane = 0.1f;
inline constexpr float DefaultFarPlane = 1000.0f;

/// Un pixel de souris, en degrés de regard.
inline constexpr float LookDegreesPerPixel = 0.25f;

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
    float verticalFovDegrees = DefaultFovDegrees;
};

/// Le tangage borné à ±89°. Un NaN ressort à 0, car `std::clamp` rend NaN pour NaN : il se
/// propagerait dans l'avant, puis dans la position, et la vue resterait vide pour de bon.
[[nodiscard]] float clampEditorPitch(float pitchDegrees);

/// Le lacet ramené entre −180° et 180° : après des heures de vol il ne croîtrait pas sans fin, et
/// la précision d'un `float` ne tournerait plus. Un NaN ou un infini ressort à 0.
[[nodiscard]] float wrapYawDegrees(float yawDegrees);

/// La vitesse de vol bornée à [`MinFlySpeed`, `MaxFlySpeed`] ; un NaN rend la vitesse par défaut.
[[nodiscard]] float clampFlySpeed(float speed);

/// L'avant unitaire de la caméra, tangage compris : vers où elle regarde, et où `flyCamera` avance.
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

/// Ce que le vol lit en une image. `move.x` est celui de `FpsInput::move`, `move.z` son `move.y`
/// (l'avant), `move.y` son `up`.
struct FlyInput
{
    glm::vec2 lookPixels{0.0f};   ///< Le mouvement de la souris, en pixels d'écran : y vers le bas.
    glm::vec3 move{0.0f};         ///< x : la droite, y : monter (verticale du monde), z : l'avant.
    float speedMultiplier = 1.0f; ///< Maj : plus vite. Un négatif est ramené à 0.
};

/// Tourne le regard, puis avance de `seconds` à la vitesse de la caméra le long de l'avant (tangage
/// compris : regarder en haut et avancer monte), de la droite et de la verticale du monde. Une
/// diagonale ne va pas plus vite qu'une ligne droite. Aucune entrée : la caméra ne bouge pas, ni
/// ne tourne.
[[nodiscard]] EditorCamera flyCamera(EditorCamera camera, const FlyInput& input, float seconds);

/// Alt+clic gauche : tourne autour du pivot, qui ne bouge pas, à la même distance. `pixels` comme
/// `FlyInput::lookPixels`.
[[nodiscard]] EditorCamera orbitCamera(EditorCamera camera, glm::vec2 pixels);

/// Bouton du milieu : fait glisser la vue de `pixels` (y vers le bas) dans le plan de l'écran, sans
/// la tourner, de sorte que le point visé au pivot suive exactement le curseur, quelle que soit la
/// distance : une unité de monde par pixel vaut `2 · distance · tan(champ / 2) / hauteur`. Une
/// hauteur de fenêtre nulle ou invalide ne déplace rien.
[[nodiscard]] EditorCamera panCamera(EditorCamera camera, glm::vec2 pixels,
                                     float viewportHeightPixels);

/// Le plan proche et le plan lointain d'une image.
struct ClipPlanes
{
    float nearPlane = DefaultNearPlane;
    float farPlane = DefaultFarPlane;
};

/// Les plans qui contiennent `box` vue de `camera` : le lointain passe le coin le plus profond, le
/// proche reste devant le plus proche. `box` est ce qui doit **rester visible**, les bornes de la
/// scène, et non la boîte que F vient de cadrer : le cadrage n'agit que par la position de l'œil,
/// et les plans de la boîte cadrée mettraient le lointain à quelques mètres, la vallée derrière un
/// cube de 1 m disparaîtrait. Le rapport reste dans
/// [`MinClipRatio`, `MaxClipRatio`] : si la caméra est dans la boîte, ou si la scène est trop
/// profonde, le plan proche monte au plancher du rapport et rogne le premier plan plutôt que la
/// précision de profondeur. Une boîte entièrement derrière l'œil, ou inutilisable, donne les plans
/// par défaut.
[[nodiscard]] ClipPlanes clipPlanesFor(const EditorCamera& camera, const render::Box& box);

/// La caméra de l'éditeur qui regarde comme `camera`, car elle part de la caméra du jeu s'il y en a
/// une (ADR-0036, décision 6) : même œil, même regard (lacet et tangage tirés de `cible -
/// position`, le tangage borné à ±89° et un regard à la verticale gardant le lacet par défaut),
/// même champ.
/// **Pas la distance du pivot**, qui reste celle par défaut : la cible d'une caméra du jeu est un
/// point à une unité devant elle (`app::cameraFrom`), pas ce qu'elle regarde. Un œil ou une cible
/// qui n'est pas un nombre, ou une cible sur l'œil, laisse la position ou le regard par défaut.
[[nodiscard]] EditorCamera editorCameraFrom(const render::Camera& camera);

/// La caméra du rendu de l'éditeur : ce que reçoit `App::cameraOverride`. Sa cible est sur le
/// regard, **loin** : au moins aussi loin de l'œil que le pivot, que la position ne l'est de
/// l'origine, et qu'une unité. `lookAtRH` retrouve le regard par `cible - position` en `float`, et
/// une cible à une unité d'un œil lointain tournerait la vue de côté.
[[nodiscard]] render::Camera toRenderCamera(const EditorCamera& camera, const ClipPlanes& planes);

} // namespace levain::editor
