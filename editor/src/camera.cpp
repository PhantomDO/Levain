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

float clampPivotDistance(float distance)
{
    return std::isnan(distance) ? MinPivotDistance
                                : std::clamp(distance, MinPivotDistance, MaxPivotDistance);
}

/// L'avant et la droite à plat du lacet de la caméra, replié : l'état peut porter n'importe quel
/// lacet (un fichier de préférences, un champ tapé), et un NaN deviendrait l'avant, puis la
/// position du premier geste qui s'en sert.
scene::HorizontalBasis horizontalBasisOf(const EditorCamera& camera)
{
    return scene::horizontalBasisFrom(wrapYawDegrees(camera.yawDegrees));
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

} // namespace levain::editor
