#include <array>
#include <cstdint>
#include <cstring>

#include <doctest/doctest.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "levain/render/camera.hpp"
#include "levain/render/culling.hpp"
#include "levain/render/mesh.hpp"

namespace
{

using levain::render::Box;

/// Une boîte de côté 1 centrée sur `center`.
Box unitBoxAt(glm::vec3 center)
{
    return {.min = center - 0.5f, .max = center + 0.5f};
}

/// La caméra par défaut regarde l'origine depuis (0, 1,5, 3) : 60° de champ, de 0,1 à 100.
levain::render::Frustum defaultFrustum()
{
    return levain::render::frustumOf(
        levain::render::viewProjectionOf(levain::render::Camera{}, 16.0f / 9.0f));
}

} // namespace

TEST_CASE("le frustum garde ce que la caméra voit, et écarte ce qui est derrière, sur les côtés ou "
          "trop loin")
{
    const levain::render::Frustum frustum = defaultFrustum();
    CHECK_FALSE(levain::render::isOutside(frustum, unitBoxAt({0.0f, 0.0f, 0.0f})));
    CHECK(levain::render::isOutside(frustum, unitBoxAt({0.0f, 1.5f, 6.0f})));    // derrière
    CHECK(levain::render::isOutside(frustum, unitBoxAt({30.0f, 0.0f, 0.0f})));   // à droite
    CHECK(levain::render::isOutside(frustum, unitBoxAt({0.0f, 30.0f, 0.0f})));   // au-dessus
    CHECK(levain::render::isOutside(frustum, unitBoxAt({0.0f, 0.0f, -200.0f}))); // trop loin
    // À cheval sur un plan : elle touche le volume, on la dessine.
    CHECK_FALSE(levain::render::isOutside(
        frustum, Box{.min = {-1.0f, 0.0f, 0.0f}, .max = {40.0f, 1.0f, 1.0f}}));
}

TEST_CASE("le frustum d'une projection orthographique, celle des cascades d'ombre")
{
    const glm::mat4 viewProjection =
        glm::orthoRH_ZO(-5.0f, 5.0f, -5.0f, 5.0f, 0.0f, 20.0f) *
        glm::lookAtRH(glm::vec3{0.0f, 10.0f, 0.0f}, glm::vec3{0.0f}, glm::vec3{0.0f, 0.0f, -1.0f});
    const levain::render::Frustum frustum = levain::render::frustumOf(viewProjection);
    CHECK_FALSE(levain::render::isOutside(frustum, unitBoxAt({4.0f, 0.0f, 4.0f})));
    CHECK(levain::render::isOutside(frustum, unitBoxAt({6.0f, 0.0f, 0.0f})));
    CHECK(levain::render::isOutside(frustum,
                                    unitBoxAt({0.0f, -11.0f, 0.0f}))); // sous le plan lointain
}

TEST_CASE("une boîte transformée contient chacun des huit coins transformés, et rien de plus")
{
    const Box box{.min = {-1.0f, -2.0f, -3.0f}, .max = {1.0f, 2.0f, 3.0f}};
    const glm::mat4 transform =
        glm::scale(glm::rotate(glm::translate(glm::mat4{1.0f}, glm::vec3{5.0f, 0.0f, -2.0f}),
                               glm::radians(45.0f), glm::vec3{0.0f, 1.0f, 0.0f}),
                   glm::vec3{2.0f});
    std::array<glm::vec3, 8> corners{};
    for (std::uint32_t i = 0; i < 8; ++i)
    {
        corners[i] = glm::vec3{transform * glm::vec4{(i & 1u) != 0 ? box.max.x : box.min.x,
                                                     (i & 2u) != 0 ? box.max.y : box.min.y,
                                                     (i & 4u) != 0 ? box.max.z : box.min.z, 1.0f}};
    }
    const Box expected = levain::render::boundsOf(corners);
    const Box actual = levain::render::transformed(box, transform);
    for (int axis = 0; axis < 3; ++axis)
    {
        CAPTURE(axis);
        CHECK(actual.min[axis] == doctest::Approx(expected.min[axis]));
        CHECK(actual.max[axis] == doctest::Approx(expected.max[axis]));
    }
}

TEST_CASE(
    "la boîte d'instances tournées contient chaque coin tourné, et reste exacte sans rotation")
{
    levain::render::Mesh mesh;
    mesh.bounds = levain::render::Box{.min = {-1.0f, -0.5f, -0.25f}, .max = {1.0f, 0.5f, 0.25f}};
    levain::render::Instances instances;
    instances.positionBounds = {.min = {10.0f, 0.0f, 0.0f}, .max = {12.0f, 0.0f, 0.0f}};

    // Sans rotation : la boîte du mesh, étirée de l'écart entre les positions, comme avant.
    const auto straight = levain::render::worldBoundsOf(mesh, instances, glm::mat4{1.0f});
    REQUIRE(straight.has_value());
    CHECK(straight.value_or(levain::render::Box{}).min.x == doctest::Approx(9.0f));
    CHECK(straight.value_or(levain::render::Box{}).max.y == doctest::Approx(0.5f));

    // Une instance tournée : chacun des huit coins, tourné d'un angle quelconque, reste dedans.
    instances.anyRotated = true;
    const glm::quat rotation =
        glm::angleAxis(glm::radians(50.0f), glm::normalize(glm::vec3{1.0f, 2.0f, 3.0f}));
    const auto box = levain::render::worldBoundsOf(mesh, instances, glm::mat4{1.0f})
                         .value_or(levain::render::Box{});
    for (std::uint32_t corner = 0; corner < 8; ++corner)
    {
        const glm::vec3 local{(corner & 1u) != 0 ? 1.0f : -1.0f, (corner & 2u) != 0 ? 0.5f : -0.5f,
                              (corner & 4u) != 0 ? 0.25f : -0.25f};
        for (const float x : {10.0f, 12.0f})
        {
            const glm::vec3 placed = rotation * local + glm::vec3{x, 0.0f, 0.0f};
            CAPTURE(corner);
            CHECK(glm::all(glm::greaterThanEqual(placed, box.min - 1e-5f)));
            CHECK(glm::all(glm::lessThanEqual(placed, box.max + 1e-5f)));
        }
    }
}

TEST_CASE("glm range un quaternion (x, y, z, w) en mémoire, comme le shader le lit")
{
    // INSTANCE_ROTATION est un float4 (x, y, z, w) : glm doit ranger ses composantes ainsi, alors
    // que son constructeur prend w en premier.
    const glm::quat rotation{4.0f, 1.0f, 2.0f, 3.0f};
    std::array<float, 4> stored{};
    std::memcpy(stored.data(), &rotation, sizeof(stored));
    CHECK(stored == std::array{1.0f, 2.0f, 3.0f, 4.0f});
}

TEST_CASE("la boîte d'instances tournées contient le mesh placé par un modèle déplacé et réduit")
{
    // L'ordre du shader (placeInInstance) : le modèle, puis la rotation de l'instance, puis sa
    // position.
    levain::render::Mesh mesh;
    mesh.bounds = levain::render::Box{.min = glm::vec3{-0.5f}, .max = glm::vec3{0.5f}};
    levain::render::Instances instances;
    instances.positionBounds = {.min = {3.0f, 0.0f, -2.0f}, .max = {3.0f, 0.0f, -2.0f}};
    instances.anyRotated = true;
    const glm::mat4 model =
        glm::scale(glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 0.6f, 0.0f}), glm::vec3{0.4f});
    const glm::quat rotation = glm::angleAxis(glm::radians(70.0f), glm::vec3{1.0f, 0.0f, 0.0f});
    const auto box =
        levain::render::worldBoundsOf(mesh, instances, model).value_or(levain::render::Box{});
    for (unsigned corner = 0; corner < 8; ++corner)
    {
        const glm::vec3 local{(corner & 1u) != 0 ? 0.5f : -0.5f, (corner & 2u) != 0 ? 0.5f : -0.5f,
                              (corner & 4u) != 0 ? 0.5f : -0.5f};
        const glm::vec3 placed =
            rotation * glm::vec3(model * glm::vec4(local, 1.0f)) + glm::vec3{3.0f, 0.0f, -2.0f};
        CAPTURE(corner);
        CHECK(glm::all(glm::greaterThanEqual(placed, box.min - 1e-5f)));
        CHECK(glm::all(glm::lessThanEqual(placed, box.max + 1e-5f)));
    }
}
