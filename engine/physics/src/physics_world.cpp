#include "levain/physics/physics_world.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>

// Jolt.h d'abord : il définit les macros que tous les autres en-têtes attendent
// (https://jrouwe.github.io/JoltPhysics/, « Getting started »).
#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/TransformedShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include "physics_state.hpp"

#include "levain/core/assert.hpp"
#include "levain/core/log.hpp"
#include "levain/physics/body_rules.hpp"
#include "levain/physics/queries.hpp"

namespace levain::physics
{

namespace
{

constexpr std::string_view LogCategory = "physics";

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

/// Une forme construite par ses réglages : Jolt rend une erreur plutôt qu'une forme quand la donnée
/// ne lui convient pas. Elle va au journal, et la forme reste vide.
JPH::RefConst<JPH::Shape> built(const JPH::ShapeSettings& settings)
{
    const JPH::ShapeSettings::ShapeResult result = settings.Create();
    if (result.HasError())
    {
        core::log(LogCategory, core::LogLevel::Error, "forme refusée par Jolt : {}",
                  result.GetError().c_str());
        return nullptr;
    }
    return result.Get();
}

JPH::RefConst<JPH::Shape> meshShapeOf(const TriangleMesh& mesh)
{
    JPH::VertexList vertices;
    vertices.reserve(mesh.vertices.size());
    for (const glm::vec3& vertex : mesh.vertices)
    {
        vertices.emplace_back(vertex.x, vertex.y, vertex.z);
    }
    JPH::IndexedTriangleList triangles;
    triangles.reserve(mesh.indices.size() / 3);
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
    {
        triangles.emplace_back(mesh.indices[i], mesh.indices[i + 1], mesh.indices[i + 2], 0);
    }
    return built(JPH::MeshShapeSettings(std::move(vertices), std::move(triangles)));
}

/// L'échantillon (x, z) de Jolt est à `scale × (x, hauteur, z)` : le pas en x et en z, 1 en y,
/// puisque nos hauteurs sont déjà en mètres. Il range ses hauteurs ligne par ligne, `z × size + x`,
/// comme `HeightField` (HeightFieldShape.h, `HeightFieldShapeSettings` : `mHeightSamples` et
/// `mOffset + mScale × (x, hauteur, y)`). Jolt complète lui-même une grille dont le côté n'est
/// pas un multiple de ses blocs (513, celui de la vallée, essayé avant d'écrire ce code).
JPH::RefConst<JPH::Shape> heightFieldShapeOf(const HeightField& field)
{
    return built(JPH::HeightFieldShapeSettings(field.heights.data(), JPH::Vec3::sZero(),
                                               JPH::Vec3(field.spacing, 1.0f, field.spacing),
                                               field.size));
}

/// La forme Jolt d'une grande forme partagée : construite une fois par donnée, gardée tant que la
/// donnée vit. Une entrée dont la donnée est morte ne sert plus, même si une nouvelle donnée a pris
/// son adresse : elle est remplacée. Les autres partent au pas suivant (`forgetDeadShapes`).
template <typename Data, typename Build>
JPH::RefConst<JPH::Shape> sharedShapeOf(PhysicsState& state,
                                        const std::shared_ptr<const Data>& data, Build build)
{
    const auto found = state.sharedShapes.find(data.get());
    if (found != state.sharedShapes.end() && !found->second.data.expired())
    {
        return found->second.shape;
    }
    JPH::RefConst<JPH::Shape> shape = build(*data);
    if (shape != nullptr)
    {
        state.sharedShapes[data.get()] = {.data = data, .shape = shape};
    }
    return shape;
}

/// Oublie les formes des données mortes : une grille de 513² pèse plusieurs centaines de Ko. Une
/// fois par pas, et non à chaque construction : charger n maillages ne coûte pas n².
void forgetDeadShapes(PhysicsState& state)
{
    std::erase_if(state.sharedShapes,
                  [](const auto& entry) { return entry.second.data.expired(); });
}

} // namespace

JPH::RefConst<JPH::Shape> createShape(PhysicsState& state, const Shape& shape)
{
    return std::visit(
        [&state](const auto& value) -> JPH::RefConst<JPH::Shape>
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
            else if constexpr (std::is_same_v<T, Capsule>)
            {
                return new JPH::CapsuleShape(value.halfHeight, value.radius);
            }
            else if constexpr (std::is_same_v<T, MeshShape>)
            {
                return sharedShapeOf(state, value.mesh, meshShapeOf);
            }
            else
            {
                return sharedShapeOf(state, value.field, heightFieldShapeOf);
            }
        },
        shape);
}

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
    state.system.SetContactListener(&state.contactListener);
    return world;
}

BodyHandle createBody(PhysicsWorld& world, const Collider& collider, const RigidBody* body,
                      const BodyPose& pose, std::uint64_t entity)
{
    LEVAIN_ASSERT(world.state != nullptr, "monde physique vide : createPhysicsWorld d'abord");
    LEVAIN_ASSERT(!world.state->stepping, "un corps se crée hors du pas de physique");
    const auto refusal =
        whyNotThisShape(collider, body).or_else([&] { return whyNotThisLayer(collider, body); });
    if (refusal.has_value())
    {
        core::log(LogCategory, core::LogLevel::Error, "corps refusé : {}", *refusal);
        LEVAIN_ASSERT(false, "corps physique refusé");
        return {};
    }
    const JPH::RefConst<JPH::Shape> shape = createShape(*world.state, collider.shape);
    if (shape == nullptr)
    {
        LEVAIN_ASSERT(false, "forme refusée par Jolt");
        return {};
    }
    const Layer layer = effectiveLayer(collider, body);
    // Un volume déclencheur est cinématique d'office, et ne s'endort jamais : un capteur statique
    // perdrait le contact d'un corps qui s'endort en lui (ADR-0027).
    const bool sensor = layer == Layer::Sensor;
    JPH::BodyCreationSettings settings(shape, toJolt(pose.position), toJoltRotation(pose.rotation),
                                       sensor ? JPH::EMotionType::Kinematic : motionOf(body),
                                       static_cast<JPH::ObjectLayer>(layer));
    settings.mUserData = entity;
    settings.mIsSensor = sensor;
    settings.mAllowSleeping = !sensor;
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
        else if (std::holds_alternative<MeshShape>(collider.shape))
        {
            // Jolt ne calcule pas la masse d'un maillage (MeshShape.cpp, `GetMassProperties` rend
            // 0), mais en exige une pour tout corps mobile (MotionProperties.cpp,
            // `SetMassProperties` : assertion en Debug, masse inverse infinie en Release). Un
            // cinématique n'en fait rien, rien ne le pousse : une masse et une inertie unitaires.
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
            settings.mMassPropertiesOverride.mMass = 1.0f;
            settings.mMassPropertiesOverride.mInertia = JPH::Mat44::sIdentity();
        }
    }
    JPH::BodyInterface& bodies = world.state->system.GetBodyInterface();
    // Un corps statique n'a rien à simuler : l'activer ne ferait que le réveiller pour rien.
    const JPH::EActivation activation =
        body != nullptr || sensor ? JPH::EActivation::Activate : JPH::EActivation::DontActivate;
    const JPH::BodyID id = bodies.CreateAndAddBody(settings, activation);
    ++world.state->bodiesAddedSinceStep;
    if (sensor && !id.IsInvalid())
    {
        world.state->contacts.sensors.insert(id.GetIndexAndSequenceNumber());
    }
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
    LEVAIN_ASSERT(!world.state->stepping, "un corps se détruit hors du pas de physique");
    forgetContactsOf(world.state->contacts, handle.value);
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
    forgetDeadShapes(state);
    state.stepping = true;
    const JPH::EPhysicsUpdateError error =
        state.system.Update(seconds, 1, &state.tempAllocator, state.jobSystem.get());
    state.stepping = false;
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

void collectOverlaps(const PhysicsWorld& world, std::vector<Overlap>& out)
{
    out.clear();
    ContactBook& contacts = world.state->contacts;
    {
        const std::scoped_lock lock(contacts.mutex);
        for (const auto& [key, overlap] : contacts.overlaps)
        {
            out.push_back(overlap.entities);
        }
    }
    // Triées, pour un résultat qui ne dépende pas de l'ordre des threads. Sans doublon par
    // garde-fou : deux paires de BodyID pour les mêmes entités ne devraient pas exister,
    // `destroyBody` effaçant celles d'un corps reconstruit.
    std::ranges::sort(out);
    out.erase(std::ranges::unique(out).begin(), out.end());
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

std::size_t sharedShapeCount(const PhysicsWorld& world)
{
    return world.state->sharedShapes.size();
}

namespace
{

static_assert(LayerCount <= 32, "un LayerMask a 32 bits : décaler de plus serait indéfini");

/// Le masque des requêtes, traduit pour Jolt : une couche passe si son bit est mis.
class MaskFilter final : public JPH::ObjectLayerFilter
{
public:
    explicit MaskFilter(LayerMask mask) : m_mask(mask) {}

    bool ShouldCollide(JPH::ObjectLayer layer) const override
    {
        return ((m_mask >> static_cast<std::uint32_t>(layer)) & 1u) != 0;
    }

private:
    LayerMask m_mask;
};

/// La direction d'une requête, de longueur 1, ou rien si elle est nulle ou faite de NaN : Jolt
/// avancerait d'une longueur nulle, ou de NaN, sans le dire.
std::optional<glm::vec3> directionOf(const Ray& ray)
{
    const float length = glm::length(ray.direction);
    if (!std::isfinite(length) || length < 1e-6f || !std::isfinite(ray.maxDistance) ||
        ray.maxDistance <= 0.0f)
    {
        return std::nullopt;
    }
    return ray.direction / length;
}

/// Le corps touché, lu sous son verrou : c'est ainsi que Jolt fait lire un corps hors du pas
/// (Architecture.md, « Locking and Concurrency »). La normale est celle de la face touchée, sauf si
/// l'appelant en donne une. Rien si le corps n'existe plus.
std::optional<RayHit> hitOn(const PhysicsState& state, JPH::BodyID id,
                            const JPH::SubShapeID& subShape, const glm::vec3& point, float distance,
                            std::optional<glm::vec3> normal = std::nullopt)
{
    const JPH::BodyLockRead lock(state.system.GetBodyLockInterface(), id);
    if (!lock.Succeeded())
    {
        return std::nullopt;
    }
    const JPH::Body& body = lock.GetBody();
    return RayHit{
        .entity = body.GetUserData(),
        .point = point,
        .normal = normal.value_or(toGlm(body.GetWorldSpaceSurfaceNormal(subShape, toJolt(point)))),
        .distance = distance};
}

} // namespace

std::optional<RayHit> raycast(const PhysicsWorld& world, const Ray& ray, LayerMask mask)
{
    const std::optional<glm::vec3> direction = directionOf(ray);
    if (!direction)
    {
        return std::nullopt;
    }
    const PhysicsState& state = *world.state;
    const JPH::RRayCast cast{toJolt(ray.origin), toJolt(*direction * ray.maxDistance)};
    // Le `CastRay` simple de Jolt tient un convexe pour plein (un rayon parti de dedans le touche à
    // 0) et voit les faces arrière des triangles (Shape.h, `RayCastSettings`). Ici, comme le
    // `Physics.Raycast` d'Unity : seules les surfaces que le rayon traverse en entrant comptent. Le
    // rayon lancé du centre du joueur ne touche pas le joueur, celui lancé sous le terrain ne
    // touche pas le terrain.
    JPH::RayCastSettings settings;
    settings.SetBackFaceMode(JPH::EBackFaceMode::IgnoreBackFaces);
    settings.mTreatConvexAsSolid = false;
    JPH::ClosestHitCollisionCollector<JPH::CastRayCollector> collector;
    state.system.GetNarrowPhaseQuery().CastRay(cast, settings, collector, {}, MaskFilter{mask});
    if (!collector.HadHit())
    {
        return std::nullopt;
    }
    const JPH::RayCastResult& result = collector.mHit;
    const float distance = result.mFraction * ray.maxDistance;
    return hitOn(state, result.mBodyID, result.mSubShapeID2, ray.origin + *direction * distance,
                 distance);
}

std::optional<RayHit> sphereCast(const PhysicsWorld& world, const Ray& ray, float radius,
                                 LayerMask mask)
{
    const std::optional<glm::vec3> direction = directionOf(ray);
    if (!direction || !std::isfinite(radius) || radius <= 0.0f)
    {
        return std::nullopt;
    }
    const PhysicsState& state = *world.state;
    JPH::SphereShape sphere(radius);
    // Sur la pile : Jolt ne doit jamais la libérer s'il en prend une référence (Reference.h).
    sphere.SetEmbedded();
    const JPH::RShapeCast cast = JPH::RShapeCast::sFromWorldTransform(
        &sphere, JPH::Vec3::sOne(), JPH::RMat44::sTranslation(toJolt(ray.origin)),
        toJolt(*direction * ray.maxDistance));
    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
    state.system.GetNarrowPhaseQuery().CastShape(
        cast, JPH::ShapeCastSettings{}, JPH::RVec3::sZero(), collector, {}, MaskFilter{mask});
    if (!collector.HadHit())
    {
        return std::nullopt;
    }
    // La normale du contact, et non celle de la face : sur l'arête d'une boîte, c'est elle qui dit
    // où glisser (l'`ImpactNormal` d'Unreal). Jolt donne l'axe de pénétration, qui va de la sphère
    // vers le corps touché.
    const JPH::ShapeCastResult& hit = collector.mHit;
    return hitOn(state, hit.mBodyID2, hit.mSubShapeID2, toGlm(hit.mContactPointOn2),
                 hit.mFraction * ray.maxDistance, toGlm(-hit.mPenetrationAxis.Normalized()));
}

} // namespace levain::physics
