#pragma once

// Charger un modèle glTF dans le monde et sur le GPU, en un appel (ADR-0029, point 6).

#include <array>
#include <filesystem>
#include <optional>
#include <string>

#include <flecs.h>

#include "levain/app/app.hpp"
#include "levain/assets/asset_id.hpp"
#include "levain/assets/gltf.hpp"
#include "levain/core/error.hpp"
#include "levain/scene/components.hpp"

namespace levain::app
{

/// La locomotion d'un modèle skinné (#118) : ses clips de repos, de marche et de course, par leurs
/// noms, et les vitesses auxquelles la marche et la course se jouent sans que les pieds glissent.
/// Les vitesses viennent du programme : `app` ne lit pas les réglages du plugin `character`.
struct LocomotionClips
{
    std::array<std::string, 3> names; ///< Repos, marche, course.
    float walkSpeed = 1.0f;
    float runSpeed = 3.0f;
};

/// Un modèle à charger : son fichier, d'une racine d'assets déjà scannée, sa place, et pour un
/// modèle skinné, le clip qu'il joue en boucle ou sa locomotion.
struct ModelLoad
{
    std::filesystem::path path;
    scene::Transform placement;
    std::string name = "model"; ///< Le nom de son entité racine.
    std::optional<std::string> clip;
    std::optional<LocomotionClips> locomotion;
};

/// Ce que le programme garde d'un modèle chargé : son entité racine, son GUID, et le modèle lu,
/// dont il peut tirer autre chose (une collision de décor, ADR-0028).
struct LoadedModel
{
    flecs::entity root;
    assets::AssetId id;
    const assets::Model* model = nullptr;
};

/// Lit le modèle, l'envoie au GPU avec ses matériaux, puis l'instancie dans le monde, à sa place.
/// `app` le dessine dès lors, comme toute entité qui porte un `MeshRef`. Les noms de clips se
/// vérifient avant tout travail GPU : un échec plus tard aurait envoyé meshes et textures pour
/// rien. Un modèle skinné déjà chargé est refusé : l'animation est rangée par asset (ADR-0029).
[[nodiscard]] core::Result<LoadedModel> loadModel(App& app, const ModelLoad& load);

} // namespace levain::app
