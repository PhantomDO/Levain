#include "levain/scene/snapshot.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <iterator>
#include <set>
#include <span>
#include <unordered_set>
#include <utility>

#include "entity_record.hpp"

#include "levain/scene/components.hpp"
#include "levain/scene/reflection.hpp"
#include "levain/scene/tombstone.hpp"

namespace levain::scene
{

namespace
{

std::uint32_t indexOf(flecs::entity_t entity)
{
    return static_cast<std::uint32_t>(entity);
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

/// `parent` est un module, ou rangé par ChildOf sous un module, à n'importe quelle profondeur
/// (« rig::cam » créé dans la portée d'un sous-module) : ce qui est dessous appartient au module,
/// pas à la scène.
bool insideModule(const flecs::world& world, flecs::entity_t parent)
{
    for (flecs::entity_t scope = parent; scope != 0;
         scope = ecs_get_target(world, scope, EcsChildOf, 0))
    {
        if (world.entity(scope).table().has(flecs::Module))
        {
            return true;
        }
    }
    return false;
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

    /// Un enfant interne détruit en jeu (l'observateur d'un `onEnter`) : une différence, mais que
    /// Stop ne peut pas réparer, donc rendue à part (`RestoreReport`).
    void addLostInternal(std::string line) { m_lostInternals.push_back(std::move(line)); }

    [[nodiscard]] const std::vector<std::string>& lostInternals() const { return m_lostInternals; }

    /// Le compte des vraies différences, tous plafonds levés : les enfants internes perdus n'y sont
    /// pas.
    [[nodiscard]] std::size_t total() const { return m_total; }

    /// Les premières lignes des vraies différences, plafonnées, avec leur ligne de suite.
    [[nodiscard]] std::vector<std::string> realLines() &&
    {
        if (m_total > m_lines.size())
        {
            m_lines.push_back(std::format("… et {} autres différences", m_total - m_lines.size()));
        }
        return std::move(m_lines);
    }

    std::vector<std::string> finish(bool withLostInternals) &&
    {
        if (withLostInternals)
        {
            for (std::string& line : m_lostInternals)
            {
                add(std::move(line));
            }
        }
        if (m_total > m_lines.size())
        {
            m_lines.push_back(std::format("… et {} autres différences", m_total - m_lines.size()));
        }
        return std::move(m_lines);
    }

private:
    std::vector<std::string> m_lines;
    std::vector<std::string> m_lostInternals;
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
            out.addLostInternal(
                std::format("{} : l'enfant interne {} manque (un observateur détruit en jeu ne "
                            "revient pas)",
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

std::string join(const std::vector<std::string>& lines)
{
    std::string joined;
    for (const std::string& line : lines)
    {
        joined += "  " + line + "\n";
    }
    return joined;
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
    // Née en jeu sous une entité de la scène par `ChildOf` : ni scène, ni détruite par Stop.
    for (const PlacedByChildOf& placed : placedByChildOf(world))
    {
        if (indexOf(placed.entity) >= PlayFirstIndex && isSceneEntity(world, placed.parent))
        {
            out.add(std::format("{} : née en jeu sous {} par ChildOf, Stop ne la détruit pas",
                                pathOf(world, placed.entity), pathOf(world, placed.parent)));
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
    // Une pierre tombale tient un index, ce n'est pas une entité de la scène (ADR-0038, 3).
    return !isEngineInternal(table) && !table.has(flecs::ChildOf, flecs::Wildcard) &&
           !table.has<Tombstone>();
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
    // Avant la liste des entités : enregistrer `Tombstone` (dans `isSceneEntity`) en crée une.
    world.component<Tombstone>();
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

/// Avant l'étape 1 : l'erreur si un index de l'instantané est vivant sous une autre génération que
/// celle d'avant Play. flecs l'arrête en Debug (src/entity.c:3133-3139) ; en Release,
/// `ecs_make_alive` écraserait la génération vivante. Vérifié avant toute destruction, pour qu'un
/// refus laisse le monde tel quel.
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
    world.component<Tombstone>();
    for (const PlacedByChildOf& placed : placedByChildOf(world))
    {
        if (world.entity(placed.entity).table().has<Transform>() &&
            !insideModule(world, placed.parent))
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
        if (auto refusal = readValues(world, record))
        {
            return core::makeError(core::ErrorCode::Unsupported,
                                   std::format("Play ne part pas : {}", *refusal));
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

Comparison compareWithSnapshot(const flecs::world& world, const Snapshot& snapshot)
{
    Differences found = compareWorld(world, snapshot);
    Comparison comparison{.differenceCount = found.total(),
                          .differences = {},
                          .lostInternalChildren = found.lostInternals()};
    comparison.differences = std::move(found).realLines();
    return comparison;
}

std::vector<std::string> differencesFrom(const flecs::world& world, const Snapshot& snapshot)
{
    return compareWorld(world, snapshot).finish(true);
}

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
    if (auto ordered = restoreRecords(world, snapshot.entities, "Stop"); !ordered)
    {
        return std::unexpected(ordered.error());
    }
    // L'étape 6 : ce que la restauration vient de faire se vérifie par la fonction même du critère.
    Differences left = compareWorld(world, snapshot);
    RestoreReport report{.lostInternalChildren = left.lostInternals()};
    if (std::vector<std::string> lines = std::move(left).finish(false); !lines.empty())
    {
        return core::makeError(core::ErrorCode::InvalidData,
                               std::format("Stop : la scène n'est pas identique à celle d'avant "
                                           "Play :\n{}",
                                           join(lines)));
    }
    return report;
}

} // namespace levain::scene
