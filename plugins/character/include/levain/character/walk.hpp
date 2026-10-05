#pragma once

#include <flecs.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "levain/animation/animator.hpp"
#include "levain/physics/character.hpp"
#include "levain/scene/components.hpp"

namespace levain::character
{

/// Ce que le joueur demande, à chaque pas : une direction **dans le monde**, sur le plan horizontal
/// (x, z), de longueur 1 au plus, déjà tournée selon la caméra ; courir ; sauter. Le saut est une
/// impulsion : la marche le consomme (le remet à faux) au pas où elle le lit, qu'il ait pu partir
/// ou non, pour qu'un appui ne fasse pas sauter deux fois.
struct WalkInput
{
    glm::vec2 direction{0.0f};
    bool run = false;
    bool jump = false;
};

/// Comment marche un personnage (ADR-0028). Les vitesses sont en m/s ; ce sont aussi celles de la
/// `Locomotion` de son animation, pour que les pieds ne glissent pas.
struct Walker
{
    float walkSpeed = 1.5f;
    float runSpeed = 4.0f;
    /// Combien de m/s il gagne ou perd par seconde pour rejoindre la vitesse demandée : 20 m/s²,
    /// la course atteinte en un cinquième de seconde.
    float acceleration = 20.0f;
    /// La part de l'accélération qui lui reste en l'air : assez pour corriger un saut, pas pour
    /// faire demi-tour.
    float airControl = 0.3f;
    float jumpSpeed = 5.0f; ///< Vers le haut, au départ : 1,27 m de saut sous 9,81 m/s².
    float gravity = 9.81f;
    float turnDegreesPerSecond = 720.0f; ///< Un demi-tour en un quart de seconde.
};

/// La vitesse que le personnage doit demander au moteur pour ce pas, gravité comprise, comme
/// l'exemple de Jolt (`CharacterVirtualTest::HandleInput`) :
///
/// - **au sol**, il repart de la vitesse du sol (une plateforme l'emporte), et saute s'il le
///   demande ;
/// - **sinon** (en l'air, ou sur une pente trop raide), il garde sa vitesse verticale, sauf si un
///   plafond l'a arrêté en montant ;
/// - **la gravité s'ajoute toujours**, même au sol : elle le garde plaqué, et sur une pente trop
///   raide, c'est elle qui le fait glisser ;
/// - **à l'horizontale**, sa vitesse par rapport au sol rejoint celle demandée, à `acceleration`
///   près (moins en l'air) ; en l'air sans rien demander, il garde son élan.
[[nodiscard]] glm::vec3 walkVelocity(const Walker& walker, const WalkInput& input,
                                     const physics::CharacterState& state,
                                     const glm::vec3& previous, float seconds);

/// La rotation vers laquelle il se tourne : vers sa marche, à `turnDegreesPerSecond` au plus, par
/// le plus court chemin, autour de Y. À l'arrêt, il garde son orientation.
[[nodiscard]] glm::quat turnTowards(const Walker& walker, const glm::quat& rotation,
                                    const glm::vec2& direction, float seconds);

/// Ce que l'animation lit (M4.5) : la vitesse **par rapport au sol**, que le renard immobile sur
/// une plateforme ne coure pas, et la vitesse effective, pas la vitesse demandée : contre un mur,
/// il s'arrête de marcher.
[[nodiscard]] animation::CharacterMotion motionOf(const physics::CharacterState& state);

/// Un pas de marche : la vitesse demandée au moteur (`walkVelocity`), la rotation vers la marche
/// (`turnTowards`), et le saut consommé.
void stepWalk(const Walker& walker, WalkInput& input, const physics::CharacterState& state,
              physics::CharacterVelocity& velocity, scene::Transform& transform, float seconds);

/// Le module flecs de la marche : `world.import<levain::character::WalkModule>()`. Une entité qui a
/// un `physics::CharacterController`, un `Walker` et un `WalkInput` marche, dans la phase
/// `Simulation`, avant le pas de physique ; avec un `animation::CharacterMotion`, elle le remplit.
struct WalkModule
{
    explicit WalkModule(flecs::world& world);
};

} // namespace levain::character
