#include "levain/scene/scene.hpp"

#include <cstddef>
#include <cstdint>

#include "levain/core/profile.hpp"
#include "levain/scene/camera_control.hpp"
#include "levain/scene/components.hpp"
#include "levain/scene/motion.hpp"
#include "levain/scene/transform.hpp"

namespace levain::scene
{

namespace
{

/// La réflexion (addon meta de flecs) : les champs de chaque composant, par leur décalage.
/// L'explorer s'en sert pour afficher les valeurs et les modifier, la sérialisation JSON aussi.
///
/// Décalages explicites plutôt que l'ordre de déclaration : glm::quat range ses floats (x, y, z, w)
/// en mémoire, pas dans l'ordre de son constructeur (w, x, y, z). La surcharge par pointeur de
/// membre de flecs calcule le sien en déréférençant un pointeur nul, ce que UBSan signalerait.
/// Le nombre d'éléments d'un champ, pour flecs : 0 est un scalaire. 1 en ferait un tableau d'un
/// élément, sérialisé « "x":[2.5] » au lieu de « "x":2.5 ».
constexpr std::int32_t ScalarMember = 0;

/// Les 16 flottants d'une glm::mat4, en colonnes : l'explorer les affiche en tableau.
constexpr std::int32_t Mat4Floats = 16;

void describeComponents(flecs::world& world)
{
    world.component<glm::vec3>("vec3")
        .member<float>("x", ScalarMember, offsetof(glm::vec3, x))
        .member<float>("y", ScalarMember, offsetof(glm::vec3, y))
        .member<float>("z", ScalarMember, offsetof(glm::vec3, z));
    world.component<glm::quat>("quat")
        .member<float>("x", ScalarMember, offsetof(glm::quat, x))
        .member<float>("y", ScalarMember, offsetof(glm::quat, y))
        .member<float>("z", ScalarMember, offsetof(glm::quat, z))
        .member<float>("w", ScalarMember, offsetof(glm::quat, w));
    world.component<Transform>()
        .member<glm::vec3>("position", ScalarMember, offsetof(Transform, position))
        .member<glm::quat>("rotation", ScalarMember, offsetof(Transform, rotation))
        .member<glm::vec3>("scale", ScalarMember, offsetof(Transform, scale));
    world.component<Velocity>().member<glm::vec3>("linear", ScalarMember,
                                                  offsetof(Velocity, linear));
    world.component<WorldTransform>().member<float>("matrix", Mat4Floats,
                                                    offsetof(WorldTransform, matrix));
    world.component<PreviousTransform>().member<Transform>("transform", ScalarMember,
                                                           offsetof(PreviousTransform, transform));
    world.component<RenderAlpha>().member<float>("value", ScalarMember,
                                                 offsetof(RenderAlpha, value));
}

/// La matrice monde du parent, ou l'identité pour une racine (`parent` nul) et pour un parent sans
/// `Transform`, qui laisse donc son enfant dans le repère du monde.
///
/// Lecture par `ecs_get_id` plutôt que par l'API C++ : le parent n'est pas dans la même table que
/// l'enfant (c'est le prix du stockage non fragmenté, ADR-0015), et cet accès est fait pour chaque
/// entité, à chaque tour. `entity(...).get<T>()` y ajoute 0,5 ms sur 100 000 entités.
const glm::mat4& parentWorldMatrix(const flecs::world_t* world, const flecs::Parent* parent,
                                   flecs::entity_t worldTransformId)
{
    static const glm::mat4 identity{1.0f};
    if (parent == nullptr)
    {
        return identity;
    }
    const auto* parentWorld =
        static_cast<const WorldTransform*>(ecs_get_id(world, parent->value, worldTransformId));
    return parentWorld != nullptr ? parentWorld->matrix : identity;
}

} // namespace

SceneModule::SceneModule(flecs::world& world)
{
    world.module<SceneModule>();
    describeComponents(world);

    // Les systèmes de simulation, dans leur pipeline à part (ADR-0016) : ils tournent N fois par
    // image, toujours avec le même pas. Dans un pipeline, flecs exécute les systèmes dans l'ordre
    // de leur déclaration (https://www.flecs.dev/flecs/md_docs_2Systems.html) : l'état précédent se
    // copie donc avant que quoi que ce soit ne bouge.
    world.system<const Transform, PreviousTransform>("SavePreviousTransform")
        .kind<Simulation>()
        .each([](const Transform& transform, PreviousTransform& previous)
              { previous.transform = transform; });

    // La glu : une instruction par système, la logique vit dans motion.hpp (ADR-0011).
    world.system<Transform, const Velocity>("ApplyVelocity")
        .kind<Simulation>()
        .each([](flecs::iter& it, std::size_t, Transform& transform, const Velocity& velocity)
              { applyVelocity(transform, velocity, it.delta_time()); });

    // La caméra libre. La logique est dans camera_control.hpp ; ici, la glu (ADR-0011). FpsInput
    // est un singleton que l'application repose à chaque image depuis `engine/input` — et qui
    // existe dès l'import, sinon la requête ne correspondrait à rien (règle n°7).
    world.set<FpsInput>({});
    world.system<Transform, FpsController, const FpsInput>("ApplyFpsInput")
        .term_at(2)
        .src<FpsInput>()
        .kind<Simulation>()
        .each([](flecs::iter& it, std::size_t, Transform& transform, FpsController& controller,
                 const FpsInput& input)
              { applyFpsInput(transform, controller, input, it.delta_time()); });

    // Un Transform posé à la main — une entité qui naît, un objet téléporté, l'explorer qui écrit —
    // remet l'état précédent au même endroit. Sans ça, l'entité serait affichée à sa position
    // d'avant pendant une image, et une entité neuve à l'origine du monde. Les systèmes, eux,
    // écrivent par référence : ils ne déclenchent pas cet observateur.
    world.observer<const Transform, PreviousTransform>("ResetPreviousTransform")
        .event(flecs::OnSet) // un Transform posé à la main
        .event(flecs::OnAdd) // ou l'état précédent qui arrive après lui (l'ordre des set)
        .each([](const Transform& transform, PreviousTransform& previous)
              { previous.transform = transform; });

    // Le facteur d'interpolation existe dès l'import : le système des matrices monde le demande en
    // singleton, et une requête dont le singleton manque ne correspond à **rien** — le rendu se
    // tairait au lieu d'échouer (règle n°7). 1 : on affiche l'état simulé tel quel.
    world.set<RenderAlpha>({.value = 1.0f});

    // Les phases de simulation, dans l'ordre : gameplay, physique, après la physique (ADR-0026), et
    // la fin du pas (ADR-0029).
    // `cascade(DependsOn)` range les systèmes par profondeur de leur phase dans cette chaîne, puis
    // dans l'ordre de leur déclaration : un plugin gameplay, qui importe la physique avant de
    // déclarer ses systèmes, tourne quand même avant le pas
    // (https://www.flecs.dev/flecs/md_docs_2Systems.html, section « Custom pipeline »).
    world.component<Simulation>().add<SimulationPhase>();
    world.component<Physics>().add<SimulationPhase>().depends_on(world.component<Simulation>());
    world.component<PostPhysics>().add<SimulationPhase>().depends_on(world.component<Physics>());
    world.component<EndOfStep>().add<SimulationPhase>().depends_on(world.component<PostPhysics>());
    // Les deux derniers termes sont ceux du pipeline intégré de flecs : une phase désactivée
    // (`world.component<Physics>().disable()`) met ses systèmes en pause, un module désactivé
    // aussi.
    world.set<SimulationPipeline>({.pipeline = world.pipeline()
                                                   .with(flecs::System)
                                                   .with<SimulationPhase>()
                                                   .cascade(flecs::DependsOn)
                                                   .without(flecs::Disabled)
                                                   .up(flecs::DependsOn)
                                                   .without(flecs::Disabled)
                                                   .up(flecs::ChildOf)
                                                   .build()});

    // Ce que la simulation déplace garde son état précédent, et rien d'autre : le décor immobile ne
    // paie pas l'interpolation (ADR-0016). Demain, un corps physique l'ajoutera de la même façon.
    world.component<Velocity>().add(flecs::With, world.component<PreviousTransform>());

    // Toute entité qui a un Transform a aussi un WorldTransform : le trait With de flecs l'ajoute
    // (https://www.flecs.dev/flecs/md_docs_2ComponentTraits.html). Personne n'a à y penser.
    world.component<Transform>().add(flecs::With, world.component<WorldTransform>());

    // Les matrices monde, après la simulation et avant le rendu. Le parent est lu dans le composant
    // flecs::Parent (ADR-0015) ; il est optionnel, une racine n'en a pas.
    //
    // group_by range les entités par profondeur de hiérarchie, pour qu'un parent soit calculé avant
    // ses enfants. EcsQueryGroupByOrdered est **obligatoire** : sans lui, flecs parcourt les
    // groupes dans l'ordre inverse de leur création, et un petit-enfant traîne une frame de retard
    // (https://www.flecs.dev/flecs/md_docs_2Queries.html, section « Grouping »).
    //
    // Le pointeur du monde est capturé une fois : it.world() le reconstruit à chaque entité.
    const flecs::world_t* worldPtr = world.c_ptr();
    const flecs::entity_t worldTransformId = world.id<WorldTransform>();
    world
        .system<const Transform, const PreviousTransform*, const flecs::Parent*, const RenderAlpha,
                WorldTransform>("ComputeWorldTransforms")
        .term_at(3)
        .src<RenderAlpha>() // un singleton : lu une fois par table, et non par entité
        .kind(flecs::PostUpdate)
        .group_by(flecs::ParentDepth)
        .query_flags(EcsQueryGroupByOrdered)
        .each(
            [worldPtr, worldTransformId](const Transform& local, const PreviousTransform* previous,
                                         const flecs::Parent* parent, const RenderAlpha& alpha,
                                         WorldTransform& transform)
            {
                // Sans état précédent, l'entité est rendue telle quelle : c'est le cas du décor.
                const Transform displayed =
                    previous != nullptr ? interpolate(previous->transform, local, alpha.value)
                                        : local;
                transform.matrix = worldMatrix(
                    parentWorldMatrix(worldPtr, parent, worldTransformId), localMatrix(displayed));
            });
}

int advanceWorld(flecs::world& world, FixedStep& step, float frameSeconds)
{
    const StepPlan plan = planSteps(step, frameSeconds);
    const flecs::entity_t simulation = world.get<SimulationPipeline>().pipeline;
    for (int i = 0; i < plan.steps; ++i)
    {
        LEVAIN_PROFILE_SCOPE_NAMED("pas fixe");
        // Le pas, jamais le temps réel de l'image : c'est là que tient le déterminisme (ADR-0016).
        world.run_pipeline(simulation, step.stepSeconds);
    }
    world.set<RenderAlpha>({.value = plan.alpha});
    LEVAIN_PROFILE_SCOPE_NAMED("interpolation et matrices monde");
    world.progress(frameSeconds); // le pipeline par défaut
    return plan.steps;
}

} // namespace levain::scene
