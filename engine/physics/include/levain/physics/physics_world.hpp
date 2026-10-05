#pragma once

#include <cstdint>
#include <memory>

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
/// corps, pour retrouver l'entité qui le porte. Une forme, une masse ou une couche invalide
/// (`whyNotThisShape`, `whyNotThisLayer`), ou le plafond de corps atteint : une erreur au journal,
/// une assertion en Debug, et `BodyHandle::None`.
BodyHandle createBody(PhysicsWorld& world, const Collider& collider, const RigidBody* body,
                      const BodyPose& pose, std::uint64_t entity);

void destroyBody(PhysicsWorld& world, BodyHandle handle);

/// Avance la simulation d'un pas : une étape de collision, la recommandation de Jolt à 60 Hz.
void stepPhysics(PhysicsWorld& world, float seconds);

BodyPose bodyPose(const PhysicsWorld& world, BodyHandle handle);
std::uint32_t bodyCount(const PhysicsWorld& world);

} // namespace levain::physics
