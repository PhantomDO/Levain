#include "levain/editor/hierarchy.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>

namespace levain::editor
{

namespace
{

constexpr const char* HierarchyWindow = "Hiérarchie";

/// Les drapeaux d'ImGui sont des `int` : les combiner en non signé, comme des bits qu'ils sont
/// (comme `withFlags` d'engine/ui/src/context.cpp).
int withFlags(int flags, int added)
{
    return static_cast<int>(static_cast<unsigned>(flags) | static_cast<unsigned>(added));
}

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
    return table.has<flecs::Parent>() && table.has<scene::Transform>() && !isEngineInternal(table);
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

void drawRow(const flecs::world& world, Hierarchy& hierarchy, const HierarchyRow& row,
             flecs::entity_t& selected)
{
    // Pas de TreePush : l'arbre est déjà aplati en lignes, la profondeur donne le retrait.
    ImGuiTreeNodeFlags flags =
        withFlags(withFlags(ImGuiTreeNodeFlags_NoTreePushOnOpen, ImGuiTreeNodeFlags_OpenOnArrow),
                  ImGuiTreeNodeFlags_SpanAvailWidth);
    if (!hasShownChildren(world, row.entity))
    {
        flags = withFlags(flags, ImGuiTreeNodeFlags_Leaf);
    }
    if (row.entity == selected)
    {
        flags = withFlags(flags, ImGuiTreeNodeFlags_Selected);
    }
    // Le retrait de cette ligne seule, sans `Indent`, dont le zéro d'une racine vaudrait
    // l'espacement par défaut : ImGui revient à la marge à la ligne suivante.
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                         (static_cast<float>(row.depth) * ImGui::GetStyle().IndentSpacing));
    pushEntityId(row.entity);
    ImGui::SetNextItemOpen(hierarchy.open.contains(row.entity));
    ImGui::TreeNodeEx("entité", flags, "%s", labelOf(world.entity(row.entity)).c_str());
    if (ImGui::IsItemToggledOpen())
    {
        if (hierarchy.open.erase(row.entity) == 0)
        {
            hierarchy.open.insert(row.entity);
        }
    }
    else if (ImGui::IsItemClicked())
    {
        selected = row.entity;
    }
    ImGui::PopID();
}

} // namespace

Hierarchy createHierarchy(const flecs::world& world)
{
    // `flecs::Parent` est un composant de la table, comme les autres : `without` écarte les
    // enfants table par table (https://www.flecs.dev/flecs/md_docs_2HierarchiesManual.html,
    // section « Parent storage details »).
    return {.roots = world.query_builder<const scene::Transform>().without<flecs::Parent>().build(),
            .open = {},
            .rows = {}};
}

bool isEngineInternal(const flecs::table& table)
{
    return table.has(flecs::Module) || table.has(flecs::System) || table.has(flecs::Observer) ||
           table.has(flecs::Query) || table.has<flecs::Component>() || table.has(flecs::Prefab);
}

void listHierarchyRows(const Hierarchy& hierarchy, std::vector<HierarchyRow>& rows)
{
    rows.clear();
    hierarchy.roots.run(
        [&](flecs::iter& it)
        {
            while (it.next())
            {
                if (isEngineInternal(it.table()))
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

void drawHierarchy(const flecs::world& world, Hierarchy& hierarchy, flecs::entity_t& selected,
                   ImGuiID dock)
{
    listHierarchyRows(hierarchy, hierarchy.rows);
    ImGui::SetNextWindowDockID(dock, ImGuiCond_FirstUseEver);
    if (ImGui::Begin(HierarchyWindow))
    {
        // Une ligne par racine, sans widget : ImGui ne dessine que les lignes visibles.
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(hierarchy.rows.size()));
        while (clipper.Step())
        {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
            {
                drawRow(world, hierarchy, hierarchy.rows.at(static_cast<std::size_t>(row)),
                        selected);
            }
        }
    }
    ImGui::End();
}

} // namespace levain::editor
