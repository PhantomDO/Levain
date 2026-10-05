#pragma once

#include <cstdint>
#include <format>

#include <flecs.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "levain/physics/components.hpp"
#include "levain/scene/components.hpp"

/// La scène des caisses de M6.1 : celle de `levain_sandbox --view physics`, et celle que mesure
/// `levain_physics_bench`. Un seul endroit, pour que le chiffre du banc soit celui de la démo.
namespace levain::sandbox
{

/// 10 × 10 × 10 caisses d'un mètre, lâchées d'une grille au-dessus du sol.
inline constexpr int CrateSide = 10;
inline constexpr float CrateSpacing = 1.6f;
/// Le dessus du sol : la hauteur du plan dessiné par la démo.
inline constexpr float CrateGroundTop = -1.0f;
/// La rangée du bas ; la plus haute part de `HighestCrateStart`. Une caisse qui n'est jamais
/// descendue sous cette hauteur trahit un pas de physique qui ne tourne pas.
inline constexpr float LowestCrateStart = 4.0f;
inline constexpr float HighestCrateStart =
    LowestCrateStart + static_cast<float>(CrateSide - 1) * CrateSpacing;

/// Un nombre entre 0 et 1, toujours le même pour une caisse et un usage (`salt`) : un hachage
/// entier (le mélange final de SplitMix64), et non un générateur aléatoire ni un sinus, pour que la
/// chute soit identique d'un lancement, d'un compilateur et d'une plateforme à l'autre.
inline float scatter(int index, int salt)
{
    std::uint64_t z = (static_cast<std::uint64_t>(index) << 8u) + static_cast<std::uint64_t>(salt);
    z += 0x9e3779b97f4a7c15ull;
    z = (z ^ (z >> 30u)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27u)) * 0x94d049bb133111ebull;
    z ^= z >> 31u;
    return static_cast<float>(z >> 40u) / static_cast<float>(1u << 24u); // 24 bits : un float exact
}

/// Un axe de rotation de longueur 1, ou l'axe vertical si le hachage est tombé sur le vecteur nul :
/// `glm::normalize` d'un vecteur nul rend des NaN.
inline glm::vec3 normalizeOrUp(const glm::vec3& axis)
{
    const float length = glm::length(axis);
    return length > 1e-6f ? axis / length : glm::vec3{0.0f, 1.0f, 0.0f};
}

/// Un sol statique de `groundSize` mètres de côté, et les caisses dynamiques au-dessus, chacune
/// décalée et tournée un peu pour qu'elles s'entrechoquent et s'éboulent au lieu de s'empiler en
/// colonnes. `Tags` s'ajoutent à chaque caisse (le sandbox y met celui de ses cubes dessinés). Ce
/// sont des racines, comme tout corps physique (ADR-0026).
template <typename... Tags> void spawnCrates(flecs::world& world, float groundSize)
{
    world.entity("ground")
        .set(levain::scene::Transform{.position = {0.0f, CrateGroundTop - 0.5f, 0.0f}})
        .set(levain::physics::Collider{
            .shape = levain::physics::Box{{groundSize / 2.0f, 0.5f, groundSize / 2.0f}}});
    const float half = static_cast<float>(CrateSide - 1) * CrateSpacing / 2.0f;
    for (int y = 0; y < CrateSide; ++y)
    {
        for (int z = 0; z < CrateSide; ++z)
        {
            for (int x = 0; x < CrateSide; ++x)
            {
                const int index = x + CrateSide * (z + CrateSide * y);
                const glm::vec3 jitter{scatter(index, 0) - 0.5f, 0.0f, scatter(index, 1) - 0.5f};
                const glm::vec3 axis{scatter(index, 2) - 0.5f, scatter(index, 3) - 0.5f,
                                     scatter(index, 4) - 0.5f};
                const flecs::entity crate =
                    world.entity(std::format("crate_{}_{}_{}", x, y, z).c_str())
                        .set(levain::scene::Transform{
                            .position =
                                glm::vec3{static_cast<float>(x) * CrateSpacing - half,
                                          LowestCrateStart + static_cast<float>(y) * CrateSpacing,
                                          static_cast<float>(z) * CrateSpacing - half} +
                                jitter,
                            .rotation = glm::angleAxis(scatter(index, 5) * glm::radians(60.0f),
                                                       normalizeOrUp(axis))})
                        .set(levain::physics::Collider{.shape = levain::physics::Box{}})
                        .set(levain::physics::RigidBody{.mass = 20.0f});
                (crate.add<Tags>(), ...);
            }
        }
    }
}

/// La hauteur de la plus haute caisse, parmi les corps dynamiques du monde.
inline float highestCrate(const flecs::world& world)
{
    float highest = -1e30f;
    world.each(
        [&highest](const levain::scene::Transform& transform, const levain::physics::RigidBody&)
        { highest = glm::max(highest, transform.position.y); });
    return highest;
}

} // namespace levain::sandbox
