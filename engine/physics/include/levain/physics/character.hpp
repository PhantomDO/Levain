#pragma once

#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>

#include <glm/glm.hpp>

#include "levain/physics/components.hpp"
#include "levain/physics/physics_world.hpp"

namespace levain::physics
{

/// Ce qui fait d'une entité un personnage (ADR-0028), comme le `CharacterController` d'Unity : une
/// capsule debout que le jeu déplace à la vitesse qu'il veut, et qui glisse le long de ce qu'elle
/// touche, monte les marches et reste collée au sol. Ce n'est pas un corps : il ne tombe pas seul
/// et ne bascule pas. Ce qui bouge contre lui (une plateforme, un rocher qui roule) l'emmène à sa
/// vitesse, sans échange de quantité de mouvement (ADR-0028).
///
/// Sa position est celle de ses **pieds** : le bas de la capsule, comme l'origine d'un modèle.
struct CharacterController
{
    Capsule shape{.halfHeight = 0.5f, .radius = 0.3f};
    /// Au-delà, le sol est trop raide : il ne le monte pas, et y glisse si le jeu lui donne la
    /// gravité. Le défaut de Jolt ; Unity prend 45°, Unreal 44,765°.
    float maxSlopeDegrees = 50.0f;
    /// La plus haute marche qu'il monte sans sauter : le `stepOffset` d'Unity, le `MaxStepHeight`
    /// d'Unreal. Au-dessus de la moitié de la capsule, il monterait sur une table.
    float stepHeight = 0.3f;
    /// Jusqu'où il se laisse descendre pour rester au sol en marchant : sans ça, il décolle à
    /// chaque marche descendue et chaque bosse franchie.
    float stickToFloorDistance = 0.5f;
    /// Ce qu'il pèse sur ce qui le porte, en kilogrammes : une caisse sous ses pieds s'enfonce, une
    /// plateforme en équilibre penche. Jolt ne s'en sert que là
    /// (`CharacterVirtualSettings::mMass`).
    float mass = 70.0f;
    /// La force maximale avec laquelle il pousse un corps dynamique, en newtons. Jolt donne au
    /// corps touché l'impulsion qui l'amène à la vitesse du personnage, compte tenu de **sa**
    /// masse, mais pas plus que cette force. C'est un seuil : une caisse ne bouge que si la force
    /// dépasse son frottement, m < F / (μ·g), soit 20 kg avec 100 N et nos frottements de 0,5.
    ///
    /// Le piège : contre un obstacle plus haut qu'une marche, `ExtendedUpdate` le déplace **deux
    /// fois** par pas (son déplacement, puis l'essai de monter la marche), et chaque fois la caisse
    /// reçoit sa poussée. Avec une hauteur de marche, le seuil double : vers 40 kg. 100 N, le
    /// défaut de Jolt.
    float maxPushForce = 100.0f;
};

/// Pourquoi ces réglages ne peuvent pas donner de personnage, ou rien. Jolt les prendrait sans un
/// mot en Release : une pente de 90° ou plus en fait un sol partout, une masse nulle des NaN dès
/// qu'il pousse (règle n°7).
inline std::optional<std::string_view> whyNotThisCharacter(const CharacterController& controller)
{
    const auto positive = [](float value) { return std::isfinite(value) && value > 0.0f; };
    const auto positiveOrZero = [](float value) { return std::isfinite(value) && value >= 0.0f; };
    if (!positive(controller.shape.halfHeight) || !positive(controller.shape.radius))
    {
        return "la capsule d'un personnage a des dimensions finies et plus grandes que 0";
    }
    if (!positive(controller.maxSlopeDegrees) || controller.maxSlopeDegrees >= 90.0f)
    {
        return "la pente maximale d'un personnage est entre 0 et 90°, exclus";
    }
    if (!positiveOrZero(controller.stepHeight) || !positiveOrZero(controller.stickToFloorDistance))
    {
        return "la hauteur de marche et la distance de collage au sol sont finies et positives";
    }
    if (!positive(controller.mass) || !positiveOrZero(controller.maxPushForce))
    {
        return "un personnage a une masse plus grande que 0, et une force de poussée positive";
    }
    return std::nullopt;
}

/// L'identifiant du personnage de l'entité, **posé par le module**. Opaque, comme `BodyHandle`.
struct CharacterHandle
{
    static constexpr std::uint32_t None = 0xffffffffu;
    std::uint32_t value = None;
};

/// Ce qu'il y a sous ses pieds, après son dernier déplacement.
enum class GroundState : std::uint8_t
{
    OnGround,      ///< Un sol qu'il peut monter : il marche.
    OnSteepGround, ///< Un sol trop raide : il glisse, si le jeu lui donne la gravité.
    NotSupported,  ///< Il touche quelque chose, mais rien ne le porte (une arête, un mur).
    InAir,         ///< Rien sous lui.
};

/// Ce que le personnage a trouvé : son sol, la normale de ce sol, et la vitesse de ce qui le porte
/// (une plateforme qui bouge). En l'air, la normale est **nulle** (Jolt la remet à zéro) et `body`
/// vaut 0 ; sinon `body` est l'entité touchée, même sans appui (`NotSupported`).
struct CharacterGround
{
    GroundState state = GroundState::InAir;
    glm::vec3 normal{0.0f};
    glm::vec3 velocity{0.0f};
    std::uint64_t body = 0;
};

/// Le personnage qui marche : il est au sol, et ce sol n'est pas trop raide. C'est la question de
/// presque tout le gameplay ; `OnSteepGround` n'y répond pas oui, sans quoi il sauterait d'une
/// paroi.
constexpr bool isWalking(const CharacterGround& ground)
{
    return ground.state == GroundState::OnGround;
}

/// La vitesse que le gameplay veut donner au personnage, **chute comprise** : le moteur ne lui
/// applique pas la gravité (ADR-0028). Elle **persiste** d'un pas à l'autre, comme le `velocity` de
/// Godot : non reposée, il continue à la même vitesse. Une téléportation la remet à zéro. Sans
/// elle, le personnage ne bouge pas.
struct CharacterVelocity
{
    glm::vec3 value{0.0f};
};

/// Ce que le module a trouvé au dernier pas, **posé par lui**, en lecture seule : le sol (sa
/// vitesse relue après le pas, pour une plateforme), et la vitesse effective, celle qu'il faut
/// animer.
struct CharacterState
{
    CharacterGround ground;
    glm::vec3 velocity{0.0f};
};

/// Crée un personnage à `pose` (ses pieds), avec un corps intérieur sur la couche `Character` que
/// les volumes déclencheurs et les rayons voient, et qui rend `entity` (ADR-0028). Des réglages
/// refusés (`whyNotThisCharacter`) : une erreur au journal, une assertion
/// en Debug, et `CharacterHandle::None`.
CharacterHandle createCharacter(PhysicsWorld& world, const CharacterController& controller,
                                const BodyPose& pose, std::uint64_t entity);

void destroyCharacter(PhysicsWorld& world, CharacterHandle handle);

/// Place le personnage sans le faire traverser l'espace entre les deux, et oublie son sol.
void teleportCharacter(PhysicsWorld& world, CharacterHandle handle, const BodyPose& pose);

/// Déplace le personnage à `velocity` pendant `seconds` : il glisse le long des obstacles, monte
/// les marches et reste au sol (`ExtendedUpdate` de Jolt), et pousse les corps dynamiques qu'il
/// touche.
/// **La gravité est dans `velocity`** : le moteur ne la lui donne pas (ADR-0028).
///
/// À appeler **avant** `stepPhysics`, comme les exemples de Jolt : les impulsions données aux
/// caisses poussées entrent dans ce pas.
void moveCharacter(PhysicsWorld& world, CharacterHandle handle, const glm::vec3& velocity,
                   float seconds);

/// Tourne le personnage, sans le déplacer, **autour de Y seulement** : le tangage et le roulis de
/// `rotation` sont ignorés, une capsule penchée traverserait le sol (ADR-0028).
void turnCharacter(PhysicsWorld& world, CharacterHandle handle, const glm::quat& rotation);

/// Relit la vitesse du sol sous le personnage, à appeler **après** `stepPhysics` : une plateforme
/// a bougé pendant le pas, et la vitesse que le jeu calculera au pas suivant doit inclure la
/// sienne.
void refreshCharacterGround(PhysicsWorld& world, CharacterHandle handle);

BodyPose characterPose(const PhysicsWorld& world, CharacterHandle handle);

/// La vitesse que le personnage a vraiment eue à son dernier déplacement, son déplacement divisé
/// par sa durée : nulle contre un mur, verticale dans un escalier. Pas la vitesse que Jolt garde,
/// que les collisions ne corrigent pas : un renard animé selon elle courrait sur place contre un
/// mur.
glm::vec3 characterVelocity(const PhysicsWorld& world, CharacterHandle handle);

CharacterGround characterGround(const PhysicsWorld& world, CharacterHandle handle);

std::uint32_t characterCount(const PhysicsWorld& world);

} // namespace levain::physics
