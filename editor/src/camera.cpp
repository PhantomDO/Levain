#include "levain/editor/camera.hpp"

#include <algorithm>
#include <cmath>

#include "levain/scene/camera_control.hpp"

namespace levain::editor
{

namespace
{

constexpr glm::vec3 WorldUp{0.0f, 1.0f, 0.0f};

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

/// L'avant et la droite à plat du lacet de la caméra, replié : l'état peut porter n'importe quel
/// lacet (un fichier de préférences, un champ tapé), et un NaN deviendrait l'avant, puis la
/// position du premier geste qui s'en sert.
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

} // namespace levain::editor
