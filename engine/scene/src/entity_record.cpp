#include "entity_record.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <iterator>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "levain/scene/reflection.hpp"

namespace levain::scene
{

/// Le chemin sans le « :: » de tête que flecs met aux enfants de `flecs::Parent` : lisible, et
/// `lookup` le relit aussi bien.
std::string pathOf(const flecs::world& world, flecs::entity_t entity)
{
    std::string path = world.entity(entity).path().c_str();
    return path.starts_with("::") ? path.substr(2) : path;
}

std::string idLabel(const flecs::world& world, flecs::entity_t id)
{
    return flecs::id(world, id).str().c_str();
}

namespace
{

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

} // namespace

/// Le type d'une entité sans ce qui n'est pas son état.
std::vector<flecs::entity_t> sceneTypeOf(const flecs::world& world, flecs::entity_t entity)
{
    return typeWhere(world, entity, [&](flecs::id_t id) { return !isTransientId(world, id); });
}

/// Ce qu'un module marque `Transient` et que l'entité porte : Stop le retire.
std::vector<flecs::entity_t> transientTypeOf(const flecs::world& world, flecs::entity_t entity)
{
    return typeWhere(world, entity, [&](flecs::id_t id) { return isTransientId(world, id); });
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

namespace
{

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

} // namespace

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

namespace
{

/// Pourquoi Stop ne saurait pas rendre cet identifiant du type, ou rien.
std::optional<std::string> refusalFor(const flecs::world& world, flecs::id_t id)
{
    if (ECS_IS_PAIR(id) && ecs_pair_first(world, id) == EcsIsA)
    {
        return std::format("instance de prefab {} : ses valeurs héritées ne se copient pas entité "
                           "par entité",
                           idLabel(world, id));
    }
    const ecs_type_info_t* info = ecs_get_type_info(world, id);
    // Une copie interdite, ou pas de constructeur par défaut : `ecs_value_new` ne saurait pas
    // construire la copie (flecs s'arrête sur une assertion).
    constexpr ecs_flags32_t Uncopyable = ECS_TYPE_HOOK_COPY_ILLEGAL | ECS_TYPE_HOOK_CTOR_ILLEGAL;
    if (info != nullptr && (info->hooks.flags & Uncopyable) != 0)
    {
        return std::format("le composant {} ne se copie pas (marquer `scene::Transient` s'il n'est "
                           "pas de la scène)",
                           idLabel(world, id));
    }
    return std::nullopt;
}

/// Un type que `sameValue` sait comparer : décrit pour la réflexion (ADR-0034), ou une feuille.
bool isDescribed(const flecs::world& world, flecs::entity_t type)
{
    return ecs_has(world, type, EcsStruct) || ecs_has(world, type, EcsArray) ||
           ecs_has(world, type, EcsPrimitive) || ecs_has(world, type, EcsEnum) ||
           ecs_has(world, type, EcsBitmask) || type == world.id<flecs::entity>();
}

const ecs_type_info_t* comparingHook(const flecs::world& world, flecs::entity_t type)
{
    const ecs_type_info_t* info = ecs_get_type_info(world, type);
    const bool usable = info != nullptr && info->hooks.cmp != nullptr &&
                        (info->hooks.flags & ECS_TYPE_HOOK_CMP_ILLEGAL) == 0;
    return usable ? info : nullptr;
}

} // namespace

std::vector<flecs::entity_t> sceneChildrenOf(const flecs::world& world, flecs::entity_t parent)
{
    return childrenOf(world, parent).scene;
}

bool isGenerated(const flecs::world& world, flecs::entity_t entity)
{
    return ecs_has_id(world, entity, world.id<Generated>());
}

SubtreeMembers subtreeMembersOf(const flecs::world& world, flecs::entity_t root)
{
    SubtreeMembers members{
        .scene = {root}, .generated = {}, .handMadeUnderGenerated = 0, .other = {}};
    // Les listes se parcourent par indice : `push_back` déplace le tableau.
    for (std::size_t next = 0; next < members.scene.size(); ++next)
    {
        for (const flecs::entity_t child : orderedChildrenOf(world, members.scene[next]))
        {
            if (!isSceneEntity(world, child))
            {
                members.other.push_back(child);
            }
            else if (isGenerated(world, child))
            {
                members.generated.push_back(child);
            }
            else
            {
                members.scene.push_back(child);
            }
        }
    }
    for (std::size_t next = 0; next < members.generated.size(); ++next)
    {
        for (const flecs::entity_t child : orderedChildrenOf(world, members.generated[next]))
        {
            if (!isSceneEntity(world, child))
            {
                members.other.push_back(child);
            }
            else if (isGenerated(world, child))
            {
                members.generated.push_back(child);
            }
            else if (members.handMadeUnderGenerated == 0)
            {
                members.handMadeUnderGenerated = child;
            }
        }
    }
    for (std::size_t next = 0; next < members.other.size(); ++next)
    {
        for (const flecs::entity_t child : orderedChildrenOf(world, members.other[next]))
        {
            members.other.push_back(child);
        }
    }
    return members;
}

core::Result<void> refuseHandMadeUnderGenerated(const flecs::world& world,
                                                const SubtreeMembers& members, const char* gesture)
{
    if (members.handMadeUnderGenerated == 0)
    {
        return {};
    }
    return core::makeError(
        core::ErrorCode::InvalidData,
        std::format("{} : {} est faite à la main sous un nœud généré : l'annuler la réveillerait "
                    "sous un parent mort, le monde n'est pas touché",
                    gesture, pathOf(world, members.handMadeUnderGenerated)));
}

std::vector<LostChild> lostChildrenOf(const flecs::world& world, const SubtreeMembers& members)
{
    std::vector<LostChild> lost;
    lost.reserve(members.other.size());
    for (const flecs::entity_t id : members.other)
    {
        const char* name = ecs_get_name(world, id);
        lost.push_back({.id = id, .name = name != nullptr ? name : "(sans nom)"});
    }
    return lost;
}

core::Result<void> refuseDeferred(const flecs::world& world, const char* gesture)
{
    if (ecs_is_deferred(world))
    {
        return core::makeError(
            core::ErrorCode::InvalidData,
            std::format("{} : appelée en mode différé, le monde n'est pas touché", gesture));
    }
    return {};
}

/// Les valeurs des composants du type, copiées par `ValueBox`. Le premier composant que Stop ne
/// saurait pas rendre arrête la lecture : « chemin : cause », que l'appelant met dans son erreur.
std::optional<std::string> readValues(const flecs::world& world, EntityRecord& record)
{
    for (const flecs::entity_t id : record.type)
    {
        if (auto refusal = refusalFor(world, id))
        {
            return std::format("{} : {}", pathOf(world, record.id), *refusal);
        }
        if (const flecs::entity_t type = valueTypeOf(world, id); type != 0)
        {
            record.values.push_back(
                {.id = id, .value = ValueBox{world, type, ecs_get_id(world, record.id, id)}});
        }
    }
    return std::nullopt;
}

/// Une valeur se compare si son type est décrit (`sameValue`) ou porte un crochet `cmp` ; sinon
/// elle ne se compare pas, et la restauration la réécrit toujours.
bool isComparable(const flecs::world& world, flecs::entity_t type)
{
    return isDescribed(world, type) || comparingHook(world, type) != nullptr;
}

bool sameComponentValue(const flecs::world& world, flecs::entity_t type, const void* first,
                        const void* second)
{
    if (isDescribed(world, type))
    {
        return sameValue(world, type, first, second);
    }
    const ecs_type_info_t* info = comparingHook(world, type);
    return info->hooks.cmp(first, second, info) == 0;
}

namespace
{

/// Les enregistrements des parents d'abord : un cycle est impossible (flecs le refuse en Debug
/// seulement, src/storage/non_fragmenting_childof.c:217-222), puisque chaque parent est posé
/// avant ses enfants et que la liste n'en contient pas. Par profondeur croissante, puis dans
/// l'ordre de la liste pour un ordre stable. Un parent qui n'est pas dans la liste (la racine d'un
/// sous-arbre) a la profondeur zéro : son entité existe déjà.
std::vector<const EntityRecord*> parentsBeforeChildren(std::span<const EntityRecord> records)
{
    std::unordered_map<flecs::entity_t, const EntityRecord*> byId;
    byId.reserve(records.size());
    for (const EntityRecord& record : records)
    {
        byId.emplace(record.id, &record);
    }
    const auto recordOf = [&](flecs::entity_t id)
    {
        const auto found = byId.find(id);
        return found != byId.end() ? found->second : nullptr;
    };
    const auto depthOf = [&](const EntityRecord& record)
    {
        std::size_t depth = 0;
        for (const EntityRecord* up = recordOf(record.parent); up != nullptr;
             up = recordOf(up->parent))
        {
            ++depth;
        }
        return depth;
    };
    std::vector<std::pair<std::size_t, const EntityRecord*>> sorted;
    sorted.reserve(records.size());
    for (const EntityRecord& record : records)
    {
        sorted.emplace_back(depthOf(record), &record);
    }
    std::ranges::stable_sort(sorted, {}, &std::pair<std::size_t, const EntityRecord*>::first);
    std::vector<const EntityRecord*> ordered;
    ordered.reserve(sorted.size());
    for (const auto& entry : sorted)
    {
        ordered.push_back(entry.second);
    }
    return ordered;
}

/// Étape 4, les parents. `flecs::Parent` est un composant : le poser réécrit le parent, le retirer
/// fait une racine.
void restoreParents(flecs::world& world, std::span<const EntityRecord> records)
{
    for (const EntityRecord* record : parentsBeforeChildren(records))
    {
        if (ecs_get_parent(world, record->id) == record->parent)
        {
            continue;
        }
        if (record->parent == 0)
        {
            ecs_remove_id(world, record->id, ecs_id(EcsParent));
        }
        else
        {
            const EcsParent parent{.value = record->parent};
            ecs_set_id(world, record->id, ecs_id(EcsParent), sizeof(parent), &parent);
        }
    }
}

/// Étape 4, les types : les composants de trop sont retirés (les observateurs du moteur détruisent
/// alors les corps Jolt), `Transient` compris, les étiquettes et paires manquantes ajoutées. Un
/// composant à valeur manquant revient avec sa valeur, à l'étape suivante.
void restoreTypes(flecs::world& world, std::span<const EntityRecord> records)
{
    for (const EntityRecord& record : records)
    {
        std::vector<flecs::entity_t> extra = transientTypeOf(world, record.id);
        const std::vector<flecs::entity_t> current = sceneTypeOf(world, record.id);
        std::ranges::set_difference(current, record.type, std::back_inserter(extra));
        for (const flecs::entity_t id : extra)
        {
            ecs_remove_id(world, record.id, id);
        }
        std::vector<flecs::entity_t> missing;
        std::ranges::set_difference(record.type, current, std::back_inserter(missing));
        for (const flecs::entity_t id : missing)
        {
            if (valueTypeOf(world, id) == 0)
            {
                ecs_add_id(world, record.id, id);
            }
        }
    }
}

/// Étape 4, les valeurs, par `setComponentValue` (ADR-0034) : réécrites quand elles ont changé
/// (`sameValue`), toujours quand on ne sait pas les comparer.
void restoreValues(flecs::world& world, std::span<const EntityRecord> records)
{
    for (const EntityRecord& record : records)
    {
        for (const ComponentValue& value : record.values)
        {
            const void* current = ecs_get_id(world, record.id, value.id);
            const flecs::entity_t type = value.value.type();
            if (current == nullptr || !isComparable(world, type) ||
                !sameComponentValue(world, type, value.value.data(), current))
            {
                setComponentValue(world, record.id, value.id, value.value.data());
            }
        }
    }
}

/// Étape 4, les noms, **tous reposés**, même ceux qui n'ont pas changé. flecs range un nom dans
/// l'index de son parent et l'y déplace quand le parent change ; mais une entité qui **perd** son
/// `flecs::Parent` (un enfant devenu racine) sort de l'index sans être rangée à la racine
/// (src/storage/non_fragmenting_childof.c:345). Un parent et son enfant inversés par le jeu, puis
/// remis en place par `restoreParents`, laissent donc `lookup("haut::bas")` sans réponse, alors
/// que les noms n'ont pas bougé (test du critère). Reposer le nom après les parents le range de
/// nouveau : `ecs_set_name` crée l'entrée qui manque (src/entity_name.c:451-456). Le même passage
/// répare le nom qu'un frère a pris à un autre avant d'être détruit.
void restoreNames(flecs::world& world, std::span<const EntityRecord> records)
{
    for (const EntityRecord& record : records)
    {
        ecs_set_name(world, record.id, record.name ? record.name->c_str() : nullptr);
    }
}

/// Les deux listes ont les mêmes éléments : deux copies triées, en n log n (la liste d'enfants d'un
/// parent compte des dizaines de milliers d'entités).
bool isPermutationOf(std::vector<flecs::entity_t> first, std::vector<flecs::entity_t> second)
{
    std::ranges::sort(first);
    std::ranges::sort(second);
    return first == second;
}

} // namespace

/// Étape 5, l'ordre des enfants. La liste part de ce que flecs tient (les enfants internes y
/// sont), prend ceux de la scène dans l'ordre demandé, puis les internes. flecs ne compare la
/// taille de la liste qu'en Debug (src/storage/ordered_children.c:203) et copierait au-delà en
/// Release : Levain vérifie lui-même que c'est une permutation des enfants du parent.
core::Result<void> setSceneChildOrder(flecs::world& world, flecs::entity_t parent,
                                      std::span<const flecs::entity_t> sceneChildren,
                                      const char* gesture, const char* source)
{
    const std::vector<flecs::entity_t> current = orderedChildrenOf(world, parent);
    std::vector<flecs::entity_t> order(sceneChildren.begin(), sceneChildren.end());
    const std::unordered_set<flecs::entity_t> inScene(sceneChildren.begin(), sceneChildren.end());
    std::ranges::copy_if(current, std::back_inserter(order),
                         [&](flecs::entity_t child) { return !inScene.contains(child); });
    if (!isPermutationOf(order, current))
    {
        return core::makeError(
            core::ErrorCode::InvalidData,
            std::format("{} : les enfants de {} ne sont pas ceux de {} ({} attendus, {} "
                        "trouvés)",
                        gesture, pathOf(world, parent), source, order.size(), current.size()));
    }
    ecs_set_child_order(world, parent, order.data(), static_cast<std::int32_t>(order.size()));
    return {};
}

core::Result<void> restoreRecords(flecs::world& world, std::span<const EntityRecord> records,
                                  const char* gesture, const char* source)
{
    restoreParents(world, records);
    restoreTypes(world, records);
    restoreValues(world, records);
    restoreNames(world, records);
    for (const EntityRecord& record : records)
    {
        if (record.children.empty())
        {
            continue;
        }
        if (auto ordered = setSceneChildOrder(world, record.id, record.children, gesture, source);
            !ordered)
        {
            return ordered;
        }
    }
    return {};
}

} // namespace levain::scene
