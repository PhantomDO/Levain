#include <ostream>

#include <doctest/doctest.h>
#include <glm/glm.hpp>

#include "levain/render/camera.hpp"

using levain::render::Camera;
using levain::render::viewProjectionOf;

namespace
{

/// Position dans l'espace normalisé de l'écran (NDC) : x et y de −1 à 1, profondeur z de 0 à 1.
glm::vec3 toNormalizedDevice(const glm::mat4& viewProjection, const glm::vec3& point)
{
    const glm::vec4 clip = viewProjection * glm::vec4{point, 1.0f};
    return glm::vec3{clip} / clip.w;
}

} // namespace

TEST_CASE("viewProjectionOf place la cible au centre de l'écran")
{
    const Camera camera;
    const glm::vec3 target =
        toNormalizedDevice(viewProjectionOf(camera, 16.0f / 9.0f), camera.target);

    CHECK(target.x == doctest::Approx(0.0f));
    CHECK(target.y == doctest::Approx(0.0f));
}

TEST_CASE("viewProjectionOf range la profondeur de 0 à 1, comme Vulkan et Direct3D 12")
{
    // Avec la convention d'OpenGL (glm::perspective), le plan proche tomberait à −1 : la moitié
    // proche de la scène sortirait du volume de vue.
    const Camera camera{.position = {0.0f, 0.0f, 0.0f},
                        .target = {0.0f, 0.0f, -1.0f},
                        .verticalFovRadians = glm::radians(60.0f),
                        .nearPlane = 0.1f,
                        .farPlane = 100.0f};
    const glm::mat4 viewProjection = viewProjectionOf(camera, 1.0f);

    CHECK(toNormalizedDevice(viewProjection, {0.0f, 0.0f, -0.1f}).z == doctest::Approx(0.0f));
    CHECK(toNormalizedDevice(viewProjection, {0.0f, 0.0f, -100.0f}).z == doctest::Approx(1.0f));
}

TEST_CASE("le rayon par un point de l'écran : au centre, vers la cible, et ailleurs il revient au "
          "même point")
{
    const levain::render::Camera camera{.position = {2.0f, 3.0f, 8.0f},
                                        .target = {0.0f, 1.0f, 0.0f}};
    constexpr float Aspect = 16.0f / 9.0f;

    // Le centre de l'image (960, 540 sur 1920 × 1080) vise la cible.
    const glm::vec2 center = levain::render::ndcOfPixel({960.0f, 540.0f}, 1920.0f, 1080.0f);
    CHECK(center.x == doctest::Approx(0.0f));
    CHECK(center.y == doctest::Approx(0.0f));
    const levain::render::CameraRay ray = levain::render::rayThrough(camera, Aspect, center);
    const glm::vec3 toTarget = glm::normalize(camera.target - camera.position);
    CHECK(glm::dot(ray.direction, toTarget) == doctest::Approx(1.0f));

    // Le haut de l'écran est en y positif : le piège du sens de y.
    CHECK(levain::render::ndcOfPixel({0.0f, 0.0f}, 1920.0f, 1080.0f).y == doctest::Approx(1.0f));

    // Un point quelconque du rayon se projette au point de l'écran d'où il part.
    const glm::vec2 ndc{0.4f, -0.7f};
    const levain::render::CameraRay corner = levain::render::rayThrough(camera, Aspect, ndc);
    const glm::vec4 clip = levain::render::viewProjectionOf(camera, Aspect) *
                           glm::vec4{corner.origin + corner.direction * 5.0f, 1.0f};
    CHECK(clip.x / clip.w == doctest::Approx(ndc.x));
    CHECK(clip.y / clip.w == doctest::Approx(ndc.y));

    // Du plan proche au plan lointain : la profondeur du frustum au centre, plus dans un coin.
    CHECK(ray.length == doctest::Approx(camera.farPlane - camera.nearPlane).epsilon(0.001));
    CHECK(corner.length > ray.length);
}
