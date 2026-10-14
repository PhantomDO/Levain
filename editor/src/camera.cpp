#include "levain/editor/camera.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "levain/scene/camera_control.hpp"

namespace levain::editor
{

namespace
{

constexpr glm::vec3 WorldUp{0.0f, 1.0f, 0.0f};

/// L'air que les plans de découpe laissent autour de la boîte : un coin pile sur un plan serait
/// découpé par l'arrondi d'un float.
constexpr float NearMargin = 0.9f;
constexpr float FarMargin = 1.05f;

/// La cible de la caméra du rendu n'est jamais plus près de l'œil que cela : voir
/// `lookTargetDistanceOf`.
constexpr float MinLookTargetDistance = 1.0f;

/// Un NaN ou un infini venu d'un delta (une souris qui rend n'importe quoi au changement de
/// fenêtre) vaut 0 : arrivé dans la position ou les angles, il y resterait, et la vue avec lui.
float finiteOrZero(float value)
{
    return std::isfinite(value) ? value : 0.0f;
}

glm::vec2 finiteOrZero(glm::vec2 value)
{
    return {finiteOrZero(value.x), finiteOrZero(value.y)};
}

glm::vec3 finiteOrZero(glm::vec3 value)
{
    return {finiteOrZero(value.x), finiteOrZero(value.y), finiteOrZero(value.z)};
}

bool isFinite(const glm::vec3& vector)
{
    return std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z);
}

/// Une boîte dont les bornes sont finies et dans l'ordre. Un infini serait « dans l'ordre » pour
/// `min <= max`, et un NaN ne l'est jamais : la comparaison seule ne suffit pas.
bool isUsableBox(const render::Box& box)
{
    return isFinite(box.min) && isFinite(box.max) && glm::all(glm::lessThanEqual(box.min, box.max));
}

float clampPivotDistance(float distance)
{
    return std::isnan(distance) ? MinPivotDistance
                                : std::clamp(distance, MinPivotDistance, MaxPivotDistance);
}

float clampVerticalFov(float degrees)
{
    return std::isnan(degrees) ? DefaultFovDegrees
                               : std::clamp(degrees, MinFovDegrees, MaxFovDegrees);
}

float largestMagnitude(const glm::vec3& vector)
{
    return std::max({std::abs(vector.x), std::abs(vector.y), std::abs(vector.z)});
}

/// À quelle distance de l'œil `toRenderCamera` pose la cible. `lookAtRH` retrouve le regard par
/// `cible - position`, en `float` : une cible à une unité d'un œil à 600 unités de l'origine hérite
/// de l'arrondi de la position (6·10⁻⁵), et au tangage de 89°, où l'avant n'a que 0,017
/// d'horizontale, la vue tourne autour de son axe d'un dixième de degré (2° à 2·10⁴ unités). Une
/// cible au moins aussi loin que la position l'est de l'origine ramène l'erreur à celle d'un
/// `float` entier ; le pivot la tient plus loin quand il l'est, et une unité l'empêche de tomber
/// sur l'œil.
float lookTargetDistanceOf(const EditorCamera& camera)
{
    return std::max({clampPivotDistance(camera.pivotDistance), largestMagnitude(camera.position),
                     MinLookTargetDistance});
}

/// L'avant et la droite à plat du lacet de la caméra, replié : l'état peut porter n'importe quel
/// lacet (un fichier de préférences, un champ tapé), et un NaN deviendrait la cible du rendu, puis
/// la position après le premier `orbitCamera` ou `framingOf`.
scene::HorizontalBasis horizontalBasisOf(const EditorCamera& camera)
{
    return scene::horizontalBasisFrom(wrapYawDegrees(camera.yawDegrees));
}

/// Le regard tourné de `pixels` : la souris à droite tourne à droite, et le lacet croît vers la
/// gauche (`scene`) ; la souris vers le bas regarde en bas, et l'écran compte y vers le bas.
EditorCamera turned(EditorCamera camera, glm::vec2 pixels)
{
    pixels = finiteOrZero(pixels);
    camera.yawDegrees = wrapYawDegrees(camera.yawDegrees - (pixels.x * LookDegreesPerPixel));
    camera.pitchDegrees = clampEditorPitch(camera.pitchDegrees - (pixels.y * LookDegreesPerPixel));
    return camera;
}

} // namespace

float clampEditorPitch(float pitchDegrees)
{
    return std::isnan(pitchDegrees)
               ? 0.0f
               : std::clamp(pitchDegrees, -MaxEditorPitchDegrees, MaxEditorPitchDegrees);
}

float wrapYawDegrees(float yawDegrees)
{
    return std::isfinite(yawDegrees) ? std::remainder(yawDegrees, 360.0f) : 0.0f;
}

float clampFlySpeed(float speed)
{
    return std::isnan(speed) ? DefaultFlySpeed : std::clamp(speed, MinFlySpeed, MaxFlySpeed);
}

glm::vec3 viewDirectionOf(const EditorCamera& camera)
{
    const float pitch = glm::radians(clampEditorPitch(camera.pitchDegrees));
    return horizontalBasisOf(camera).forward * std::cos(pitch) + WorldUp * std::sin(pitch);
}

glm::vec3 rightOf(const EditorCamera& camera)
{
    return horizontalBasisOf(camera).right;
}

glm::vec3 upOf(const EditorCamera& camera)
{
    return glm::cross(rightOf(camera), viewDirectionOf(camera));
}

glm::vec3 pivotOf(const EditorCamera& camera)
{
    return camera.position + (viewDirectionOf(camera) * clampPivotDistance(camera.pivotDistance));
}

EditorCamera scaleFlySpeed(EditorCamera camera, float notches)
{
    // Pour un très grand nombre de crans, 1,25ⁿ déborde vers l'infini ou s'évanouit vers 0 : la
    // vitesse (au moins `MinFlySpeed`, donc jamais 0 × ∞) retombe sur l'une ou l'autre borne par
    // `clampFlySpeed`, sans autre garde.
    camera.flySpeed = clampFlySpeed(clampFlySpeed(camera.flySpeed) *
                                    std::pow(FlySpeedPerNotch, finiteOrZero(notches)));
    return camera;
}

EditorCamera flyCamera(EditorCamera camera, const FlyInput& input, float seconds)
{
    camera = turned(camera, input.lookPixels);

    // Le regard d'abord : on avance là où l'on regarde maintenant, pas là où l'on regardait.
    const glm::vec3 move = finiteOrZero(input.move);
    const glm::vec3 wanted =
        (viewDirectionOf(camera) * move.z) + (rightOf(camera) * move.x) + (WorldUp * move.y);
    const float speed =
        clampFlySpeed(camera.flySpeed) * std::max(finiteOrZero(input.speedMultiplier), 0.0f);
    camera.position +=
        scene::normalizeOrZero(wanted) * speed * std::max(finiteOrZero(seconds), 0.0f);
    return camera;
}

EditorCamera orbitCamera(EditorCamera camera, glm::vec2 pixels)
{
    // Le pivot se lit avant de tourner, puis l'œil est reposé à la même distance, de l'autre côté
    // du nouvel avant : la distance tient par construction, pas par une correction après coup.
    const float distance = clampPivotDistance(camera.pivotDistance);
    const glm::vec3 pivot = pivotOf(camera);
    camera = turned(camera, pixels);
    camera.pivotDistance = distance;
    camera.position = pivot - (viewDirectionOf(camera) * distance);
    return camera;
}

EditorCamera panCamera(EditorCamera camera, glm::vec2 pixels, float viewportHeightPixels)
{
    if (!std::isfinite(viewportHeightPixels) || viewportHeightPixels <= 0.0f)
    {
        return camera;
    }
    const float halfFov = glm::radians(clampVerticalFov(camera.verticalFovDegrees)) * 0.5f;
    const float unitsPerPixel =
        2.0f * clampPivotDistance(camera.pivotDistance) * std::tan(halfFov) / viewportHeightPixels;
    pixels = finiteOrZero(pixels);
    // Le contenu suit le curseur, donc l'œil va à l'opposé : à gauche quand la souris va à droite,
    // en haut quand elle descend (l'écran compte y vers le bas).
    camera.position += ((upOf(camera) * pixels.y) - (rightOf(camera) * pixels.x)) * unitsPerPixel;
    return camera;
}

EditorCamera dollyCamera(EditorCamera camera, float notches)
{
    // Borné, car 1,25^n déborde d'un float vers l'infini, qui serait la position.
    notches = std::clamp(finiteOrZero(notches), -MaxDollyNotches, MaxDollyNotches);
    const float distance = clampPivotDistance(camera.pivotDistance);
    // De `distance` à `distance / 1,25^notches` : la même chose qu'un zoom exponentiel, sauf sous
    // `MinDollyStep`, où le pas garde sa taille d'une unité plutôt que de s'évanouir avec la
    // distance.
    float step =
        std::max(distance, MinDollyStep) * (1.0f - std::pow(DollyFactorPerNotch, -notches));
    // Le plafond arrête l'œil, il ne pousse pas le pivot : à `MaxPivotDistance` l'œil avançait du
    // pas entier (négatif, il recule) et traînait le pivot avec lui, à 10⁷ unités après une
    // molette libre ; revenu au plancher, il restait là, un pas de 0,2 s'y perdant dans l'ulp.
    step = std::max(step, distance - MaxPivotDistance);
    camera.position += viewDirectionOf(camera) * step;
    camera.pivotDistance = clampPivotDistance(distance - step);
    return camera;
}

EditorCamera framingOf(EditorCamera camera, const render::Box& box, float aspectRatio)
{
    if (!isUsableBox(box) || !std::isfinite(aspectRatio) || aspectRatio <= 0.0f)
    {
        return camera;
    }
    const glm::vec3 center = (box.min + box.max) * 0.5f;
    const float radius =
        std::max(glm::length(box.max - box.min) * 0.5f, MinFramedRadius) * FramingPadding;
    const float halfVertical = glm::radians(clampVerticalFov(camera.verticalFovDegrees)) * 0.5f;
    const float halfHorizontal = std::atan(std::tan(halfVertical) * aspectRatio);
    // Une sphère de rayon r tient dans un cône de demi-angle a quand l'œil est à r / sin(a) de son
    // centre ; le cône du plus étroit des deux demi-champs tient dans la pyramide de vue.
    const float distance = radius / std::sin(std::min(halfVertical, halfHorizontal));
    camera.pivotDistance = clampPivotDistance(distance);
    camera.position = center - (viewDirectionOf(camera) * camera.pivotDistance);
    return camera;
}

ClipPlanes clipPlanesFor(const EditorCamera& camera, const render::Box& box)
{
    if (!isUsableBox(box))
    {
        return {};
    }
    // La profondeur d'un coin, c'est sa distance au plan de l'œil, le long du regard : les plans de
    // découpe sont perpendiculaires à l'avant, pas des sphères autour de l'œil.
    const glm::vec3 forward = viewDirectionOf(camera);
    float nearest = std::numeric_limits<float>::max();
    float farthest = std::numeric_limits<float>::lowest();
    for (unsigned corner = 0; corner < 8U; ++corner)
    {
        const glm::vec3 point{(corner & 1U) != 0 ? box.max.x : box.min.x,
                              (corner & 2U) != 0 ? box.max.y : box.min.y,
                              (corner & 4U) != 0 ? box.max.z : box.min.z};
        const float depth = glm::dot(point - camera.position, forward);
        nearest = std::min(nearest, depth);
        farthest = std::max(farthest, depth);
    }
    if (farthest <= 0.0f)
    {
        return {}; // tout est derrière l'œil : il n'y a rien à contenir
    }
    const float farFirst = farthest * FarMargin;
    const float nearPlane = std::max({nearest * NearMargin, farFirst / MaxClipRatio, MinNearPlane});
    // Un proche aussi loin que le lointain (une boîte plate de face) : le lointain recule.
    return {.nearPlane = nearPlane, .farPlane = std::max(farFirst, nearPlane * MinClipRatio)};
}

EditorCamera editorCameraFrom(const render::Camera& camera)
{
    EditorCamera editor;
    if (isFinite(camera.position))
    {
        editor.position = camera.position;
    }
    editor.verticalFovDegrees = clampVerticalFov(glm::degrees(camera.verticalFovRadians));

    // Une cible qui n'est pas un nombre donne une direction NaN : le tangage ressort à 0
    // (`clampEditorPitch`) et `flat > 0` est faux, donc le lacet par défaut. Une cible sur l'œil
    // donne la direction nulle de `normalizeOrZero`, qui mène au même regard.
    const glm::vec3 direction = scene::normalizeOrZero(camera.target - editor.position);
    // Le tangage par `atan2` de la hauteur sur la longueur à plat, et non par `asin(y)` : `asin`
    // rend NaN au moindre dépassement de [−1, 1], `atan2` n'a pas ce piège.
    const float flat = std::hypot(direction.x, direction.z);
    editor.pitchDegrees = clampEditorPitch(glm::degrees(std::atan2(direction.y, flat)));
    // Pile à la verticale il n'y a pas de lacet : `atan2(-0, -0)` rendrait −180°, pas le défaut.
    if (flat > 0.0f)
    {
        // Le lacet croît vers la gauche et vaut 0 vers −Z : l'inverse de `horizontalBasisFrom`.
        editor.yawDegrees = wrapYawDegrees(glm::degrees(std::atan2(-direction.x, -direction.z)));
    }
    return editor;
}

render::Camera toRenderCamera(const EditorCamera& camera, const ClipPlanes& planes)
{
    return {.position = camera.position,
            .target = camera.position + (viewDirectionOf(camera) * lookTargetDistanceOf(camera)),
            .verticalFovRadians = glm::radians(clampVerticalFov(camera.verticalFovDegrees)),
            .nearPlane = planes.nearPlane,
            .farPlane = planes.farPlane};
}

} // namespace levain::editor
