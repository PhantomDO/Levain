#include "levain/physics/character.hpp"

#include <cmath>
#include <cstdint>
#include <string_view>
#include <utility>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

// L'état de Jolt d'abord : il inclut Jolt.h, que tous les autres en-têtes de Jolt attendent.
#include "physics_state.hpp"
// clang-format off
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
// clang-format on

#include "levain/core/assert.hpp"
#include "levain/core/log.hpp"

namespace levain::physics
{

namespace
{

constexpr std::string_view LogCategory = "physics";

/// Ce que le personnage lui-même touche : ce que sa couche touche, **sauf les volumes
/// déclencheurs**. Jolt ignore déjà leurs contacts dans son solveur ; les écarter pendant le
/// parcours de la broad phase (par le filtre de couches : les capteurs partagent la broad phase du
/// décor) évite de les chercher. C'est son corps intérieur que les volumes voient (ADR-0028).
bool characterSees(Layer layer)
{
    return layer != Layer::Sensor && layersCollide(Layer::Character, layer);
}

class CharacterLayerFilter final : public JPH::ObjectLayerFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer layer) const override
    {
        return characterSees(layerOf(layer));
    }
};

class CharacterBroadPhaseFilter final : public JPH::BroadPhaseLayerFilter
{
public:
    bool ShouldCollide(JPH::BroadPhaseLayer broadPhase) const override
    {
        for (std::uint8_t other = 0; other < LayerCount; ++other)
        {
            const auto layer = static_cast<Layer>(other);
            if (broadPhaseOf(layer) == broadPhase && characterSees(layer))
            {
                return true;
            }
        }
        return false;
    }
};

/// La capsule, posée sur ses pieds : Jolt la centre sur l'origine du personnage, nous la voulons
/// au-dessus, comme l'origine d'un modèle. Le décalage est dans la forme (les exemples de Jolt font
/// de même), pas dans la pose : le corps intérieur la suit sans calcul. `scale` la réduit autour de
/// son centre, qui ne bouge pas.
JPH::RefConst<JPH::Shape> standingCapsule(const Capsule& capsule, float scale = 1.0f)
{
    const float centerHeight = capsule.halfHeight + capsule.radius;
    return JPH::RotatedTranslatedShapeSettings(
               JPH::Vec3(0.0f, centerHeight, 0.0f), JPH::Quat::sIdentity(),
               new JPH::CapsuleShape(capsule.halfHeight * scale, capsule.radius * scale))
        .Create()
        .Get();
}

/// Le corps intérieur, à 90 % de la capsule, comme dans les exemples de Jolt
/// (`cInnerShapeFraction`). Le piège qu'il évite : de la taille du personnage, il pousserait les
/// caisses par sa seule pénétration, avec une masse infinie et sans la limite de `maxPushForce`.
JPH::RefConst<JPH::Shape> innerShapeOf(const Capsule& capsule)
{
    constexpr float InnerShapeFraction = 0.9f;
    return standingCapsule(capsule, InnerShapeFraction);
}

PhysicsState::Character& characterOf(PhysicsState& state, CharacterHandle handle)
{
    LEVAIN_ASSERT(handle.value < state.characters.size() &&
                      state.characters[handle.value].jolt != nullptr,
                  "personnage inconnu");
    return state.characters[handle.value];
}

const PhysicsState::Character& characterOf(const PhysicsState& state, CharacterHandle handle)
{
    LEVAIN_ASSERT(handle.value < state.characters.size() &&
                      state.characters[handle.value].jolt != nullptr,
                  "personnage inconnu");
    return state.characters[handle.value];
}

/// La rotation du personnage, **autour de Y seulement** (ADR-0028) : le lacet de `rotation`. Une
/// capsule penchée par une rotation de gameplay qui aurait pris du tangage traverserait le sol d'un
/// côté et décollerait de l'autre. Une rotation qui regarde à la verticale n'a pas de lacet : elle
/// garde celui de son axe droit : sans ça, regarder ses pieds ferait faire volte-face au
/// personnage.
glm::quat uprightOf(const glm::quat& rotation)
{
    const glm::vec3 forward = rotation * glm::vec3{0.0f, 0.0f, -1.0f};
    if (glm::length(glm::vec2{forward.x, forward.z}) > 1e-4f)
    {
        return glm::angleAxis(std::atan2(-forward.x, -forward.z), glm::vec3{0.0f, 1.0f, 0.0f});
    }
    // L'axe droit, (1, 0, 0) sans rotation, donne le lacet par le même calcul tourné d'un quart.
    const glm::vec3 right = rotation * glm::vec3{1.0f, 0.0f, 0.0f};
    return glm::angleAxis(std::atan2(-right.z, right.x), glm::vec3{0.0f, 1.0f, 0.0f});
}

GroundState groundStateOf(JPH::CharacterBase::EGroundState state)
{
    switch (state)
    {
    case JPH::CharacterBase::EGroundState::OnGround:
        return GroundState::OnGround;
    case JPH::CharacterBase::EGroundState::OnSteepGround:
        return GroundState::OnSteepGround;
    case JPH::CharacterBase::EGroundState::NotSupported:
        return GroundState::NotSupported;
    case JPH::CharacterBase::EGroundState::InAir:
        break;
    }
    return GroundState::InAir;
}

} // namespace

CharacterHandle createCharacter(PhysicsWorld& world, const CharacterController& controller,
                                const BodyPose& pose, std::uint64_t entity)
{
    LEVAIN_ASSERT(world.state != nullptr, "monde physique vide : createPhysicsWorld d'abord");
    PhysicsState& state = *world.state;
    LEVAIN_ASSERT(!state.stepping, "un personnage se crée hors du pas de physique");
    if (const auto refusal = whyNotThisCharacter(controller); refusal.has_value())
    {
        core::log(LogCategory, core::LogLevel::Error, "personnage refusé : {}", *refusal);
        LEVAIN_ASSERT(false, "personnage refusé");
        return {};
    }
    const JPH::RefConst<JPH::Shape> shape = standingCapsule(controller.shape);

    JPH::CharacterVirtualSettings settings;
    settings.mShape = shape;
    settings.mMaxSlopeAngle = glm::radians(controller.maxSlopeDegrees);
    settings.mMass = controller.mass;
    settings.mMaxStrength = controller.maxPushForce;
    // Un contact ne porte le personnage que s'il touche la demi-sphère du bas : celui d'un mur, à
    // mi-hauteur, ne fait pas un sol. La valeur des exemples de Jolt, dans notre repère des pieds.
    settings.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -controller.shape.radius);
    // Le corps intérieur : ce que les volumes et les rayons voient (ADR-0028). Jolt l'exclut des
    // requêtes du personnage lui-même.
    settings.mInnerBodyShape = innerShapeOf(controller.shape);
    settings.mInnerBodyLayer = static_cast<JPH::ObjectLayer>(Layer::Character);

    PhysicsState::Character character{
        .jolt = new JPH::CharacterVirtual(&settings, toJolt(pose.position),
                                          toJoltRotation(uprightOf(pose.rotation)), entity,
                                          &state.system),
        .update = {}};
    character.update.mWalkStairsStepUp = JPH::Vec3(0.0f, controller.stepHeight, 0.0f);
    character.update.mStickToFloorStepDown =
        JPH::Vec3(0.0f, -controller.stickToFloorDistance, 0.0f);

    if (!state.freeCharacters.empty())
    {
        const std::uint32_t slot = state.freeCharacters.back();
        state.freeCharacters.pop_back();
        state.characters[slot] = std::move(character);
        return {slot};
    }
    state.characters.push_back(std::move(character));
    return {static_cast<std::uint32_t>(state.characters.size() - 1)};
}

void destroyCharacter(PhysicsWorld& world, CharacterHandle handle)
{
    // Un personnage refusé n'a rien à détruire : sans ce retour, sa poignée invalide entrerait dans
    // les cases libres, et le personnage suivant serait écrit hors du tableau.
    if (handle.value == CharacterHandle::None)
    {
        return;
    }
    PhysicsState& state = *world.state;
    LEVAIN_ASSERT(!state.stepping, "un personnage se détruit hors du pas de physique");
    PhysicsState::Character& character = characterOf(state, handle);
    // Les volumes oublient son corps intérieur tout de suite, comme un corps détruit.
    forgetContactsOf(state.contacts, character.jolt->GetInnerBodyID().GetIndexAndSequenceNumber());
    // Le `Ref` relâché détruit le personnage, qui retire et détruit son corps intérieur.
    character.jolt = nullptr;
    state.freeCharacters.push_back(handle.value);
}

void teleportCharacter(PhysicsWorld& world, CharacterHandle handle, const BodyPose& pose)
{
    PhysicsState& state = *world.state;
    LEVAIN_ASSERT(!state.stepping, "un personnage se téléporte hors du pas de physique");
    PhysicsState::Character& stored = characterOf(state, handle);
    JPH::CharacterVirtual& character = *stored.jolt;
    character.SetPosition(toJolt(pose.position));
    character.SetRotation(toJoltRotation(uprightOf(pose.rotation)));
    character.SetLinearVelocity(JPH::Vec3::sZero());
    stored.moved = JPH::Vec3::sZero();
    // Ses contacts et son sol sont ceux de l'ancienne place : relus à la nouvelle.
    const CharacterBroadPhaseFilter broadPhase;
    const CharacterLayerFilter layers;
    character.RefreshContacts(broadPhase, layers, {}, {}, state.tempAllocator);
}

void moveCharacter(PhysicsWorld& world, CharacterHandle handle, const glm::vec3& velocity,
                   float seconds)
{
    PhysicsState& state = *world.state;
    LEVAIN_ASSERT(!state.stepping, "un personnage se déplace hors du pas de physique");
    PhysicsState::Character& character = characterOf(state, handle);
    const JPH::RVec3 before = character.jolt->GetPosition();
    character.jolt->SetLinearVelocity(toJolt(velocity));
    const CharacterBroadPhaseFilter broadPhase;
    const CharacterLayerFilter layers;
    // La gravité du monde ne déplace pas le personnage : Jolt s'en sert pour peser sur ce qui le
    // porte (en-tête de `CharacterVirtual::Update`).
    character.jolt->ExtendedUpdate(seconds, state.system.GetGravity(), character.update, broadPhase,
                                   layers, {}, {}, state.tempAllocator);
    character.moved = seconds > 0.0f ? JPH::Vec3(character.jolt->GetPosition() - before) / seconds
                                     : JPH::Vec3::sZero();
}

void turnCharacter(PhysicsWorld& world, CharacterHandle handle, const glm::quat& rotation)
{
    LEVAIN_ASSERT(!world.state->stepping, "un personnage tourne hors du pas de physique");
    characterOf(*world.state, handle).jolt->SetRotation(toJoltRotation(uprightOf(rotation)));
}

void refreshCharacterGround(PhysicsWorld& world, CharacterHandle handle)
{
    LEVAIN_ASSERT(!world.state->stepping,
                  "le sol d'un personnage se relit hors du pas de physique");
    characterOf(*world.state, handle).jolt->UpdateGroundVelocity();
}

BodyPose characterPose(const PhysicsWorld& world, CharacterHandle handle)
{
    const JPH::CharacterVirtual& character = *characterOf(std::as_const(*world.state), handle).jolt;
    return {.position = toGlm(character.GetPosition()), .rotation = toGlm(character.GetRotation())};
}

glm::vec3 characterVelocity(const PhysicsWorld& world, CharacterHandle handle)
{
    return toGlm(characterOf(std::as_const(*world.state), handle).moved);
}

CharacterGround characterGround(const PhysicsWorld& world, CharacterHandle handle)
{
    const JPH::CharacterVirtual& character = *characterOf(std::as_const(*world.state), handle).jolt;
    const GroundState state = groundStateOf(character.GetGroundState());
    return {.state = state,
            .normal = toGlm(character.GetGroundNormal()),
            .velocity = toGlm(character.GetGroundVelocity()),
            .body = state == GroundState::InAir ? 0 : character.GetGroundUserData()};
}

std::uint32_t characterCount(const PhysicsWorld& world)
{
    return static_cast<std::uint32_t>(world.state->characters.size() -
                                      world.state->freeCharacters.size());
}

} // namespace levain::physics
