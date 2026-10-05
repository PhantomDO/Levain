#include "levain/physics/physics_world.hpp"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <thread>
#include <type_traits>
#include <variant>

// Jolt.h d'abord : il définit les macros que tous les autres en-têtes attendent
// (https://jrouwe.github.io/JoltPhysics/, « Getting started »).
#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/TransformedShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include "levain/core/assert.hpp"
#include "levain/core/log.hpp"
#include "levain/physics/body_rules.hpp"

namespace levain::physics
{

namespace
{

constexpr std::string_view LogCategory = "physics";

/// Les deux couches de la broad phase, la recommandation de Jolt pour commencer : le décor, peu
/// souvent mis à jour mais qui contient presque tout, et ce qui bouge. Chaque couche de broad phase
/// a un coût (https://jrouwe.github.io/JoltPhysics/, section « Broad Phase »).
constexpr JPH::BroadPhaseLayer NonMovingBroadPhase{0};
constexpr JPH::BroadPhaseLayer MovingBroadPhase{1};
constexpr JPH::uint BroadPhaseCount = 2;

JPH::BroadPhaseLayer broadPhaseOf(Layer layer)
{
    // Les volumes déclencheurs sont du décor qui ne bouge pas : ils vont avec lui.
    return layer == Layer::Static || layer == Layer::Sensor ? NonMovingBroadPhase
                                                            : MovingBroadPhase;
}

Layer layerOf(JPH::ObjectLayer layer)
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

/// Les messages de Jolt, dans notre journal. Jolt les formate à la printf.
void traceToLog(const char* format, ...)
{
    std::array<char, 1024> buffer{};
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(buffer.data(), buffer.size(), format, arguments);
    va_end(arguments);
    // En Release, Jolt ne s'en sert que pour ses erreurs : mémoire de travail épuisée, options de
    // compilation incompatibles.
    core::log(LogCategory, core::LogLevel::Warning, "{}", buffer.data());
}

#ifdef JPH_ENABLE_ASSERTS
/// Une assertion de Jolt (en Debug seulement : `JPH_DEBUG` suit `NDEBUG`), dans notre journal, puis
/// l'arrêt dans le débogueur, comme `LEVAIN_ASSERT` : rendre vrai demande à Jolt de s'y arrêter.
bool assertToLog(const char* expression, const char* message, const char* file, JPH::uint line)
{
    core::log(LogCategory, core::LogLevel::Critical, "assertion de Jolt : {} ({}) à {}:{}",
              expression, message != nullptr ? message : "", file, line);
    return true;
}
#endif

/// Jolt garde des globales : l'allocateur, la fabrique des types et leur enregistrement. Elles
/// appartiennent au processus, pas à un monde : installées au premier monde, **jamais
/// désinstallées**. Les réinstaller après `UnregisterTypes` plante dans `Factory::Register` en
/// WebAssembly (vu sur le test de déterminisme, qui crée deux mondes l'un après l'autre) ; en
/// natif, ça passait par chance. Ce qui reste à la sortie est atteignable par `Factory::sInstance`
/// : LeakSanitizer ne le compte pas comme une fuite.
void installJoltOnce()
{
    // Une statique locale : C++ garantit qu'elle n'est initialisée qu'une fois, même si deux
    // threads créent un monde en même temps.
    [[maybe_unused]] static const bool installed = []
    {
        JPH::RegisterDefaultAllocator();
        JPH::Trace = traceToLog;
#ifdef JPH_ENABLE_ASSERTS
        JPH::AssertFailed = assertToLog;
#endif
        // Jolt compilé avec d'autres options que le moteur (ses `JPH_*`) donne des structures de
        // tailles différentes des deux côtés : la mémoire se corrompt sans prévenir. On s'arrête
        // net (règle n°7), avec un message dans notre journal, ce que `RegisterTypes` ne ferait
        // pas en Release.
        if (!JPH::VerifyJoltVersionID())
        {
            core::log(LogCategory, core::LogLevel::Critical,
                      "Jolt n'est pas compilé avec les options du moteur (JPH_VERSION_ID)");
            std::abort();
        }
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
        return true;
    }();
}

JPH::Vec3 toJolt(const glm::vec3& vector)
{
    return {vector.x, vector.y, vector.z};
}

/// Un quaternion pour Jolt, **renormalisé** : Jolt suppose une rotation unitaire, ne le vérifie
/// qu'en Debug, et un quaternion de longueur 1,01 déforme le corps qu'il tourne. Le constructeur de
/// glm prend w en premier, celui de Jolt en dernier.
JPH::Quat toJoltRotation(const glm::quat& rotation)
{
    const glm::quat unit = glm::normalize(rotation);
    return {unit.x, unit.y, unit.z, unit.w};
}

glm::vec3 toGlm(JPH::Vec3Arg vector)
{
    return {vector.GetX(), vector.GetY(), vector.GetZ()};
}

glm::quat toGlm(JPH::QuatArg rotation)
{
    // Le constructeur de glm prend w en premier ; Jolt le range en dernier.
    return {rotation.GetW(), rotation.GetX(), rotation.GetY(), rotation.GetZ()};
}

static_assert(BodyHandle::None == JPH::BodyID::cInvalidBodyID,
              "BodyHandle::None doit être le BodyID invalide de Jolt");

JPH::BodyID toJolt(BodyHandle handle)
{
    return JPH::BodyID(handle.value);
}

/// La forme de Jolt d'un `Collider`. Les formes sont comptées par référence : le corps garde la
/// sienne, il n'y a rien à libérer ici.
JPH::RefConst<JPH::Shape> createShape(const Shape& shape)
{
    return std::visit(
        [](const auto& value) -> JPH::RefConst<JPH::Shape>
        {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Box>)
            {
                return new JPH::BoxShape(toJolt(value.halfExtents));
            }
            else if constexpr (std::is_same_v<T, Sphere>)
            {
                return new JPH::SphereShape(value.radius);
            }
            else
            {
                return new JPH::CapsuleShape(value.halfHeight, value.radius);
            }
        },
        shape);
}

JPH::EMotionType motionOf(const RigidBody* body)
{
    if (body == nullptr)
    {
        return JPH::EMotionType::Static;
    }
    return body->motion == Motion::Dynamic ? JPH::EMotionType::Dynamic
                                           : JPH::EMotionType::Kinematic;
}

/// Le job system selon la cible : des threads en natif, un seul dans le navigateur, où les threads
/// demanderaient des en-têtes HTTP qu'un hébergement simple n'envoie pas (ADR-0026).
std::unique_ptr<JPH::JobSystem> createJobSystem([[maybe_unused]] int workerThreads)
{
#if defined(__EMSCRIPTEN__)
    return std::make_unique<JPH::JobSystemSingleThreaded>(JPH::cMaxPhysicsJobs);
#else
    const int threads =
        workerThreads >= 0 ? workerThreads
                           : std::max(0, static_cast<int>(std::thread::hardware_concurrency()) - 1);
    return std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs,
                                                      JPH::cMaxPhysicsBarriers, threads);
#endif
}

} // namespace

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
    JPH::PhysicsSystem system;
    JPH::BodyIDVector activeBodies; ///< Gardé d'un pas à l'autre : la liste ne réalloue pas.
    std::uint32_t bodiesAddedSinceStep = 0; ///< Pour `optimizeAfterLoading`.
};

PhysicsWorld::PhysicsWorld() = default;
PhysicsWorld::PhysicsWorld(PhysicsWorld&&) noexcept = default;
PhysicsWorld& PhysicsWorld::operator=(PhysicsWorld&&) noexcept = default;

PhysicsWorld::~PhysicsWorld() = default;

PhysicsWorld createPhysicsWorld(const PhysicsSettings& settings)
{
    installJoltOnce();
    PhysicsWorld world;
    world.state = std::make_unique<PhysicsState>();
    PhysicsState& state = *world.state;
    state.jobSystem = createJobSystem(settings.workerThreads);
    // Paires de corps et contacts : les valeurs des exemples de Jolt pour 65 536 corps. Dépassées,
    // `Update` le signale, et `stepPhysics` le dit bruyamment.
    constexpr JPH::uint MaxBodyPairs = 65536;
    constexpr JPH::uint MaxContactConstraints = 10240;
    state.system.Init(settings.maxBodies, 0, MaxBodyPairs, MaxContactConstraints,
                      state.broadPhaseLayers, state.objectVsBroadPhase, state.objectLayerPairs);
    state.system.SetGravity(toJolt(settings.gravity));
    return world;
}

BodyHandle createBody(PhysicsWorld& world, const Collider& collider, const RigidBody* body,
                      const BodyPose& pose, std::uint64_t entity)
{
    LEVAIN_ASSERT(world.state != nullptr, "monde physique vide : createPhysicsWorld d'abord");
    const auto refusal =
        whyNotThisShape(collider, body).or_else([&] { return whyNotThisLayer(collider, body); });
    if (refusal.has_value())
    {
        core::log(LogCategory, core::LogLevel::Error, "corps refusé : {}", *refusal);
        LEVAIN_ASSERT(false, "corps physique refusé");
        return {};
    }
    const Layer layer = effectiveLayer(collider, body);
    JPH::BodyCreationSettings settings(createShape(collider.shape), toJolt(pose.position),
                                       toJoltRotation(pose.rotation), motionOf(body),
                                       static_cast<JPH::ObjectLayer>(layer));
    settings.mUserData = entity;
    settings.mIsSensor = layer == Layer::Sensor;
    // Le décor aussi : laissé au 0,2 de Jolt, il ferait glisser ce qu'on y pose.
    settings.mFriction = body != nullptr ? body->friction : DefaultFriction;
    if (body != nullptr)
    {
        settings.mRestitution = body->restitution;
        if (body->motion == Motion::Dynamic)
        {
            // La masse donnée, l'inertie calculée par Jolt depuis la forme et remise à l'échelle.
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass = body->mass;
        }
    }
    JPH::BodyInterface& bodies = world.state->system.GetBodyInterface();
    // Un corps statique n'a rien à simuler : l'activer ne ferait que le réveiller pour rien.
    const JPH::EActivation activation =
        body != nullptr ? JPH::EActivation::Activate : JPH::EActivation::DontActivate;
    const JPH::BodyID id = bodies.CreateAndAddBody(settings, activation);
    ++world.state->bodiesAddedSinceStep;
    if (id.IsInvalid())
    {
        // Le plafond `maxBodies` est atteint : un corps manquant est un bug de gameplay invisible.
        core::log(LogCategory, core::LogLevel::Error, "plus de place pour un corps ({} au plus)",
                  world.state->system.GetMaxBodies());
        LEVAIN_ASSERT(false, "plafond de corps physiques atteint");
        return {};
    }
    return {.value = id.GetIndexAndSequenceNumber()};
}

void destroyBody(PhysicsWorld& world, BodyHandle handle)
{
    // Un corps refusé ou jamais créé n'a rien à détruire. Jolt ne le vérifie qu'en Debug : en
    // Release, retirer le BodyID invalide lit hors de son tableau de corps et décompte un corps de
    // trop.
    if (handle.value == BodyHandle::None)
    {
        return;
    }
    JPH::BodyInterface& bodies = world.state->system.GetBodyInterface();
    bodies.RemoveBody(toJolt(handle));
    bodies.DestroyBody(toJolt(handle));
}

void teleportBody(PhysicsWorld& world, BodyHandle handle, const BodyPose& pose)
{
    JPH::BodyInterface& bodies = world.state->system.GetBodyInterface();
    const JPH::BodyID id = toJolt(handle);
    if (bodies.GetMotionType(id) != JPH::EMotionType::Static)
    {
        bodies.SetPositionAndRotation(id, toJolt(pose.position), toJoltRotation(pose.rotation),
                                      JPH::EActivation::Activate);
        return;
    }
    // Un corps statique déplacé laisse dormir ce qui reposait dessus, et ce qu'il vient recouvrir :
    // Jolt ne réveille pas les voisins d'un statique (Architecture.md, « Sleeping »). Une caisse
    // resterait suspendue en l'air là où était le sol. On réveille tout ce qui touche l'ancienne
    // place et la nouvelle.
    const JPH::AABox before = bodies.GetTransformedShape(id).GetWorldSpaceBounds();
    bodies.SetPositionAndRotation(id, toJolt(pose.position), toJoltRotation(pose.rotation),
                                  JPH::EActivation::DontActivate);
    const JPH::AABox after = bodies.GetTransformedShape(id).GetWorldSpaceBounds();
    const JPH::BroadPhaseLayerFilter everyBroadPhase;
    const JPH::ObjectLayerFilter everyLayer;
    bodies.ActivateBodiesInAABox(before, everyBroadPhase, everyLayer);
    bodies.ActivateBodiesInAABox(after, everyBroadPhase, everyLayer);
}

void moveKinematic(PhysicsWorld& world, BodyHandle handle, const BodyPose& target, float seconds)
{
    // Seul un cinématique se déplace ainsi : Jolt, sans ses assertions en Release, déplacerait
    // aussi un corps statique sans prévenir. Le cas arrive un instant, quand le RigidBody vient de
    // changer et que son corps attend d'être reconstruit au pas suivant.
    if (world.state->system.GetBodyInterface().GetMotionType(toJolt(handle)) !=
        JPH::EMotionType::Kinematic)
    {
        return;
    }
    world.state->system.GetBodyInterface().MoveKinematic(toJolt(handle), toJolt(target.position),
                                                         toJoltRotation(target.rotation), seconds);
}

namespace
{

/// Au-delà, le pas qui suit un ajout de corps commence par réorganiser la broad phase.
constexpr std::uint32_t LoadingBodies = 256;

/// Réorganise l'arbre de la broad phase après un gros chargement : des corps ajoutés un par un y
/// sont mal rangés. Mesuré sur les 1 001 caisses de M6.1, sur un thread : le pas qui les construit
/// passe de 5,0 à 2,5 ms, et celui qui le suit, le pire des 600 (3,9 ms), redevient ordinaire.
/// Jolt le recommande après avoir ajouté beaucoup de corps, jamais à chaque pas, qu'il ralentirait
/// (`PhysicsSystem::OptimizeBroadPhase`).
void optimizeAfterLoading(PhysicsState& state)
{
    if (state.bodiesAddedSinceStep > LoadingBodies)
    {
        state.system.OptimizeBroadPhase();
    }
    state.bodiesAddedSinceStep = 0;
}

} // namespace

void stepPhysics(PhysicsWorld& world, float seconds)
{
    PhysicsState& state = *world.state;
    optimizeAfterLoading(state);
    const JPH::EPhysicsUpdateError error =
        state.system.Update(seconds, 1, &state.tempAllocator, state.jobSystem.get());
    if (error != JPH::EPhysicsUpdateError::None)
    {
        // Un cache plein : des contacts ont été ignorés, des objets vont se traverser.
        core::log(LogCategory, core::LogLevel::Error,
                  "pas de physique incomplet (code {}) : augmenter les plafonds du monde",
                  static_cast<unsigned>(error));
        LEVAIN_ASSERT(false, "pas de physique incomplet");
    }
}

void collectMovedBodies(const PhysicsWorld& world, std::vector<MovedBody>& out)
{
    PhysicsState& state = *world.state;
    out.clear();
    state.system.GetActiveBodies(JPH::EBodyType::RigidBody, state.activeBodies);
    // Sans verrou : le pas est fini, aucun thread de Jolt ne touche plus aux corps.
    const JPH::BodyInterface& bodies = state.system.GetBodyInterfaceNoLock();
    for (const JPH::BodyID id : state.activeBodies)
    {
        // Un cinématique est actif tant qu'il bouge, mais c'est le jeu qui le place : lui recopier
        // la pose que Jolt a intégrée ferait dériver son Transform de quelques ulps à chaque pas.
        if (bodies.GetMotionType(id) != JPH::EMotionType::Dynamic)
        {
            continue;
        }
        JPH::RVec3 position;
        JPH::Quat rotation;
        bodies.GetPositionAndRotation(id, position, rotation);
        out.push_back({.entity = bodies.GetUserData(id),
                       .pose = {.position = toGlm(position), .rotation = toGlm(rotation)}});
    }
}

BodyPose bodyPose(const PhysicsWorld& world, BodyHandle handle)
{
    JPH::RVec3 position;
    JPH::Quat rotation;
    world.state->system.GetBodyInterface().GetPositionAndRotation(toJolt(handle), position,
                                                                  rotation);
    return {.position = toGlm(position), .rotation = toGlm(rotation)};
}

std::uint32_t bodyCount(const PhysicsWorld& world)
{
    return world.state->system.GetNumBodies();
}

} // namespace levain::physics
