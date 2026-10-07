#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "levain/physics/components.hpp"

namespace levain::physics
{

struct PhysicsSettings
{
    glm::vec3 gravity{0.0f, -9.81f, 0.0f};
    /// Les threads de travail de Jolt, en plus de celui qui appelle `stepPhysics`. Négatif : les
    /// cœurs de la machine moins un. Ignoré dans le navigateur, où tout tourne sur un thread
    /// (ADR-0026).
    int workerThreads = -1;
    std::uint32_t maxBodies = 65536;
};

/// Où est un corps, et comment il est tourné : ce que la physique et le `Transform` s'échangent.
struct BodyPose
{
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
};

/// Un corps que le dernier pas a déplacé, et l'entité qu'il représente.
struct MovedBody
{
    std::uint64_t entity = 0;
    BodyPose pose;
};

/// L'état de Jolt (son `PhysicsSystem`, ses couches, son allocateur et ses threads), caché ici :
/// aucun en-tête de Jolt ne sort de `physics/src` (ADR-0026, SPECS §7).
struct PhysicsState;

/// Le monde physique. Un objet sans méthode : tout passe par les fonctions libres ci-dessous, qui
/// le prennent en paramètre (ADR-0011). Vide quand il est construit par défaut — flecs l'exige d'un
/// composant —, utilisable après `createPhysicsWorld`.
struct PhysicsWorld
{
    PhysicsWorld();
    PhysicsWorld(PhysicsWorld&&) noexcept;
    PhysicsWorld& operator=(PhysicsWorld&&) noexcept;
    ~PhysicsWorld();

    std::unique_ptr<PhysicsState> state;
};

PhysicsWorld createPhysicsWorld(const PhysicsSettings& settings = {});

/// Crée un corps et l'ajoute au monde. `body` nul : un corps statique. `entity` est gardé par le
/// corps, et rendu par `collectMovedBodies` pour retrouver l'entité qui le porte. Une forme, une
/// masse ou une couche invalide (`whyNotThisShape`, `whyNotThisLayer`), ou une forme que Jolt
/// refuse : une erreur au journal et `BodyHandle::None`, sans assertion, car c'est une donnée et
/// non un bug (ADR-0034). Le plafond de corps atteint, lui, en est un : une assertion en Debug.
BodyHandle createBody(PhysicsWorld& world, const Collider& collider, const RigidBody* body,
                      const BodyPose& pose, std::uint64_t entity);

void destroyBody(PhysicsWorld& world, BodyHandle handle);

/// Place un corps sans le faire traverser l'espace entre les deux : un corps qu'on y aurait laissé
/// est simplement dépassé. Le corps est réveillé, sauf s'il est statique.
void teleportBody(PhysicsWorld& world, BodyHandle handle, const BodyPose& pose);

/// Donne à un corps cinématique la vitesse qui l'amène en `target` au bout de `seconds` : ce qu'il
/// rencontre en chemin est poussé, ce que `teleportBody` ne ferait pas.
void moveKinematic(PhysicsWorld& world, BodyHandle handle, const BodyPose& target, float seconds);

/// Avance la simulation d'un pas : une étape de collision, la recommandation de Jolt à 60 Hz.
void stepPhysics(PhysicsWorld& world, float seconds);

/// Les corps dynamiques que Jolt n'a pas endormis, avec leur pose : ce qu'il faut recopier dans les
/// `Transform`. Pas les cinématiques, que le jeu place lui-même. `out` est vidé puis rempli ; le
/// garder d'un pas à l'autre évite d'allouer.
///
/// L'ordre n'est pas déterministe (Jolt réveille les corps depuis plusieurs threads) : chacun
/// désigne sa propre entité, le résultat de la recopie n'en dépend pas.
void collectMovedBodies(const PhysicsWorld& world, std::vector<MovedBody>& out);

/// Un corps dans un volume déclencheur : les entités du volume et du corps.
struct Overlap
{
    std::uint64_t volume = 0;
    std::uint64_t body = 0;
    auto operator<=>(const Overlap&) const = default;
};

/// Ce qui est dans les volumes déclencheurs après le dernier pas, trié et sans doublon : la base de
/// la relation `InsideOf` (ADR-0027). Jolt signale ses contacts par paire de sous-formes et de
/// `BodyID` ; ici, ils sont ramenés aux entités. Un corps reconstruit au même pas (un nouveau
/// `BodyID`) garde donc sa place, sans sortie ni entrée fantômes.
void collectOverlaps(const PhysicsWorld& world, std::vector<Overlap>& out);

BodyPose bodyPose(const PhysicsWorld& world, BodyHandle handle);
std::uint32_t bodyCount(const PhysicsWorld& world);
/// Les grandes formes que Jolt garde construites : une par donnée partagée, quel que soit le nombre
/// de corps qui l'utilisent. Celles des données relâchées partent au pas suivant.
std::size_t sharedShapeCount(const PhysicsWorld& world);

} // namespace levain::physics
