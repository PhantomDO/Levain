#include <doctest/doctest.h>

#include "levain/render/mesh.hpp"

using levain::render::MeshVertex;

TEST_CASE("la tangente suit u, et w dit le sens de v")
{
    // La face +x du cube (mesh.cpp) : u croît vers −z, v croît vers le bas (−y).
    const glm::vec3 normal{1.0f, 0.0f, 0.0f};
    const MeshVertex a{.position = {0.5f, -0.5f, 0.5f}, .normal = normal, .uv = {0.0f, 1.0f}};
    const MeshVertex b{.position = {0.5f, -0.5f, -0.5f}, .normal = normal, .uv = {1.0f, 1.0f}};
    const MeshVertex c{.position = {0.5f, 0.5f, -0.5f}, .normal = normal, .uv = {1.0f, 0.0f}};
    const glm::vec4 tangent = levain::render::tangentOf(a, b, c);
    CHECK(tangent.x == doctest::Approx(0.0f));
    CHECK(tangent.y == doctest::Approx(0.0f));
    CHECK(tangent.z == doctest::Approx(-1.0f));
    // normale × tangente = +y, alors que v croît vers −y : la bitangente est retournée.
    CHECK(tangent.w == -1.0f);
}

TEST_CASE("une texture retournée retourne la tangente")
{
    const glm::vec3 normal{0.0f, 1.0f, 0.0f};
    const MeshVertex a{.position = {0.0f, 0.0f, 0.0f}, .normal = normal, .uv = {0.0f, 0.0f}};
    const MeshVertex b{.position = {1.0f, 0.0f, 0.0f}, .normal = normal, .uv = {1.0f, 0.0f}};
    const MeshVertex c{.position = {0.0f, 0.0f, 1.0f}, .normal = normal, .uv = {0.0f, 1.0f}};
    const MeshVertex mirroredB{.position = b.position, .normal = normal, .uv = {-1.0f, 0.0f}};
    CHECK(levain::render::tangentOf(a, b, c).x == doctest::Approx(1.0f));
    CHECK(levain::render::tangentOf(a, mirroredB, c).x == doctest::Approx(-1.0f));
}
