#pragma once

// L'état de Jolt, partagé par les fichiers du module : le monde (`physics_world.cpp`) et, à partir
// de M6.3, le personnage. **Interne** : il reste dans `src/`, et aucun en-tête de Jolt n'en sort
// (ADR-0026, SPECS §7).

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>

// Jolt.h d'abord : il définit les macros que tous les autres en-têtes attendent
// (https://jrouwe.github.io/JoltPhysics/, « Getting started »).
#include <Jolt/Jolt.h>
#include <Jolt/Core/JobSystem.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "levain/physics/components.hpp"
#include "levain/physics/layers.hpp"
#include "levain/physics/physics_world.hpp"

namespace levain::physics
{

/// Les deux couches de la broad phase, la recommandation de Jolt pour commencer : le décor, peu
/// souvent mis à jour mais qui contient presque tout, et ce qui bouge. Chaque couche de broad phase
/// a un coût (https://jrouwe.github.io/JoltPhysics/, section « Broad Phase »).
constexpr JPH::BroadPhaseLayer NonMovingBroadPhase{0};
constexpr JPH::BroadPhaseLayer MovingBroadPhase{1};
constexpr JPH::uint BroadPhaseCount = 2;

inline JPH::BroadPhaseLayer broadPhaseOf(Layer layer)
{
    // Les volumes déclencheurs sont cinématiques pour voir les corps endormis (ADR-0027), mais ne
    // bougent presque jamais : ils vont avec le décor.
    return layer == Layer::Static || layer == Layer::Sensor ? NonMovingBroadPhase
                                                            : MovingBroadPhase;
}

inline Layer layerOf(JPH::ObjectLayer layer)
{
    return static_cast<Layer>(layer);
}

class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface
{
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseCount; }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
    {
        return broadPhaseOf(layerOf(layer));
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
    {
        return layer == NonMovingBroadPhase ? "NonMoving" : "Moving";
    }
#endif
};

/// Une couche contre une couche de broad phase : vrai si l'une des couches qui y sont rangées peut
/// la toucher. Déduit de la matrice, pour qu'elle reste la seule source.
class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhase) const override
    {
        for (std::uint8_t other = 0; other < LayerCount; ++other)
        {
            const auto otherLayer = static_cast<Layer>(other);
            if (broadPhaseOf(otherLayer) == broadPhase && layersCollide(layerOf(layer), otherLayer))
            {
                return true;
            }
        }
        return false;
    }
};

class ObjectLayerPairs final : public JPH::ObjectLayerPairFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer first, JPH::ObjectLayer second) const override
    {
        return layersCollide(layerOf(first), layerOf(second));
    }
};

inline JPH::Vec3 toJolt(const glm::vec3& vector)
{
    return {vector.x, vector.y, vector.z};
}

/// Un quaternion pour Jolt, **renormalisé** : Jolt suppose une rotation unitaire, ne le vérifie
/// qu'en Debug, et un quaternion de longueur 1,01 déforme le corps qu'il tourne. Le constructeur de
/// glm prend w en premier, celui de Jolt en dernier.
inline JPH::Quat toJoltRotation(const glm::quat& rotation)
{
    const glm::quat unit = glm::normalize(rotation);
    return {unit.x, unit.y, unit.z, unit.w};
}

inline glm::vec3 toGlm(JPH::Vec3Arg vector)
{
    return {vector.GetX(), vector.GetY(), vector.GetZ()};
}

inline glm::quat toGlm(JPH::QuatArg rotation)
{
    // Le constructeur de glm prend w en premier ; Jolt le range en dernier.
    return {rotation.GetW(), rotation.GetX(), rotation.GetY(), rotation.GetZ()};
}

static_assert(BodyHandle::None == JPH::BodyID::cInvalidBodyID,
              "BodyHandle::None doit être le BodyID invalide de Jolt");

inline JPH::BodyID toJolt(BodyHandle handle)
{
    return JPH::BodyID(handle.value);
}

/// Une paire de `BodyID` en contact, un capteur et l'autre corps : combien de paires de sous-formes
/// la tiennent, et les entités, relevées à l'ajout (Jolt ne laisse pas lire les corps au retrait).
struct CountedOverlap
{
    int contacts = 0;
    Overlap entities;
};

/// Ce que les rappels de contact écrivent, depuis les threads de Jolt : les paires, sous le mutex.
/// La table des capteurs, elle, ne change que hors du pas (`createBody`, `destroyBody`, vérifié par
/// `PhysicsState::stepping`) : les threads de Jolt la lisent donc sans verrou pendant le pas.
struct ContactBook
{
    std::unordered_set<std::uint32_t> sensors;
    std::mutex mutex;
    std::map<std::pair<std::uint32_t, std::uint32_t>, CountedOverlap> overlaps;
};

/// Les contacts des volumes déclencheurs. Jolt les signale par paire de sous-formes, les deux corps
/// rangés par `BodyID` et non capteur en premier ; la plupart (les caisses entre elles) ne
/// concernent aucun capteur et repartent avant le verrou.
class SensorContactListener final : public JPH::ContactListener
{
public:
    explicit SensorContactListener(ContactBook& book) : m_book(book) {}

    void OnContactAdded(const JPH::Body& first, const JPH::Body& second,
                        const JPH::ContactManifold&, JPH::ContactSettings&) override
    {
        const auto [sensor, other] = sensorFirst(first, second);
        if (sensor == nullptr)
        {
            return;
        }
        const std::scoped_lock lock(m_book.mutex);
        CountedOverlap& overlap = m_book.overlaps[keyOf(sensor->GetID(), other->GetID())];
        overlap.entities = {.volume = sensor->GetUserData(), .body = other->GetUserData()};
        ++overlap.contacts;
    }

    void OnContactRemoved(const JPH::SubShapeIDPair& pair) override
    {
        const JPH::BodyID first = pair.GetBody1ID();
        const JPH::BodyID second = pair.GetBody2ID();
        const bool firstIsSensor = isSensor(first);
        if (!firstIsSensor && !isSensor(second))
        {
            return;
        }
        const std::scoped_lock lock(m_book.mutex);
        const auto found =
            m_book.overlaps.find(firstIsSensor ? keyOf(first, second) : keyOf(second, first));
        // Absente : le corps a été détruit entre-temps, et ses paires effacées avec lui.
        if (found != m_book.overlaps.end() && --found->second.contacts <= 0)
        {
            m_book.overlaps.erase(found);
        }
    }

private:
    static std::pair<std::uint32_t, std::uint32_t> keyOf(JPH::BodyID sensor, JPH::BodyID other)
    {
        return {sensor.GetIndexAndSequenceNumber(), other.GetIndexAndSequenceNumber()};
    }

    bool isSensor(JPH::BodyID id) const
    {
        return m_book.sensors.contains(id.GetIndexAndSequenceNumber());
    }

    std::pair<const JPH::Body*, const JPH::Body*> sensorFirst(const JPH::Body& first,
                                                              const JPH::Body& second) const
    {
        if (isSensor(first.GetID()))
        {
            return {&first, &second};
        }
        if (isSensor(second.GetID()))
        {
            return {&second, &first};
        }
        return {nullptr, nullptr};
    }

    ContactBook& m_book;
};

struct PhysicsState
{
    // Les couches d'abord : le `PhysicsSystem` garde des références vers elles, elles doivent
    // mourir après lui. Les membres sont détruits dans l'ordre inverse de leur déclaration.
    BroadPhaseLayers broadPhaseLayers;
    ObjectVsBroadPhase objectVsBroadPhase;
    ObjectLayerPairs objectLayerPairs;
    /// 10 Mio, la taille des exemples de Jolt : la mémoire de travail d'un pas, sans allocation.
    JPH::TempAllocatorImpl tempAllocator{std::size_t{10} * 1024 * 1024};
    std::unique_ptr<JPH::JobSystem> jobSystem;
    // Avant le système, comme les couches : il garde un pointeur vers l'écouteur.
    ContactBook contacts;
    SensorContactListener contactListener{contacts};
    JPH::PhysicsSystem system;
    JPH::BodyIDVector activeBodies; ///< Gardé d'un pas à l'autre : la liste ne réalloue pas.
    std::uint32_t bodiesAddedSinceStep = 0; ///< Pour `optimizeAfterLoading`.
    bool stepping = false; ///< Pendant `Update` : la table des capteurs ne doit pas changer.

    /// La forme Jolt de chaque grande forme partagée, par l'adresse de sa donnée. Le `weak_ptr` dit
    /// si la donnée vit encore : une nouvelle donnée allouée à la même adresse ne reprend pas la
    /// forme de l'ancienne.
    struct SharedShape
    {
        std::weak_ptr<const void> data;
        JPH::RefConst<JPH::Shape> shape;
    };

    std::unordered_map<const void*, SharedShape> sharedShapes;
};

/// La forme de Jolt d'un `Collider`, ou rien si Jolt la refuse. Les formes sont comptées par
/// référence : le corps garde la sienne, il n'y a rien à libérer ici.
JPH::RefConst<JPH::Shape> createShape(PhysicsState& state, const Shape& shape);

} // namespace levain::physics
