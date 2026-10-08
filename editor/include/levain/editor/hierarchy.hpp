#pragma once

// La hiérarchie de l'éditeur (ADR-0034, « Les panneaux ») : les entités placées (`Transform`),
// rangées par `flecs::Parent` (ADR-0015). En M7.2, elle sélectionne seulement. Ses lignes se
// calculent sans ImGui : les tests les lisent.

#include <unordered_set>
#include <vector>

#include <flecs.h>
#include <imgui.h>

#include "levain/scene/components.hpp"

namespace levain::editor
{

/// Une ligne de la hiérarchie : une entité, à sa profondeur dans l'arbre.
struct HierarchyRow
{
    flecs::entity_t entity = 0;
    int depth = 0;
};

/// Ce que la hiérarchie garde d'une image à l'autre.
struct Hierarchy
{
    /// Les racines : les entités placées sans `flecs::Parent`.
    flecs::query<const scene::Transform> roots;
    /// Les nœuds ouverts, par identifiant complet : seuls leurs enfants sont lus.
    std::unordered_set<flecs::entity_t> open;
    /// Les lignes de la dernière image : les racines, et les enfants des nœuds ouverts.
    std::vector<HierarchyRow> rows;
};

[[nodiscard]] Hierarchy createHierarchy(const flecs::world& world);

/// Une table d'entités de flecs plutôt que de la scène : modules, systèmes, observateurs,
/// requêtes, composants, prefabs. La hiérarchie les tait, table par table et non entité par
/// entité.
[[nodiscard]] bool isEngineInternal(const flecs::table& table);

/// Vide `rows`, puis y range chaque racine et, sous un nœud ouvert, ses enfants placés, dans
/// l'ordre où flecs les tient : une ligne par racine à chaque image, sans widget. Sa mémoire
/// ressert d'une image à l'autre.
void listHierarchyRows(const Hierarchy& hierarchy, std::vector<HierarchyRow>& rows);

/// Si `entity` a un enfant que la hiérarchie listerait, pour sa flèche : le même filtre que les
/// lignes, arrêté au premier.
[[nodiscard]] bool hasShownChildren(const flecs::world& world, flecs::entity_t entity);

/// Ouvre les ancêtres de `entity`, pour qu'elle se voie dans l'arbre (`--select`).
void revealInHierarchy(Hierarchy& hierarchy, flecs::entity entity);

/// L'identifiant ImGui d'une entité, poussé sur la pile d'ImGui : l'identifiant complet, jamais le
/// nom, qui se répète (« cube » sous deux parents), ni l'index seul, que flecs recycle.
void pushEntityId(flecs::entity_t entity);

/// La fenêtre « Hiérarchie », ancrée au premier affichage au nœud `dock` de la disposition d'`app`.
/// Un clic choisit l'entité (`selected`), la flèche ouvre ou ferme son nœud.
void drawHierarchy(const flecs::world& world, Hierarchy& hierarchy, flecs::entity_t& selected,
                   ImGuiID dock);

} // namespace levain::editor
