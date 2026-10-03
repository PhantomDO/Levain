#include <array>

#include <doctest/doctest.h>
#include <glm/gtc/matrix_transform.hpp>

#include "levain/render/camera.hpp"
#include "levain/render/culling.hpp"

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
    for (int i = 0; i < 8; ++i)
    {
        corners[i] = glm::vec3{transform * glm::vec4{(i & 1) != 0 ? box.max.x : box.min.x,
                                                     (i & 2) != 0 ? box.max.y : box.min.y,
                                                     (i & 4) != 0 ? box.max.z : box.min.z, 1.0f}};
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
