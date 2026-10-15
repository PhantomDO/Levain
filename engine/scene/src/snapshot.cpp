#include "levain/scene/snapshot.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <iterator>
#include <set>
#include <span>
#include <unordered_set>
#include <utility>

#include "levain/scene/components.hpp"
#include "levain/scene/reflection.hpp"

namespace levain::scene
{

namespace
{

std::uint32_t indexOf(flecs::entity_t entity)
{
    return static_cast<std::uint32_t>(entity);
}

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

/// Une entité de la scène en apparence, mais rangée par `ChildOf` sous autre chose qu'un module
/// (`world.entity("a::b")`, `child_of`) : `isSceneEntity` la laisse hors de l'instantané, alors que
/// la Hiérarchie montre ce qui porte un `Transform`. Les modules rangent ainsi leurs propres
/// entités, qui sont internes ou n'ont pas de `Transform`.
struct PlacedByChildOf
{
    flecs::entity_t entity;
    flecs::entity_t parent;
};

std::vector<PlacedByChildOf> placedByChildOf(const flecs::world& world)
{
    const ecs_entities_t all = ecs_get_entities(world);
    std::vector<PlacedByChildOf> placed;
    for (const flecs::entity_t entity :
         std::span{all.ids, static_cast<std::size_t>(all.alive_count)})
    {
        const flecs::table table = world.entity(entity).table();
        if (indexOf(entity) >= EcsFirstUserEntityId && !isEngineInternal(table) &&
            table.has(flecs::ChildOf, flecs::Wildcard))
        {
            placed.push_back({entity, ecs_get_parent(world, entity)});
        }
    }
    std::ranges::sort(placed, {}, [](const PlacedByChildOf& p) { return indexOf(p.entity); });
    return placed;
}

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

/// Les différences, plafonnées : une scène entière fausse n'écrit pas 10 000 lignes.
class Differences
{
public:
    void add(std::string line)
    {
        if (m_lines.size() < MaxReportedDifferences)
        {
            m_lines.push_back(std::move(line));
        }
        ++m_total;
    }

    std::vector<std::string> finish() &&
    {
        if (m_total > m_lines.size())
        {
            m_lines.push_back(std::format("… et {} autres différences", m_total - m_lines.size()));
        }
        return std::move(m_lines);
    }

private:
    std::vector<std::string> m_lines;
    std::size_t m_total = 0;
};

template <class Ids>
void reportIdsOnlyIn(const flecs::world& world, const Ids& left, const Ids& right,
                     const std::string& who, const char* what, Differences& out)
{
    std::vector<flecs::entity_t> only;
    std::ranges::set_difference(left, right, std::back_inserter(only));
    for (const flecs::entity_t id : only)
    {
        out.add(std::format("{} : {} {}", who, what, idLabel(world, id)));
    }
}

void compareValues(const flecs::world& world, const EntityRecord& expected, const std::string& who,
                   Differences& out)
{
    for (const ComponentValue& value : expected.values)
    {
        const void* current = ecs_get_id(world, expected.id, value.id);
        if (current != nullptr && isComparable(world, value.value.type()) &&
            !sameComponentValue(world, value.value.type(), value.value.data(), current))
        {
            out.add(std::format("{} : la valeur de {} a changé", who, idLabel(world, value.id)));
        }
    }
}

void compareChildren(const flecs::world& world, const EntityRecord& expected,
                     const EntityRecord& current, const std::string& who, Differences& out)
{
    if (expected.children != current.children)
    {
        out.add(std::format("{} : les enfants ou leur ordre diffèrent ({} attendus, {} trouvés)",
                            who, expected.children.size(), current.children.size()));
    }
    for (const flecs::entity_t internal : expected.internalChildren)
    {
        if (ecs_get_alive(world, indexOf(internal)) != internal)
        {
            out.add(std::format("{} : l'enfant interne {} manque (un observateur détruit en "
                                "jeu ne revient pas)",
                                who, pathOf(world, internal)));
        }
    }
}

void compareEntity(const flecs::world& world, const EntityRecord& expected, Differences& out)
{
    const std::string who = pathOf(world, expected.id);
    const EntityRecord current = readRecord(world, expected.id);
    if (current.name != expected.name)
    {
        out.add(std::format("{} : nom « {} » au lieu de « {} »", who, current.name.value_or("-"),
                            expected.name.value_or("-")));
    }
    // L'index des noms de flecs peut perdre une entrée sans que le nom change (probe 3 de la
    // relecture de l'ADR-0037) : seul `lookup` le voit.
    if (current.name && world.lookup(who.c_str()).id() != expected.id)
    {
        out.add(std::format("{} : lookup du chemin ne rend pas l'entité", who));
    }
    if (current.parent != expected.parent)
    {
        out.add(std::format("{} : parent {} au lieu de {}", who,
                            current.parent != 0 ? pathOf(world, current.parent) : "-",
                            expected.parent != 0 ? pathOf(world, expected.parent) : "-"));
    }
    reportIdsOnlyIn(world, expected.type, current.type, who, "composant manquant", out);
    reportIdsOnlyIn(world, current.type, expected.type, who, "composant en trop", out);
    compareValues(world, expected, who, out);
    compareChildren(world, expected, current, who, out);
}

const EntityRecord* recordOf(const Snapshot& snapshot, flecs::entity_t id)
{
    const auto found = std::ranges::lower_bound(
        snapshot.entities, indexOf(id), {}, [](const EntityRecord& r) { return indexOf(r.id); });
    return found != snapshot.entities.end() && found->id == id ? &*found : nullptr;
}

/// Tout ce qui diffère entre le monde et l'instantané.
Differences compareWorld(const flecs::world& world, const Snapshot& snapshot)
{
    Differences out;
    for (const EntityRecord& expected : snapshot.entities)
    {
        const flecs::entity_t alive = ecs_get_alive(world, indexOf(expected.id));
        if (alive == expected.id)
        {
            compareEntity(world, expected, out);
        }
        else
        {
            out.add(std::format("#{} : {}", indexOf(expected.id),
                                alive == 0 ? "l'entité n'existe plus"
                                           : "l'index est repris par une autre génération"));
        }
    }
    for (const flecs::entity_t entity : sceneEntities(world))
    {
        if (recordOf(snapshot, entity) == nullptr)
        {
            out.add(std::format("{} : entité de la scène en trop", pathOf(world, entity)));
        }
    }
    return out;
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
    // `ecs_value_*` passe par les crochets du type (flecs.h, « Values ») ; `takeSnapshot` a refusé
    // avant un type qui ne se copie pas.
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

core::Result<IdRanges> createIdRanges(flecs::world& world)
{
    const ecs_entities_t entities = ecs_get_entities(world);
    if (entities.count != entities.alive_count)
    {
        return core::makeError(core::ErrorCode::InvalidData,
                               "plages d'identifiants : le monde a déjà détruit une entité, flecs "
                               "refuserait la plage (en Debug seulement) ; créer les plages juste "
                               "après le monde");
    }
    if (ecs_entity_range_get(world) != nullptr || ecs_get_max_id(world) >= PlayFirstIndex)
    {
        return core::makeError(core::ErrorCode::InvalidData,
                               "plages d'identifiants : une plage existe déjà, ou un index dépasse "
                               "celle de l'édition");
    }
    // La première plage créée à partir de 2^24 fait créer à flecs celle de l'édition, [1, 2^24 -
    // 1], et l'active (src/world.c:1535-1540) : `ecs_entity_range_get` la rend.
    const ecs_entity_range_t* play = ecs_entity_range_new(world, PlayFirstIndex, 0);
    return IdRanges{.edit = ecs_entity_range_get(world), .play = play};
}

void useEditIds(flecs::world& world, const IdRanges& ranges)
{
    ecs_entity_range_set(world, ranges.edit);
}

void usePlayIds(flecs::world& world, const IdRanges& ranges)
{
    ecs_entity_range_set(world, ranges.play);
}

bool bornInPlay(const flecs::world& world, flecs::entity_t entity)
{
    return indexOf(entity) >= PlayFirstIndex && isSceneEntity(world, entity);
}

namespace
{

/// Étape 1 : détruire les entités nées en jeu. La liste d'abord, puis les destructions : un parent
/// détruit emporte ses enfants, qui n'ont alors plus à l'être.
void destroyBornInPlay(flecs::world& world)
{
    std::vector<flecs::entity_t> born = sceneEntities(world);
    std::erase_if(born, [&](flecs::entity_t entity) { return !bornInPlay(world, entity); });
    for (const flecs::entity_t entity : born)
    {
        if (ecs_is_alive(world, entity))
        {
            ecs_delete(world, entity);
        }
    }
}

/// Les entités de l'instantané à réveiller, ou l'erreur : un index vivant d'une autre génération
/// que celle d'avant Play. flecs l'arrête en Debug (src/entity.c:3133-3139) ; en Release,
/// `ecs_make_alive` écraserait la génération vivante. Rien n'est réveillé avant que tout soit
/// vérifié.
core::Result<void> refuseTakenIndexes(const flecs::world& world, const Snapshot& snapshot)
{
    for (const EntityRecord& record : snapshot.entities)
    {
        const flecs::entity_t alive = ecs_get_alive(world, indexOf(record.id));
        if (alive != 0 && alive != record.id)
        {
            return core::makeError(
                core::ErrorCode::InvalidData,
                std::format("Stop : l'index {} de {} est pris par une autre génération ({}), "
                            "le monde n'est pas touché",
                            indexOf(record.id), record.name.value_or("une entité sans nom"),
                            alive >> 32U));
        }
    }
    return {};
}

/// Les entités de l'instantané qui ne sont plus vivantes, après l'étape 1 : celles que le jeu a
/// détruites, et les enfants des entités nées en jeu qui viennent de partir avec elles.
std::vector<flecs::entity_t> deadEntities(const flecs::world& world, const Snapshot& snapshot)
{
    std::vector<flecs::entity_t> dead;
    for (const EntityRecord& record : snapshot.entities)
    {
        if (ecs_get_alive(world, indexOf(record.id)) == 0)
        {
            dead.push_back(record.id);
        }
    }
    return dead;
}

} // namespace

core::Result<Snapshot> takeSnapshot(const flecs::world& world)
{
    world.component<Transient>(); // avant la liste des entités : l'enregistrer en crée une
    for (const PlacedByChildOf& placed : placedByChildOf(world))
    {
        if (world.entity(placed.entity).table().has<Transform>() &&
            !world.entity(placed.parent).table().has(flecs::Module))
        {
            return core::makeError(
                core::ErrorCode::Unsupported,
                std::format(
                    "Play ne part pas : {} est rangée par ChildOf sous {} : la scène n'emploie "
                    "que flecs::Parent (ADR-0015), et Stop ne la rendrait pas",
                    pathOf(world, placed.entity), pathOf(world, placed.parent)));
        }
    }
    Snapshot snapshot;
    std::set<std::pair<flecs::entity_t, std::string>> siblingNames;
    for (const flecs::entity_t entity : sceneEntities(world))
    {
        EntityRecord record = readRecord(world, entity);
        for (const flecs::entity_t id : record.type)
        {
            if (auto refusal = refusalFor(world, id))
            {
                return core::makeError(
                    core::ErrorCode::Unsupported,
                    std::format("Play ne part pas : {} : {}", pathOf(world, entity), *refusal));
            }
            if (const flecs::entity_t type = valueTypeOf(world, id); type != 0)
            {
                record.values.push_back(
                    {.id = id, .value = ValueBox{world, type, ecs_get_id(world, entity, id)}});
            }
        }
        // Deux frères du même nom : flecs les accepte, mais `lookup` n'en rend qu'un, et Stop
        // échouerait à chaque fois sur ce `lookup`.
        if (record.name && !siblingNames.emplace(record.parent, *record.name).second)
        {
            return core::makeError(
                core::ErrorCode::Unsupported,
                std::format("Play ne part pas : deux frères s'appellent « {} » "
                            "sous {}, dont {}",
                            *record.name,
                            record.parent != 0 ? pathOf(world, record.parent) : "la racine",
                            pathOf(world, entity)));
        }
        snapshot.entities.push_back(std::move(record));
    }
    return snapshot;
}

std::vector<std::string> differencesFrom(const flecs::world& world, const Snapshot& snapshot)
{
    return compareWorld(world, snapshot).finish();
}

namespace
{

/// Les enregistrements des parents d'abord : un cycle est impossible (flecs le refuse en Debug
/// seulement, src/storage/non_fragmenting_childof.c:217-222), puisque chaque parent est posé
/// avant ses enfants et que l'instantané n'en contient pas. Par profondeur croissante, puis par
/// index pour un ordre stable.
std::vector<const EntityRecord*> parentsBeforeChildren(const Snapshot& snapshot)
{
    const auto depthOf = [&](const EntityRecord& record)
    {
        std::size_t depth = 0;
        for (const EntityRecord* up = recordOf(snapshot, record.parent); up != nullptr;
             up = recordOf(snapshot, up->parent))
        {
            ++depth;
        }
        return depth;
    };
    std::vector<std::pair<std::size_t, const EntityRecord*>> sorted;
    sorted.reserve(snapshot.entities.size());
    for (const EntityRecord& record : snapshot.entities)
    {
        sorted.emplace_back(depthOf(record), &record);
    }
    std::ranges::stable_sort(sorted, {}, &std::pair<std::size_t, const EntityRecord*>::first);
    std::vector<const EntityRecord*> records;
    records.reserve(sorted.size());
    for (const auto& entry : sorted)
    {
        records.push_back(entry.second);
    }
    return records;
}

/// Étape 4, les parents. `flecs::Parent` est un composant : le poser réécrit le parent, le retirer
/// fait une racine.
void restoreParents(flecs::world& world, const Snapshot& snapshot)
{
    for (const EntityRecord* record : parentsBeforeChildren(snapshot))
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
void restoreTypes(flecs::world& world, const Snapshot& snapshot)
{
    for (const EntityRecord& record : snapshot.entities)
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
void restoreValues(flecs::world& world, const Snapshot& snapshot)
{
    for (const EntityRecord& record : snapshot.entities)
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

/// Étape 4, les noms, **tous reposés**, même ceux qui n'ont pas changé : l'index des noms de flecs
/// range un nom sous son parent *au moment où il est posé*, et changer un `flecs::Parent` ne le
/// déplace pas. Un parent et son enfant inversés par le jeu, puis remis en place par
/// `restoreParents`, laissent `lookup("haut::bas")` sans réponse, alors que les noms n'ont pas
/// bougé (test du critère). Poser le nom après les parents l'inscrit sous le bon parent.
void restoreNames(flecs::world& world, const Snapshot& snapshot)
{
    for (const EntityRecord& record : snapshot.entities)
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

/// Étape 5, l'ordre des enfants. La liste part de ce que flecs tient (les enfants internes y
/// sont), prend ceux de la scène dans l'ordre de l'instantané, puis les internes. flecs ne compare
/// la taille de la liste qu'en Debug (src/storage/ordered_children.c:203) et copierait au-delà en
/// Release : Levain vérifie lui-même que c'est une permutation des enfants du parent.
core::Result<void> restoreChildOrder(flecs::world& world, const Snapshot& snapshot)
{
    for (const EntityRecord& record : snapshot.entities)
    {
        if (record.children.empty())
        {
            continue;
        }
        const std::vector<flecs::entity_t> current = orderedChildrenOf(world, record.id);
        std::vector<flecs::entity_t> order = record.children;
        const std::unordered_set<flecs::entity_t> inScene(record.children.begin(),
                                                          record.children.end());
        std::ranges::copy_if(current, std::back_inserter(order),
                             [&](flecs::entity_t child) { return !inScene.contains(child); });
        if (!isPermutationOf(order, current))
        {
            return core::makeError(
                core::ErrorCode::InvalidData,
                std::format("Stop : les enfants de {} ne sont pas ceux de l'instantané ({} "
                            "attendus, {} trouvés)",
                            pathOf(world, record.id), order.size(), current.size()));
        }
        ecs_set_child_order(world, record.id, order.data(),
                            static_cast<std::int32_t>(order.size()));
    }
    return {};
}

} // namespace

core::Result<RestoreReport> restoreSnapshot(flecs::world& world, const IdRanges& ranges,
                                            const Snapshot& snapshot)
{
    // Stop veut des écritures immédiates (réveil, ordre des enfants) : en mode différé, flecs les
    // mettrait en file et la restauration lirait un monde pas encore écrit. Un refus, dans tous
    // les builds, le monde intact.
    if (ecs_is_deferred(world))
    {
        return core::makeError(core::ErrorCode::InvalidData,
                               "Stop : restauration appelée en mode différé, le monde n'est pas "
                               "touché");
    }
    if (auto taken = refuseTakenIndexes(world, snapshot); !taken)
    {
        return std::unexpected(taken.error());
    }
    destroyBornInPlay(world);
    useEditIds(world, ranges);
    for (const flecs::entity_t entity : deadEntities(world, snapshot))
    {
        ecs_make_alive(world, entity);
    }
    restoreParents(world, snapshot);
    restoreTypes(world, snapshot);
    restoreValues(world, snapshot);
    restoreNames(world, snapshot);
    if (auto ordered = restoreChildOrder(world, snapshot); !ordered)
    {
        return std::unexpected(ordered.error());
    }
    return RestoreReport{};
}

} // namespace levain::scene
