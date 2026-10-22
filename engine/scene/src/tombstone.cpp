#include "levain/scene/tombstone.hpp"

#include <cstdint>
#include <format>
#include <span>
#include <utility>

#include "entity_record.hpp"

#include "levain/scene/snapshot.hpp"

namespace levain::scene
{

namespace
{

std::uint32_t indexOf(flecs::entity_t entity)
{
    return static_cast<std::uint32_t>(entity);
}

/// L'index est dans la plage d'identifiants active du monde (sans plage active : partout). Les
/// entités de l'édition ne se suppriment pas, ne se réveillent pas, ne se libèrent pas en jeu :
/// voir `removeKeepingIndex`.
bool inActiveRange(const flecs::world& world, flecs::entity_t entity)
{
    const ecs_entity_range_t* range = ecs_entity_range_get(world);
    const std::uint32_t index = indexOf(entity);
    return range == nullptr || (index >= range->min && (range->max == 0 || index <= range->max));
}

core::Result<void> refuseOutsideRange(const flecs::world& world, const Tombstones& tombstones,
                                      const char* gesture)
{
    for (const ReservedIndex& reserved : tombstones.reserved)
    {
        if (!inActiveRange(world, reserved.entity))
        {
            return core::makeError(
                core::ErrorCode::InvalidData,
                std::format("{} : l'index {} est hors de la plage d'identifiants "
                            "active (jeu ou édition), le monde n'est pas touché",
                            gesture, indexOf(reserved.entity)));
        }
    }
    return {};
}

bool isTombstone(const flecs::world& world, flecs::entity_t entity)
{
    return ecs_has_id(world, entity, world.id<Tombstone>());
}

/// Une pierre est tenue quand son index est vivant sous sa génération et qu'elle porte l'étiquette.
bool holds(const flecs::world& world, const ReservedIndex& reserved)
{
    return ecs_get_alive(world, indexOf(reserved.stone)) == reserved.stone &&
           isTombstone(world, reserved.stone);
}

} // namespace

core::Result<Tombstones> removeKeepingIndex(flecs::world& world, flecs::entity_t entity)
{
    if (auto deferred = refuseDeferred(world, "Supprimer"); !deferred)
    {
        return std::unexpected(deferred.error());
    }
    // En jeu, supprimer n'est jamais une commande (ADR-0038, décision 9, amendement du
    // 2026-10-22) : une pierre posée ici survivrait à Stop (`bornInPlay` l'ignore).
    if (const ecs_entity_range_t* range = ecs_entity_range_get(world);
        range != nullptr && range->min >= PlayFirstIndex)
    {
        return core::makeError(
            core::ErrorCode::InvalidData,
            "Supprimer : en jeu, supprimer est un ecs_delete simple et pas une "
            "commande (une pierre survivrait à Stop), le monde n'est pas touché");
    }
    // Avant toute destruction : passés les 256 identifiants bas de flecs, un composant enregistré
    // après une destruction prend l'index libéré (et devient sa propre pierre).
    const flecs::entity_t tag = world.component<Tombstone>().id();
    world.component<Generated>(); // même raison : `isGenerated` l'enregistrerait après la
                                  // destruction
    if (!ecs_is_alive(world, entity))
    {
        return core::makeError(core::ErrorCode::InvalidData,
                               std::format("Supprimer : l'entité {} n'est pas vivante, le monde "
                                           "n'est pas touché",
                                           indexOf(entity)));
    }
    if (ecs_has_id(world, entity, tag))
    {
        return core::makeError(core::ErrorCode::InvalidData,
                               std::format("Supprimer : l'index {} est une pierre tombale, pas une "
                                           "entité, le monde n'est pas touché",
                                           indexOf(entity)));
    }
    if (!isSceneEntity(world, entity))
    {
        return core::makeError(core::ErrorCode::InvalidData,
                               std::format("Supprimer : l'entité {} n'est pas une entité de la "
                                           "scène (composant, module, système…), le monde n'est "
                                           "pas touché",
                                           indexOf(entity)));
    }

    const SubtreeMembers members = subtreeMembersOf(world, entity);
    if (auto handMade = refuseHandMadeUnderGenerated(world, members, "Supprimer"); !handMade)
    {
        return std::unexpected(handMade.error());
    }
    Tombstones tombstones;
    tombstones.lost = lostChildrenOf(world, members);
    tombstones.reserved.reserve(members.scene.size());
    for (const flecs::entity_t id : members.scene)
    {
        // La génération que flecs va poser dans l'index libéré (ECS_GENERATION_INC, src/entity.c).
        tombstones.reserved.push_back({.entity = id, .stone = ECS_GENERATION_INC(id)});
    }
    if (auto inRange = refuseOutsideRange(world, tombstones, "Supprimer"); !inRange)
    {
        return std::unexpected(inRange.error());
    }

    ecs_delete(world, entity);
    // Un observateur `OnRemove` a pu créer une entité pendant la destruction et prendre un index
    // libéré, à la génération de la pierre : `ecs_make_alive` n'y changerait rien et la pierre
    // serait cette entité. On vérifie tous les index avant d'en toucher un seul.
    for (const ReservedIndex& reserved : tombstones.reserved)
    {
        if (ecs_get_alive(world, indexOf(reserved.entity)) != 0)
        {
            // Ni refus ni succès complet : la suppression est faite (voir `notUndoable`).
            Tombstones done;
            done.lost = std::move(tombstones.lost);
            done.notUndoable =
                std::format("l'index {} a été repris pendant la destruction (un observateur a "
                            "créé une entité) : aucune pierre posée, l'annulation est impossible",
                            indexOf(reserved.entity));
            return done;
        }
    }
    // Toutes les pierres vivantes d'abord, l'étiquette ensuite.
    for (const ReservedIndex& reserved : tombstones.reserved)
    {
        ecs_make_alive(world, reserved.stone);
    }
    for (const ReservedIndex& reserved : tombstones.reserved)
    {
        ecs_add_id(world, reserved.stone, tag);
    }
    return tombstones;
}

core::Result<void> checkTombstonesHold(const flecs::world& world, const Tombstones& tombstones)
{
    // Sans pierre (`reserved` vide), la boucle ci-dessous passerait : on le refuse ici.
    if (!tombstones.notUndoable.empty())
    {
        return core::makeError(core::ErrorCode::InvalidData,
                               std::format("Annuler : la suppression n'est pas annulable ({}), le "
                                           "monde n'est pas touché",
                                           tombstones.notUndoable));
    }
    for (const ReservedIndex& reserved : tombstones.reserved)
    {
        if (!holds(world, reserved))
        {
            return core::makeError(
                core::ErrorCode::InvalidData,
                std::format("Annuler : l'index {} n'est plus tenu par la pierre tombale de la "
                            "commande (génération vivante : {}), le monde n'est pas touché",
                            indexOf(reserved.entity),
                            ecs_get_alive(world, indexOf(reserved.entity)) >> 32U));
        }
    }
    return {};
}

namespace
{

void destroyStones(flecs::world& world, const Tombstones& tombstones)
{
    for (const ReservedIndex& reserved : tombstones.reserved)
    {
        if (ecs_is_alive(world, reserved.stone) && isTombstone(world, reserved.stone))
        {
            ecs_delete(world, reserved.stone);
        }
    }
}

} // namespace

core::Result<void> reviveFromTombstones(flecs::world& world, const Tombstones& tombstones)
{
    if (auto deferred = refuseDeferred(world, "Annuler"); !deferred)
    {
        return std::unexpected(deferred.error());
    }
    if (auto inRange = refuseOutsideRange(world, tombstones, "Annuler"); !inRange)
    {
        return inRange;
    }
    if (auto held = checkTombstonesHold(world, tombstones); !held)
    {
        return held;
    }
    // Toutes les pierres d'abord : `ecs_make_alive` refuse (Debug) un index vivant d'une autre
    // génération. Même fenêtre qu'à la suppression : détruire une pierre déclenche `OnRemove`
    // avant le réveil, et un observateur qui créerait une entité y prendrait l'index. Aucun
    // observateur n'écoute `Tombstone` aujourd'hui ; si l'un le fait, `checkTombstonesHold` ne
    // suffit plus et il faut reprendre la garde de `removeKeepingIndex`.
    destroyStones(world, tombstones);
    for (const ReservedIndex& reserved : tombstones.reserved)
    {
        ecs_make_alive(world, reserved.entity);
    }
    return {};
}

core::Result<void> releaseTombstones(flecs::world& world, const Tombstones& tombstones)
{
    if (auto deferred = refuseDeferred(world, "Libérer"); !deferred)
    {
        return deferred;
    }
    if (auto inRange = refuseOutsideRange(world, tombstones, "Libérer"); !inRange)
    {
        return inRange;
    }
    destroyStones(world, tombstones);
    return {};
}

} // namespace levain::scene
