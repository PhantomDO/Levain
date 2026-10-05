#include "levain/physics/physics.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <utility>

#include "levain/core/assert.hpp"
#include "levain/core/log.hpp"
#include "levain/physics/body_rules.hpp"
#include "levain/physics/components.hpp"
#include "levain/scene/components.hpp"
#include "levain/scene/scene.hpp"

namespace levain::physics
{

namespace
{

/// Le corps de l'entité est à (re)construire, au début du pas suivant (`BuildBodies`).
void markDirty(flecs::entity entity)
{
    entity.add<BodyDirty>();
}

/// Remplace le corps d'une entité par un neuf, ou le retire si elle ne peut pas en porter.
void rebuildBody(PhysicsWorld& physics, flecs::entity entity, const Collider& collider,
                 const RigidBody* body, const scene::Transform& transform)
{
    entity.remove<BodyDirty>();
    if (const auto reason = whyNotABody(transform, entity.has<flecs::Parent>()))
    {
        // Un corps manquant ne se voit pas à l'écran avant qu'un objet ne traverse le décor.
        core::log("physics", core::LogLevel::Error, "{} : {}", entity.path().c_str(), *reason);
        LEVAIN_ASSERT(false, "entité refusée comme corps physique");
        // L'ancien corps, s'il y en a un, part avec son BodyHandle (observateur DestroyBody) : le
        // détruire aussi ici le détruirait deux fois.
        entity.remove<BodyHandle>();
        return;
    }
    // Remplacé, et non retiré : `set` sur un BodyHandle existant ne déclenche pas DestroyBody.
    if (const BodyHandle* previous = entity.try_get<BodyHandle>())
    {
        destroyBody(physics, *previous);
    }
    const BodyHandle created = createBody(physics, collider, body, poseOf(transform), entity.id());
    // Refusé par `createBody` (forme, masse, couche, plafond) : il l'a dit au journal. Le handle
    // détruit ci-dessus est d'abord effacé, pour que son retrait ne le détruise pas une seconde
    // fois.
    entity.set<BodyHandle>(created);
    if (created.value == BodyHandle::None)
    {
        entity.remove<BodyHandle>();
    }
}

/// Téléporte le corps d'une entité, ou le fait reconstruire, donc refuser, si le Transform posé ne
/// convient plus à un corps (une échelle).
void teleportOrRebuild(PhysicsWorld& physics, flecs::entity entity, BodyHandle handle,
                       const scene::Transform& transform)
{
    if (whyNotABody(transform, entity.has<flecs::Parent>()).has_value())
    {
        markDirty(entity);
        return;
    }
    teleportBody(physics, handle, poseOf(transform));
}

/// Le dernier corps d'une entité qui perd son BodyHandle. Pas à la fermeture du monde : flecs
/// supprime d'abord les entités racines, quand le singleton existe encore, et leurs corps sont
/// détruits ici, un par un ; ce qui part ensuite, le monde déjà marqué en fermeture
/// (`ecs_is_fini`), est laissé au destructeur du PhysicsSystem, qui libère tout ce qui reste.
void destroyUnlessWorldClosing(flecs::world_t* world, BodyHandle handle)
{
    if (!ecs_is_fini(world))
    {
        destroyBody(flecs::world{world}.get_mut<PhysicsWorld>(), handle);
    }
}

/// Le gameplay déplace un cinématique par son Transform ; Jolt l'y amène pendant le pas, en
/// poussant ce qu'il rencontre. Les autres corps ne passent pas par là.
void pushIfKinematic(PhysicsWorld& physics, const scene::Transform& transform,
                     const RigidBody& body, BodyHandle handle, float seconds)
{
    if (body.motion == Motion::Kinematic)
    {
        moveKinematic(physics, handle, poseOf(transform), seconds);
    }
}

/// Recopie dans leur `Transform` les corps dynamiques que le pas a déplacés. Par référence : un
/// `set` déclencherait l'observateur qui téléporte, et renverrait chaque corps là où il est déjà.
void copyMovedBodies(flecs::world& world, const std::vector<MovedBody>& moved)
{
    for (const MovedBody& body : moved)
    {
        scene::Transform* transform = world.entity(body.entity).try_get_mut<scene::Transform>();
        LEVAIN_ASSERT(transform != nullptr, "un corps dont l'entité a perdu son Transform");
        if (transform != nullptr)
        {
            transform->position = body.pose.position;
            transform->rotation = body.pose.rotation;
        }
    }
}

/// Un pas de Jolt, puis la recopie des corps qu'il a déplacés.
void stepAndCopyBack(flecs::world world, float seconds)
{
    PhysicsWorld& physics = world.get_mut<PhysicsWorld>();
    std::vector<MovedBody>& moved = world.get_mut<MovedBodyBuffer>().bodies;
    stepPhysics(physics, seconds);
    collectMovedBodies(physics, moved);
    copyMovedBodies(world, moved);
}

/// Pose et retire les relations `InsideOf` selon les volumes du dernier pas : seulement ce qui a
/// changé, dans l'ordre trié des entités, pour un résultat déterministe (ADR-0027). Une entité qui
/// n'existe plus est ignorée : flecs a déjà retiré ses paires, et émis leurs `OnRemove`.
void applyOverlaps(const flecs::world& world, const PhysicsWorld& physics, OverlapState& state)
{
    collectOverlaps(physics, state.next);
    std::vector<Overlap>& changed = state.changed;
    changed.clear();
    std::ranges::set_difference(state.current, state.next, std::back_inserter(changed));
    for (const Overlap& left : changed)
    {
        const flecs::entity body = world.entity(left.body);
        const flecs::entity volume = world.entity(left.volume);
        if (body.is_alive() && volume.is_alive())
        {
            body.remove<InsideOf>(volume);
        }
    }
    changed.clear();
    std::ranges::set_difference(state.next, state.current, std::back_inserter(changed));
    for (const Overlap& entered : changed)
    {
        const flecs::entity body = world.entity(entered.body);
        const flecs::entity volume = world.entity(entered.volume);
        if (body.is_alive() && volume.is_alive())
        {
            body.add<InsideOf>(volume);
        }
    }
    std::swap(state.current, state.next);
}

} // namespace

namespace
{

/// L'observateur devient un enfant du volume, et part avec lui. Sans ça, flecs refuse de supprimer
/// une entité qu'un observateur désigne dans une paire (« still in use by queries ») : une
/// assertion en Debug, et en Release, un observateur qui viserait une entité morte.
flecs::observer ownedBy(flecs::observer observer, flecs::entity volume)
{
    observer.child_of(volume);
    return observer;
}

} // namespace

flecs::observer onEnter(flecs::world& world, flecs::entity volume,
                        std::function<void(flecs::entity body)> react)
{
    return ownedBy(world.observer()
                       .with<InsideOf>(volume)
                       .event(flecs::OnAdd)
                       .each([react = std::move(react)](flecs::entity body) { react(body); }),
                   volume);
}

flecs::observer onExit(flecs::world& world, flecs::entity volume,
                       std::function<void(flecs::entity body)> react)
{
    return ownedBy(world.observer()
                       .with<InsideOf>(volume)
                       .event(flecs::OnRemove)
                       .each([react = std::move(react)](flecs::entity body) { react(body); }),
                   volume);
}

std::vector<flecs::entity> occupantsOf(const flecs::world& world, flecs::entity volume)
{
    std::vector<flecs::entity> occupants;
    world.query_builder().with<InsideOf>(volume).build().each([&occupants](flecs::entity body)
                                                              { occupants.push_back(body); });
    return occupants;
}

PhysicsModule::PhysicsModule(flecs::world& world)
{
    world.module<PhysicsModule>();
    world.import<scene::SceneModule>();

    const PhysicsSettings* settings = world.try_get<PhysicsSettings>();
    world.set<PhysicsWorld>(
        createPhysicsWorld(settings != nullptr ? *settings : PhysicsSettings{}));
    world.set<MovedBodyBuffer>({});
    world.set<OverlapState>({});

    // Ce que la physique déplace est interpolé par le rendu, comme ce qui a une Velocity
    // (ADR-0016) : le trait With lui attache l'état du pas précédent.
    world.component<RigidBody>().add(flecs::With, world.component<scene::PreviousTransform>());

    // La glu : une instruction par observateur et par système (ADR-0011).
    //
    // Un Collider ou un RigidBody qui arrive, change ou part : le corps est à reconstruire. Pas
    // tout de suite : au début du pas suivant, une fois que l'entité a tous ses composants, quel
    // que soit l'ordre de ses `set`. OnAdd aussi : un `add<Collider>()` n'émet pas d'OnSet.
    world.observer<const Collider>("MarkColliderDirty")
        .event(flecs::OnAdd)
        .event(flecs::OnSet)
        .each([](flecs::entity entity, const Collider&) { markDirty(entity); });
    world.observer<const RigidBody>("MarkRigidBodyDirty")
        .event(flecs::OnAdd)
        .event(flecs::OnSet)
        .event(flecs::OnRemove)
        .each([](flecs::entity entity, const RigidBody&) { markDirty(entity); });

    // Plus de Collider ou plus de Transform, plus de corps ; le retrait du BodyHandle le détruit.
    world.observer<const Collider>("RemoveBodyWithCollider")
        .event(flecs::OnRemove)
        .each([](flecs::entity entity, const Collider&) { entity.remove<BodyHandle>(); });
    world.observer<const scene::Transform>("RemoveBodyWithTransform")
        .with<BodyHandle>()
        .event(flecs::OnRemove)
        .each([](flecs::entity entity, const scene::Transform&) { entity.remove<BodyHandle>(); });

    // Le seul endroit où un corps est détruit : le BodyHandle qui part, avec son Collider, son
    // Transform ou son entité.
    world.observer<const BodyHandle>("DestroyBody")
        .event(flecs::OnRemove)
        .each([](flecs::iter& it, std::size_t, const BodyHandle& handle)
              { destroyUnlessWorldClosing(it.world().c_ptr(), handle); });

    // Un Transform **posé** sur un corps le téléporte. Le BodyHandle ne déclenche rien (filter) :
    // sans ça, chaque corps construit serait téléporté là où il vient d'être créé.
    world.observer<const scene::Transform, const BodyHandle>("TeleportBody")
        .event(flecs::OnSet)
        .term_at(1)
        .filter()
        .each(
            [](flecs::iter& it, std::size_t row, const scene::Transform& transform,
               const BodyHandle& handle)
            {
                teleportOrRebuild(it.world().get_mut<PhysicsWorld>(), it.entity(row), handle,
                                  transform);
            });

    // Un refus n'est pas définitif : une entité sans corps dont le Transform est reposé (l'échelle
    // remise à 1), ou qui prend ou perd un parent, est réexaminée. flecs::Parent et non ChildOf,
    // que flecs pose aussi pour les noms (ADR-0015).
    world.observer<const scene::Transform>("RecheckRefusedBody")
        .with<Collider>()
        .without<BodyHandle>()
        .event(flecs::OnSet)
        .each([](flecs::entity entity, const scene::Transform&) { markDirty(entity); });
    world.observer<const flecs::Parent>("RecheckBodyOnReparent")
        .with<Collider>()
        .event(flecs::OnSet)
        .event(flecs::OnRemove)
        .each([](flecs::entity entity, const flecs::Parent&) { markDirty(entity); });

    // Le pas, dans la phase Physics : après le gameplay, avant ce qui lit son résultat (ADR-0026).
    // Quatre systèmes, exécutés dans l'ordre de leur déclaration.
    //
    // D'abord un point de synchronisation. Un `set` fait par le gameplay pendant la simulation
    // écrit la valeur tout de suite, mais son OnSet attend la fusion des commandes, et flecs n'en
    // fait aucune entre deux phases : la téléportation arriverait après le pas, que la recopie
    // aurait déjà écrasée. Un système `immediate` force la fusion avant lui (manuel des systèmes de
    // flecs, « Sync points » : https://www.flecs.dev/flecs/md_docs_2Systems.html). Le
    // `write<BodyHandle>` de BuildBodies en crée une aussi, mais par accident : ce point-ci est
    // voulu. Sans aucun des deux, le test « un Transform posé par le gameplay » échoue.
    world.system("SyncBeforePhysics").kind<scene::Physics>().immediate().run([](flecs::iter&) {});
    // `write<BodyHandle>` : les BodyHandle posés ici sont fusionnés avant le système suivant, qui
    // pousse donc dès ce pas un cinématique qui vient de naître.
    world.system<const Collider, const scene::Transform, const RigidBody*>("BuildBodies")
        .with<BodyDirty>()
        .write<BodyHandle>()
        .kind<scene::Physics>()
        .each(
            [](flecs::iter& it, std::size_t row, const Collider& collider,
               const scene::Transform& transform, const RigidBody* body)
            {
                rebuildBody(it.world().get_mut<PhysicsWorld>(), it.entity(row), collider, body,
                            transform);
            });
    world.system<const scene::Transform, const RigidBody, const BodyHandle>("PushKinematicBodies")
        .kind<scene::Physics>()
        .each(
            [](flecs::iter& it, std::size_t, const scene::Transform& transform,
               const RigidBody& body, const BodyHandle& handle)
            {
                pushIfKinematic(it.world().get_mut<PhysicsWorld>(), transform, body, handle,
                                it.delta_time());
            });
    world.system("StepPhysics")
        .kind<scene::Physics>()
        .run([](flecs::iter& it) { stepAndCopyBack(it.world(), it.delta_time()); });

    // Après le pas : qui est entré dans un volume, qui en est sorti (ADR-0027).
    world.system("ApplyOverlaps")
        .kind<scene::PostPhysics>()
        .run(
            [](flecs::iter& it)
            {
                applyOverlaps(it.world(), it.world().get<PhysicsWorld>(),
                              it.world().get_mut<OverlapState>());
            });
}

} // namespace levain::physics
