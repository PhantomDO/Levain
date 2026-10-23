#include "levain/editor/hierarchy.hpp"

#include <algorithm>
#include <bit>
#include <cfloat>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <string_view>

#include "levain/core/log.hpp"
#include "levain/editor/structure.hpp"
#include "levain/editor/window_menu.hpp"
#include "levain/scene/snapshot.hpp"
#include "levain/ui/flags.hpp"
#include "levain/ui/style.hpp"
#include "levain/ui/tr.hpp"

namespace levain::editor
{

namespace
{

/// Le nom de l'entité, ou son index si elle n'en a pas.
std::string labelOf(flecs::entity entity)
{
    const char* name = entity.name().c_str();
    return name != nullptr && *name != '\0'
               ? std::string{name}
               : std::format("#{}", static_cast<std::uint32_t>(entity.id()));
}

/// Un enfant que la hiérarchie montre : rangé par `flecs::Parent`, placé, de la scène. Un enfant
/// par `ChildOf` seul (créé par son chemin, `a::b`) n'en est pas un : sans `flecs::Parent`, la
/// requête des racines le montre déjà, et deux lignes pousseraient le même `pushEntityId`.
bool isShownChild(const flecs::table& table)
{
    return table.has<flecs::Parent>() && table.has<scene::Transform>() &&
           !scene::isEngineInternal(table);
}

/// Un singleton du moteur plutôt que de la scène : un module, dont flecs garde l'instance sur sa
/// propre entité, ou un composant de flecs lui-même (`flecs.core.Component` se décrit).
bool isEngineSingleton(flecs::entity component)
{
    if (component.has(flecs::Module))
    {
        return true;
    }
    for (flecs::entity scope = component.parent(); scope; scope = scope.parent())
    {
        if (scope == flecs::Flecs)
        {
            return true;
        }
    }
    return false;
}

void appendRow(const Hierarchy& hierarchy, flecs::entity entity, int depth,
               std::vector<HierarchyRow>& rows)
{
    rows.push_back({.entity = entity, .depth = depth});
    if (!hierarchy.open.contains(entity))
    {
        return;
    }
    // Les enfants d'un `flecs::Parent`, dans l'ordre où flecs les tient (ADR-0015). Ceux d'une
    // même table partagent la décision : un test par table, pas par enfant.
    const ecs_table_t* lastTable = nullptr;
    bool shown = false;
    entity.children(
        [&](flecs::entity child)
        {
            const flecs::table table = child.table();
            if (table.get_table() != lastTable)
            {
                lastTable = table.get_table();
                shown = isShownChild(table);
            }
            if (shown)
            {
                appendRow(hierarchy, child, depth + 1, rows);
            }
        });
}

void drawSingletons(const flecs::world& world, flecs::entity_t& selected)
{
    if (!ImGui::TreeNode(ui::labelOf("Singletons").c_str()))
    {
        return;
    }
    for (const flecs::entity_t singleton : singletonsOf(world))
    {
        pushEntityId(singleton);
        const int accent = ui::pushSelectedRowColors(singleton == selected);
        if (ImGui::Selectable(labelOf(world.entity(singleton)).c_str(), singleton == selected))
        {
            selected = singleton;
        }
        ImGui::PopStyleColor(accent);
        ImGui::PopID();
    }
    ImGui::TreePop();
}

/// Le texte du champ de renommage : le nom de l'entité, coupé à la taille du tampon.
void fillRenameText(RenameField& field, std::string_view name)
{
    const std::size_t length = std::min(name.size(), field.text.size() - 1);
    std::ranges::copy(name.substr(0, length), field.text.begin());
    field.text.at(length) = '\0';
    field.edited = false;
}

/// Ce que le champ a à dire de ce qui est tapé : vide si c'est bon.
std::string refusalOfTyped(const flecs::world& world, const RenameField& field)
{
    return renameRefusalOf(world, field.entity, field.text.data()).value_or(std::string{});
}

/// Le champ de renommage, à la place du nom de la ligne. **Entrée ou un clic ailleurs valide,
/// Échap annule** ; un nom refusé (`renameRefusalOf`) ne change rien : le refus est dit sous le
/// champ, Entrée le laisse ouvert, un clic ailleurs l'abandonne en le disant à la console. Un nom
/// inchangé ne pose aucune demande. Rien n'est écrit ici : la demande part dans `requests`.
void drawRenameField(const flecs::world& world, Hierarchy& hierarchy,
                     std::vector<EditorRequest>& requests)
{
    RenameField& field = hierarchy.rename;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (field.justOpened)
    {
        ImGui::SetKeyboardFocusHere();
        ImGui::SetScrollHereY();
    }
    const bool entered = ImGui::InputText(
        "##renommer", field.text.data(), field.text.size(),
        ui::withFlags(ImGuiInputTextFlags_EnterReturnsTrue, ImGuiInputTextFlags_AutoSelectAll));
    const ImVec2 below{ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y};
    const bool active = ImGui::IsItemActive();
    field.edited = field.edited || ImGui::IsItemEdited();
    // Un texte non tapé est le nom d'aujourd'hui (peut-être coupé) : `beginRename` l'a déjà
    // accepté.
    field.refusal = field.edited ? refusalOfTyped(world, field) : std::string{};
    if (!field.refusal.empty())
    {
        ImGui::SetNextWindowPos(below);
        ImGui::BeginTooltip();
        ImGui::TextColored({1.0f, 0.45f, 0.4f, 1.0f}, "%s", field.refusal.c_str());
        ImGui::EndTooltip();
    }
    if (field.justOpened)
    {
        field.justOpened = !active; // le focus se prend à l'image suivante
        return;
    }
    const bool escaped = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    if (active && !entered)
    {
        return; // en cours de frappe
    }
    if (escaped)
    {
        cancelRename(hierarchy);
        return;
    }
    if (entered && !field.refusal.empty())
    {
        field.justOpened = true; // refusé : le champ reste ouvert, tout est resélectionné
        return;
    }
    if (!field.refusal.empty())
    {
        core::log("editor", core::LogLevel::Warning, "{}",
                  ui::trf("Renommer : {}", field.refusal)); // un clic ailleurs abandonne
    }
    else
    {
        const char* current = ecs_get_name(world, field.entity);
        if (field.edited &&
            std::string_view{field.text.data()} != (current != nullptr ? current : ""))
        {
            requests.push_back({RequestKind::Rename, field.entity, field.text.data()});
        }
    }
    cancelRename(hierarchy);
}

/// Les entrées du menu d'une ligne : créer un enfant, renommer, supprimer. Rien d'écrit ici.
void drawRowMenu(const flecs::world& world, Hierarchy& hierarchy, const HierarchyRow& row,
                 std::vector<EditorRequest>& requests)
{
    hierarchy.menu.open = true;
    if (ImGui::MenuItem(ui::labelOf("Créer un enfant").c_str()))
    {
        requests.push_back({RequestKind::Create, row.entity});
    }
    hierarchy.menu.create = centerOfLastItem();
    if (ImGui::MenuItem(ui::labelOf("Renommer").c_str()))
    {
        if (const auto refusal = beginRename(hierarchy, world, row.entity))
        {
            core::log("editor", core::LogLevel::Warning, "{}", ui::trf("Renommer : {}", *refusal));
        }
    }
    hierarchy.menu.rename = centerOfLastItem();
    if (ImGui::MenuItem(ui::labelOf("Supprimer").c_str()))
    {
        requests.push_back({RequestKind::Delete, row.entity});
    }
    hierarchy.menu.remove = centerOfLastItem();
}

void drawRow(const flecs::world& world, Hierarchy& hierarchy, const HierarchyRow& row,
             flecs::entity_t& selected, std::vector<EditorRequest>& requests)
{
    const bool renaming = hierarchy.rename.entity == row.entity;
    // Pas de TreePush : l'arbre est déjà aplati en lignes, la profondeur donne le retrait.
    ImGuiTreeNodeFlags flags =
        ui::withFlags(ImGuiTreeNodeFlags_NoTreePushOnOpen, ImGuiTreeNodeFlags_OpenOnArrow);
    // Le champ de renommage prend la place du nom : la ligne ne couvre pas toute la largeur, pour
    // que le champ, posé par-dessus, ne se dispute pas la souris avec elle.
    flags = renaming ? flags : ui::withFlags(flags, ImGuiTreeNodeFlags_SpanAvailWidth);
    if (!hasShownChildren(world, row.entity))
    {
        flags = ui::withFlags(flags, ImGuiTreeNodeFlags_Leaf);
    }
    if (row.entity == selected)
    {
        flags = ui::withFlags(flags, ImGuiTreeNodeFlags_Selected);
    }
    // Le retrait de cette ligne seule, sans `Indent`, dont le zéro d'une racine vaudrait
    // l'espacement par défaut : ImGui revient à la marge à la ligne suivante.
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                         (static_cast<float>(row.depth) * ImGui::GetStyle().IndentSpacing));
    pushEntityId(row.entity);
    ImGui::SetNextItemOpen(hierarchy.open.contains(row.entity));
    const int accent = ui::pushSelectedRowColors(row.entity == selected);
    ImGui::TreeNodeEx("##entité", flags, "%s",
                      renaming ? "" : labelOf(world.entity(row.entity)).c_str());
    ImGui::PopStyleColor(accent);
    if (ImGui::IsItemToggledOpen())
    {
        if (hierarchy.open.erase(row.entity) == 0)
        {
            hierarchy.open.insert(row.entity);
        }
    }
    else
    {
        if (ImGui::IsItemClicked())
        {
            selected = row.entity;
        }
        // Un clic droit choisit la ligne avant d'ouvrir son menu (Unity et Unreal le font).
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        {
            selected = row.entity;
        }
        // Le double-clic choisit aussi la ligne, puis demande le cadrage : Unreal cadre au
        // double-clic dans l'Outliner (supposé, ADR-0036 [E3]). Il ne renomme pas : F2 et le menu
        // le font (GESTES.md, geste 25 couvert).
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            selected = row.entity;
            hierarchy.frameRequested = true;
        }
    }
    hierarchy.rects[row.entity] = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
    if (ImGui::BeginPopupContextItem("##menu-ligne"))
    {
        drawRowMenu(world, hierarchy, row, requests);
        ImGui::EndPopup();
    }
    if (renaming)
    {
        drawRenameField(world, hierarchy, requests);
    }
    ImGui::PopID();
}

/// Le menu du vide de la fenêtre : créer une racine. Pas sur une ligne (`NoOpenOverItems`).
void drawEmptySpaceMenu(Hierarchy& hierarchy, std::vector<EditorRequest>& requests)
{
    constexpr ImGuiPopupFlags Flags =
        ui::withFlags(ImGuiPopupFlags_MouseButtonRight, ImGuiPopupFlags_NoOpenOverItems);
    if (!ImGui::BeginPopupContextWindow("##menu-vide", Flags))
    {
        return;
    }
    hierarchy.menu.open = true;
    if (ImGui::MenuItem(ui::labelOf("Créer une entité").c_str()))
    {
        requests.push_back({RequestKind::Create, 0});
    }
    hierarchy.menu.create = centerOfLastItem();
    ImGui::EndPopup();
}

} // namespace

Hierarchy createHierarchy(const flecs::world& world)
{
    // `flecs::Parent` est un composant de la table, comme les autres : `without` écarte les
    // enfants table par table (https://www.flecs.dev/flecs/md_docs_2HierarchiesManual.html,
    // section « Parent storage details »).
    //
    // **Triée par index sans composant** (`order_by(0, …)`) : flecs ne retrie que si les tables de
    // la requête ont changé (une entité y entre ou en sort), non à chaque `Transform` écrit, ce que
    // le jeu fait à chaque pas (https://www.flecs.dev/flecs/md_docs_2Queries.html, « Sorting »).
    return {.roots = world.query_builder<const scene::Transform>()
                         .without<flecs::Parent>()
                         .order_by(0, scene::byEntityIndex)
                         .build(),
            .open = {},
            .rows = {},
            .frameRequested = false,
            .rects = {},
            .rename = {},
            .menu = {}};
}

void listHierarchyRows(const Hierarchy& hierarchy, std::vector<HierarchyRow>& rows)
{
    rows.clear();
    hierarchy.roots.run(
        [&](flecs::iter& it)
        {
            while (it.next())
            {
                if (scene::isEngineInternal(it.table()))
                {
                    continue;
                }
                for (const std::size_t i : it)
                {
                    appendRow(hierarchy, it.entity(i), 0, rows);
                }
            }
        });
}

bool hasShownChildren(const flecs::world& world, flecs::entity_t entity)
{
    ecs_iter_t it = ecs_children(world, entity);
    while (ecs_children_next(&it))
    {
        for (const flecs::entity_t child :
             std::span{it.entities, static_cast<std::size_t>(it.count)})
        {
            if (isShownChild(world.entity(child).table()))
            {
                // Un itérateur arrêté avant la fin libère ses ressources à la main
                // (https://www.flecs.dev/flecs/group__iterator.html, `ecs_iter_fini`).
                ecs_iter_fini(&it);
                return true;
            }
        }
    }
    return false;
}

std::vector<flecs::entity_t> singletonsOf(const flecs::world& world)
{
    std::vector<flecs::entity_t> singletons;
    world.each<flecs::Component>(
        [&](flecs::entity component, const flecs::Component&)
        {
            // flecs range un singleton sur l'entité de son composant, qui se porte donc lui-même
            // (https://www.flecs.dev/flecs/md_docs_2EntitiesComponents.html, « Singletons »).
            if (component.has(component) && !isEngineSingleton(component))
            {
                singletons.push_back(component);
            }
        });
    return singletons;
}

void revealInHierarchy(Hierarchy& hierarchy, flecs::entity entity)
{
    while (const auto* parent = entity.try_get<flecs::Parent>())
    {
        entity = entity.world().entity(parent->value);
        hierarchy.open.insert(entity);
    }
}

void pushEntityId(flecs::entity_t entity)
{
    // Les 64 bits, génération comprise : ImGui hache le pointeur entier.
    ImGui::PushID(std::bit_cast<const void*>(entity));
}

std::optional<std::string> beginRename(Hierarchy& hierarchy, const flecs::world& world,
                                       flecs::entity_t entity)
{
    const char* name = ecs_is_alive(world, entity) ? ecs_get_name(world, entity) : nullptr;
    // Le nom d'aujourd'hui comme nom voulu : un nom égal au sien n'est jamais refusé, seule
    // l'entité peut l'être (un nœud généré, hors de la scène).
    if (const auto refusal = renameRefusalOf(world, entity, name != nullptr ? name : ""))
    {
        return refusal;
    }
    RenameField& field = hierarchy.rename;
    field.entity = entity;
    field.justOpened = true;
    field.refusal.clear();
    fillRenameText(field, name != nullptr ? name : "");
    revealInHierarchy(hierarchy, world.entity(entity));
    return std::nullopt;
}

void cancelRename(Hierarchy& hierarchy)
{
    hierarchy.rename = {};
}

void drawHierarchy(const flecs::world& world, Hierarchy& hierarchy, flecs::entity_t& selected,
                   std::vector<EditorRequest>& requests, ImGuiID dock, bool* open)
{
    listHierarchyRows(hierarchy, hierarchy.rows);
    hierarchy.frameRequested = false;
    hierarchy.rects.clear();
    hierarchy.menu = {};
    if (hierarchy.rename.entity != 0 && !world.is_alive(hierarchy.rename.entity))
    {
        cancelRename(hierarchy); // l'entité a disparu (annuler, supprimer) : plus de champ
    }
    if (open != nullptr && !*open)
    {
        cancelRename(
            hierarchy); // un champ qu'on ne voit pas ne reste pas ouvert (F2, Vue focalisée)
        return;
    }
    ImGui::SetNextWindowDockID(dock, ImGuiCond_FirstUseEver);
    if (ImGui::Begin(ui::labelOf(HierarchyWindow).c_str(), open))
    {
        drawSingletons(world, selected);
        // Ses lignes ont le retrait des racines qui suivent : le trait marque où le nœud s'arrête.
        ImGui::Separator();
        // Une ligne par racine, sans widget : ImGui ne dessine que les lignes visibles.
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(hierarchy.rows.size()));
        if (hierarchy.rename.justOpened)
        {
            // Le champ à peine ouvert se montre, même si sa ligne était hors de la fenêtre.
            const auto row =
                std::ranges::find(hierarchy.rows, hierarchy.rename.entity, &HierarchyRow::entity);
            if (row != hierarchy.rows.end())
            {
                clipper.IncludeItemByIndex(static_cast<int>(row - hierarchy.rows.begin()));
            }
        }
        while (clipper.Step())
        {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
            {
                drawRow(world, hierarchy, hierarchy.rows.at(static_cast<std::size_t>(row)),
                        selected, requests);
            }
        }
        drawEmptySpaceMenu(hierarchy, requests);
    }
    ImGui::End();
}

} // namespace levain::editor
