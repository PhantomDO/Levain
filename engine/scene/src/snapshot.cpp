#include "levain/scene/snapshot.hpp"

#include <algorithm>
#include <cstdint>
#include <span>
#include <utility>

namespace levain::scene
{

namespace
{

std::uint32_t indexOf(flecs::entity_t entity)
{
    return static_cast<std::uint32_t>(entity);
}

/// Un identifiant du type qui n'est pas de l'état de la scène : ce que flecs tient lui-même (le
/// nom et le symbole `(Identifier, *)`, `flecs::Parent` rendu par le parent de l'enregistrement,
/// `(ParentDepth, n)` qu'il recalcule), et ce qu'un module marque `Transient`.
bool isTransientId(const flecs::world& world, flecs::id_t id)
{
    const flecs::entity_t first = ECS_IS_PAIR(id) ? ecs_pair_first(world, id) : id;
    const flecs::entity_t value = ecs_get_typeid(world, id);
    const flecs::entity_t transient = world.id<Transient>();
    return ecs_has_id(world, first, transient) ||
           (value != 0 && ecs_has_id(world, value, transient));
}

bool isKeptByFlecs(const flecs::world& world, flecs::id_t id)
{
    if (id == ecs_id(EcsParent))
    {
        return true;
    }
    return ECS_IS_PAIR(id) && (ecs_pair_first(world, id) == ecs_id(EcsIdentifier) ||
                               ecs_pair_first(world, id) == EcsParentDepth);
}

/// Les identifiants du type d'une entité que `keep` retient, triés pour se comparer d'un monde à
/// l'autre.
template <class Keep>
std::vector<flecs::entity_t> typeWhere(const flecs::world& world, flecs::entity_t entity, Keep keep)
{
    std::vector<flecs::entity_t> type;
    if (const ecs_type_t* all = ecs_get_type(world, entity))
    {
        for (const flecs::id_t id : std::span{all->array, static_cast<std::size_t>(all->count)})
        {
            if (!isKeptByFlecs(world, id) && keep(id))
            {
                type.push_back(id);
            }
        }
    }
    std::ranges::sort(type);
    return type;
}

/// Le type d'une entité sans ce qui n'est pas son état.
std::vector<flecs::entity_t> sceneTypeOf(const flecs::world& world, flecs::entity_t entity)
{
    return typeWhere(world, entity, [&](flecs::id_t id) { return !isTransientId(world, id); });
}

/// Le type de la valeur d'un identifiant du type (le composant, ou celui d'une paire à valeur) ; 0
/// pour une étiquette ou une paire sans valeur.
flecs::entity_t valueTypeOf(const flecs::world& world, flecs::id_t id)
{
    const flecs::entity_t type = ecs_get_typeid(world, id);
    return type != 0 && ecs_get_type_info(world, type)->size > 0 ? type : 0;
}

/// Les enfants dans l'ordre de flecs, la scène et les internes mêlés. `ecs_get_ordered_children`
/// n'existe que pour un parent de `flecs::Parent` : sur une entité sans enfant, ou dont les enfants
/// sont tous sous `ChildOf` (les observateurs d'un `onEnter`), flecs s'arrête sur une assertion
/// (src/entity.c:2520, vérifié sur la 4.1.6). On ne l'appelle donc que si un enfant porte
/// `flecs::Parent`.
std::vector<flecs::entity_t> orderedChildrenOf(const flecs::world& world, flecs::entity_t parent)
{
    std::vector<flecs::entity_t> all;
    bool ordered = false;
    ecs_iter_t it = ecs_children(world, parent);
    while (ecs_children_next(&it))
    {
        for (const flecs::entity_t child :
             std::span{it.entities, static_cast<std::size_t>(it.count)})
        {
            all.push_back(child);
            ordered = ordered || world.entity(child).table().has<flecs::Parent>();
        }
    }
    if (ordered)
    {
        const ecs_entities_t kids = ecs_get_ordered_children(world, parent);
        all.assign(kids.ids, kids.ids + kids.count);
    }
    return all;
}

struct ChildLists
{
    std::vector<flecs::entity_t> scene;
    std::vector<flecs::entity_t> internal;
};

/// Les enfants séparés entre la scène et les internes : la scène n'emploie que `Parent`, donc un
/// enfant qui n'est pas de la scène (un observateur sous `ChildOf`) est interne.
ChildLists childrenOf(const flecs::world& world, flecs::entity_t parent)
{
    ChildLists lists;
    for (const flecs::entity_t child : orderedChildrenOf(world, parent))
    {
        (isSceneEntity(world, child) ? lists.scene : lists.internal).push_back(child);
    }
    return lists;
}

/// L'état d'une entité de la scène tel que le monde le tient : tout l'enregistrement sauf les
/// valeurs, que seul `takeSnapshot` copie.
EntityRecord readRecord(const flecs::world& world, flecs::entity_t entity)
{
    EntityRecord record;
    record.id = entity;
    if (const char* name = ecs_get_name(world, entity))
    {
        record.name = name;
    }
    record.parent = ecs_get_parent(world, entity);
    record.type = sceneTypeOf(world, entity);
    ChildLists children = childrenOf(world, entity);
    record.children = std::move(children.scene);
    record.internalChildren = std::move(children.internal);
    return record;
}

} // namespace

bool isEngineInternal(const flecs::table& table)
{
    return table.has(flecs::Module) || table.has(flecs::System) || table.has(flecs::Observer) ||
           table.has(flecs::Query) || table.has<flecs::Component>() || table.has(flecs::Prefab);
}

bool isSceneEntity(const flecs::world& world, flecs::entity_t entity)
{
    if (!ecs_is_alive(world, entity) || indexOf(entity) < EcsFirstUserEntityId)
    {
        return false;
    }
    const flecs::table table = world.entity(entity).table();
    return !isEngineInternal(table) && !table.has(flecs::ChildOf, flecs::Wildcard);
}

ValueBox::ValueBox(const flecs::world& world, flecs::entity_t type, const void* source)
    : m_world(world.c_ptr()), m_type(type), m_data(ecs_value_new(world, type))
{
    // `ecs_value_*` passe par les crochets du type (flecs.h, « Values »).
    (void)ecs_value_copy(world, type, m_data, source);
}

ValueBox::ValueBox(ValueBox&& other) noexcept
    : m_world(other.m_world), m_type(other.m_type), m_data(std::exchange(other.m_data, nullptr))
{
}

ValueBox& ValueBox::operator=(ValueBox&& other) noexcept
{
    std::swap(m_world, other.m_world);
    std::swap(m_type, other.m_type);
    std::swap(m_data, other.m_data);
    return *this;
}

ValueBox::~ValueBox()
{
    if (m_data != nullptr)
    {
        ecs_value_free(m_world, m_type, m_data);
    }
}

std::vector<flecs::entity_t> sceneEntities(const flecs::world& world)
{
    // L'ordre du tableau de flecs change à chaque destruction ; un ordre d'index se compare d'un
    // monde à l'autre. On copie la liste avant tout : la moindre création l'invalide.
    const ecs_entities_t all = ecs_get_entities(world);
    std::vector<flecs::entity_t> scene;
    for (const flecs::entity_t entity :
         std::span{all.ids, static_cast<std::size_t>(all.alive_count)})
    {
        if (isSceneEntity(world, entity))
        {
            scene.push_back(entity);
        }
    }
    std::ranges::sort(scene, {}, indexOf);
    return scene;
}

core::Result<Snapshot> takeSnapshot(const flecs::world& world)
{
    world.component<Transient>(); // avant la liste des entités : l'enregistrer en crée une
    Snapshot snapshot;
    for (const flecs::entity_t entity : sceneEntities(world))
    {
        EntityRecord record = readRecord(world, entity);
        for (const flecs::entity_t id : record.type)
        {
            if (const flecs::entity_t type = valueTypeOf(world, id); type != 0)
            {
                record.values.push_back(
                    {.id = id, .value = ValueBox{world, type, ecs_get_id(world, entity, id)}});
            }
        }
        snapshot.entities.push_back(std::move(record));
    }
    return snapshot;
}

} // namespace levain::scene
