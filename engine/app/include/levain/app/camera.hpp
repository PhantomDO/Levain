#pragma once

// La caméra du rendu (ADR-0029, point 8) : une entité comme les autres, qui porte un objectif. Le
// rendu regarde par l'unique entité qui en a un.

#include <optional>

#include <flecs.h>
#include <glm/glm.hpp>

#include "levain/core/error.hpp"
#include "levain/render/camera.hpp"
#include "levain/scene/components.hpp"

namespace levain::app
{

/// L'objectif d'une caméra : son champ vertical et ses plans de découpe. L'entité qui le porte
/// donne la position et le regard, par sa matrice monde (vers −Z, comme `render::Camera`).
/// C'est le `CameraComponent` d'Unreal, la `Camera3D` de Godot.
struct CameraLens
{
    float verticalFovDegrees = 60.0f;
    float nearPlane = 0.5f;
    float farPlane = 1000.0f;
};

/// Le plan lointain d'un objectif est au moins `MinFarOverNear` fois le proche.
inline constexpr float MinFarOverNear = 2.0f;

/// farBeyondNear : le plan lointain à projeter pour ce couple. L'inspecteur laisse taper n'importe
/// quel plan lointain, et `glm::perspectiveRH_ZO` divise par (near − far) : égaux, le viewport se
/// dessine d'une seule couleur, sans un mot ; en deçà, la projection s'inverse, et le découpage des
/// grappes de lumières et des cascades d'ombres calcule `pow(far / near, …)`. Un lointain trop
/// proche (ou NaN) est ramené à `MinFarOverNear` fois le proche, comme `clampPitch` remet des
/// bornes inversées dans l'ordre.
[[nodiscard]] float farBeyondNear(float nearPlane, float farPlane);

/// Les composants d'`app` (ADR-0034) : `CameraLens`, donnée d'auteur. `PlayerInput` n'est pas
/// décrit : il porte un pointeur et des conteneurs, et `app` le repose à chaque image. Appelée par
/// `prepareAppWorld` (app.hpp).
void describeAppComponents(flecs::world& world);

/// La caméra du rendu, d'un objectif et de la matrice monde de son entité : celle-ci est déjà
/// interpolée entre deux pas de simulation (ADR-0016). Lire le `Transform` ferait saccader le
/// regard dès que le rendu va plus vite que la simulation.
[[nodiscard]] render::Camera cameraFrom(const CameraLens& lens, const glm::mat4& world);

/// La caméra du rendu, celle de l'unique entité de `cameras` : celles qui portent un `CameraLens`
/// et une matrice monde (un `Transform` la leur donne). Aucune, ou plusieurs, est une erreur qui
/// les nomme : le rendu ne choisit pas au hasard (règle n°7). Une requête gardée : en créer une à
/// chaque image coûterait à chaque image.
[[nodiscard]] core::Result<render::Camera>
renderCameraOf(const flecs::query<const CameraLens, const scene::WorldTransform>& cameras);

/// La caméra de l'image (ADR-0036, décision 6) : celle que l'éditeur **impose**
/// (`App::cameraOverride`, un état de l'éditeur qui n'est pas une entité) sans consulter les
/// entités, donc zéro ou plusieurs `CameraLens` ne l'arrêtent pas ; sinon `renderCameraOf`, qui
/// refuse toujours zéro ou plusieurs. Le plan lointain imposé passe par `farBeyondNear`, comme
/// celui d'un objectif.
[[nodiscard]] core::Result<render::Camera>
renderCameraOr(const std::optional<render::Camera>& imposed,
               const flecs::query<const CameraLens, const scene::WorldTransform>& cameras);

} // namespace levain::app
