#pragma once

// La hiérarchie de l'éditeur (ADR-0034, « Les panneaux ») : les entités placées (`Transform`),
// rangées par `flecs::Parent` (ADR-0015), et un nœud des singletons. Elle sélectionne, et depuis
// M7.3 (A6) elle crée, supprime et renomme : un clic droit ouvre le menu (« Créer un enfant »,
// « Renommer », « Supprimer » sur une ligne, « Créer une entité » sur le vide).
// **Elle n'écrit jamais dans le monde** : elle dessine entre `defer_begin`
// et `defer_end`, ses demandes sont mises en file (`EditorRequest`) et `applyEditorRequests` les
// exécute juste après. Équivalents : l'Outliner d'Unreal, la fenêtre Hierarchy d'Unity, le dock
// Scène de Godot. Ses lignes se calculent sans ImGui : les tests les lisent.

#include <array>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <flecs.h>
#include <imgui.h>

#include "levain/editor/undo.hpp"
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

/// Le champ de renommage d'une ligne (F2, « Renommer » du menu, une entité qu'on vient de créer).
struct RenameField
{
    /// L'entité en renommage, 0 s'il n'y en a pas : la ligne montre un champ à la place du nom.
    flecs::entity_t entity = 0;
    /// Ce qui est tapé, terminé par un zéro. 255 octets : un nom plus long est coupé.
    std::array<char, 256> text{};
    /// Le champ vient d'être ouvert : il prend le focus et sélectionne tout à sa première image.
    bool justOpened = false;
    /// Le texte a été tapé depuis l'ouverture. Sans cela, un nom plus long que le tampon (coupé à
    /// l'ouverture) serait renommé en sa version coupée par un simple Entrée.
    bool edited = false;
    /// Pourquoi ce qui est tapé est refusé (`renameRefusalOf`), vide si c'est bon : dit sous le
    /// champ. Un refus ne change rien dans le monde.
    std::string refusal;
};

/// Où le menu contextuel a dessiné ses entrées à la dernière image (les tests les visent), `open`
/// faux et les centres nuls quand il est fermé.
struct HierarchyMenuLayout
{
    bool open = false;
    ImVec2 create{}; ///< « Créer une entité » (le vide) ou « Créer un enfant » (une ligne).
    ImVec2 rename{}; ///< « Renommer » (une ligne).
    ImVec2 remove{}; ///< « Supprimer » (une ligne).
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
    RenameField rename;
    HierarchyMenuLayout menu;
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

/// beginRename : ouvre le champ de renommage de `entity` (F2, « Renommer », une entité qu'on vient
/// de créer), son nom dedans, révélée dans l'arbre. Rend le refus nommé, et n'ouvre rien, si
/// l'entité ne se renomme pas (un nœud `Generated`, une entité hors de la scène) : l'appelant le
/// dit à la console. Ne touche pas au monde.
[[nodiscard]] std::optional<std::string>
beginRename(Hierarchy& hierarchy, const flecs::world& world, flecs::entity_t entity);

/// Ferme le champ de renommage sans rien changer (Échap).
void cancelRename(Hierarchy& hierarchy);

/// La fenêtre « Hiérarchie », ancrée au premier affichage au nœud `dock` de la disposition d'`app`.
/// Un clic choisit l'entité (`selected`), la flèche ouvre ou ferme son nœud. Elle ne touche jamais
/// au monde (elle dessine dans le `defer`) : créer, supprimer et renommer sont des demandes
/// ajoutées à `requests`, que `applyEditorRequests` exécute.
///
/// `open` : le drapeau que la croix de la fenêtre et le menu « Fenêtre » partagent ; faux, rien
/// n'est dessiné (les lignes sont tout de même listées, la CI compte les entités).
void drawHierarchy(const flecs::world& world, Hierarchy& hierarchy, flecs::entity_t& selected,
                   std::vector<EditorRequest>& requests, ImGuiID dock, bool* open = nullptr);

} // namespace levain::editor
