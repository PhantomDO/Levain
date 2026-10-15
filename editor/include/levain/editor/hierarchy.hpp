#pragma once

// La hiérarchie de l'éditeur (ADR-0034, « Les panneaux ») : les entités placées (`Transform`),
// rangées par `flecs::Parent` (ADR-0015), et un nœud des singletons. En M7.2, elle sélectionne
// seulement. Ses lignes se calculent sans ImGui : les tests les lisent.

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <flecs.h>
#include <imgui.h>

#include "levain/scene/components.hpp"

namespace levain::editor
{

/// La clé de catalogue du titre de la fenêtre.
inline constexpr const char* HierarchyWindow = "Hiérarchie";

/// Une ligne de la hiérarchie : une entité, à sa profondeur dans l'arbre.
struct HierarchyRow
{
    flecs::entity_t entity = 0;
    int depth = 0;
};

/// Où une ligne a été dessinée à la dernière image, en pixels de l'image.
struct RowRect
{
    ImVec2 min{};
    ImVec2 max{};
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
    /// Le double-clic sur une ligne a eu lieu à cette image : l'entité de la ligne est choisie et
    /// l'éditeur la cadre (ADR-0036, décision 6). Remis à faux au début de chaque dessin.
    bool frameRequested = false;
    /// Où les lignes dessinées à la dernière image sont (celles que le clipper a montrées) : un
    /// test vise une ligne par son rectangle, et la hiérarchie qui défile jusqu'à la sélection
    /// (M7.4) en aura besoin.
    std::unordered_map<flecs::entity_t, RowRect> rects;
};

[[nodiscard]] Hierarchy createHierarchy(const flecs::world& world);

/// Vide `rows`, puis y range chaque racine et, sous un nœud ouvert, ses enfants placés, dans
/// l'ordre où flecs les tient : une ligne par racine à chaque image, sans widget. Sa mémoire
/// ressert d'une image à l'autre.
void listHierarchyRows(const Hierarchy& hierarchy, std::vector<HierarchyRow>& rows);

/// Si `entity` a un enfant que la hiérarchie listerait, pour sa flèche : le même filtre que les
/// lignes, arrêté au premier.
[[nodiscard]] bool hasShownChildren(const flecs::world& world, flecs::entity_t entity);

/// Les composants qui portent leur propre valeur : flecs 4 range un singleton sur l'entité de son
/// composant, qu'aucune requête sur `Transform` ne voit. Sans les modules, dont flecs garde
/// l'instance sur leur entité, ni les composants de flecs.
[[nodiscard]] std::vector<flecs::entity_t> singletonsOf(const flecs::world& world);

/// Ouvre les ancêtres de `entity`, pour qu'elle se voie dans l'arbre (`--select`).
void revealInHierarchy(Hierarchy& hierarchy, flecs::entity entity);

/// L'identifiant ImGui d'une entité, poussé sur la pile d'ImGui : l'identifiant complet, jamais le
/// nom, qui se répète (« cube » sous deux parents), ni l'index seul, que flecs recycle.
void pushEntityId(flecs::entity_t entity);

/// La fenêtre « Hiérarchie », ancrée au premier affichage au nœud `dock` de la disposition d'`app`.
/// Un clic choisit l'entité (`selected`), la flèche ouvre ou ferme son nœud.
///
/// `open` : le drapeau que la croix de la fenêtre et le menu « Fenêtre » partagent ; faux, rien
/// n'est dessiné (les lignes sont tout de même listées, la CI compte les entités).
void drawHierarchy(const flecs::world& world, Hierarchy& hierarchy, flecs::entity_t& selected,
                   ImGuiID dock, bool* open = nullptr);

} // namespace levain::editor
