#pragma once

// Le frustum culling (M5.5) : ne pas soumettre au GPU ce que la caméra ne verra pas. Chaque dessin
// est enfermé dans une boîte alignée sur les axes du monde, et la boîte est testée contre les six
// plans du volume de vue. Un test conservateur : une boîte qui touche le volume est dessinée, même
// si son contenu reste dehors, mais jamais l'inverse.

#include <array>
#include <span>

#include <glm/glm.hpp>

namespace levain::render
{

/// Une boîte alignée sur les axes.
struct Box
{
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
};

/// La plus petite boîte qui contient `points`. Pas de point : une boîte vide en un point,
/// l'origine.
[[nodiscard]] Box boundsOf(std::span<const glm::vec3> points);

/// La boîte, alignée sur les axes du monde, qui contient `box` passée par `transform` (méthode
/// d'Arvo : chaque axe de la matrice élargit la boîte de sa contribution la plus forte).
[[nodiscard]] Box transformed(const Box& box, const glm::mat4& transform);

/// Les six plans du volume de vue, tournés vers l'intérieur : (normale, distance), un point p est
/// du bon côté si dot(normale, p) + distance ≥ 0.
struct Frustum
{
    std::array<glm::vec4, 6> planes{};
};

/// Le volume de vue de `viewProjection`, perspective ou orthographique, avec la profondeur de 0 à 1
/// de Vulkan, Direct3D et WebGPU : chaque plan est une somme ou une différence de lignes de la
/// matrice (Gribb et Hartmann, 2001).
[[nodiscard]] Frustum frustumOf(const glm::mat4& viewProjection);

/// Vrai si la boîte est entièrement d'un côté extérieur d'un plan : elle n'a rien à dessiner. Pour
/// chaque plan, seul compte le coin de la boîte le plus avancé dans la direction de sa normale.
[[nodiscard]] bool isOutside(const Frustum& frustum, const Box& box);

} // namespace levain::render
