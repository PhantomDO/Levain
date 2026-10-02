#include <cmath>

#include <doctest/doctest.h>
#include <glm/gtc/matrix_transform.hpp>

#include "levain/render/shadows.hpp"

namespace
{

using levain::render::Camera;
using levain::render::CascadeCount;
using levain::render::CascadeSettings;

constexpr float Aspect = 16.0f / 9.0f;

/// Une fonction et non une constante : construire un glm::vec3 peut lever en théorie, et une
/// globale ne le rattraperait pas.
glm::vec3 sunDirection()
{
    return {0.4f, 1.0f, 0.6f};
}

Camera testCamera()
{
    return Camera{.position = {3.0f, 2.0f, 8.0f},
                  .target = {0.0f, 1.0f, 0.0f},
                  .verticalFovRadians = glm::radians(60.0f),
                  .nearPlane = 0.5f,
                  .farPlane = 1000.0f};
}

} // namespace

TEST_CASE("les cascades partagent la profondeur du plan proche à la distance d'ombre")
{
    const Camera camera = testCamera();
    const auto splits = levain::render::cascadeSplitsOf(camera, {});
    CHECK(splits.front() == camera.nearPlane);
    CHECK(splits.back() == CascadeSettings{}.shadowDistance);
    for (std::uint32_t i = 0; i < CascadeCount; ++i)
    {
        CHECK(splits[i] < splits[i + 1]);
    }
    // 0 : des tranches égales ; 1 : au même rapport.
    const auto uniform = levain::render::cascadeSplitsOf(camera, {.splitBlend = 0.0f});
    CHECK(uniform[2] - uniform[1] == doctest::Approx(uniform[1] - uniform[0]));
    const auto logarithmic = levain::render::cascadeSplitsOf(camera, {.splitBlend = 1.0f});
    CHECK(logarithmic[2] / logarithmic[1] == doctest::Approx(logarithmic[1] / logarithmic[0]));
}

TEST_CASE("chaque point du volume de vue est dans la shadow map de sa cascade : pas de trou")
{
    const Camera camera = testCamera();
    const CascadeSettings settings;
    const auto splits = levain::render::cascadeSplitsOf(camera, settings);
    const auto cascades = levain::render::cascadesOf(camera, Aspect, sunDirection(), settings);
    const glm::mat4 inverseProjection = glm::inverse(levain::render::projectionOf(camera, Aspect));
    const glm::mat4 cameraToWorld = glm::inverse(levain::render::viewOf(camera));

    int checked = 0;
    // Des profondeurs jusqu'aux frontières entre cascades, et des points jusqu'aux bords de
    // l'écran.
    // Des profondeurs de 7 % en 7 %, du plan proche à la distance d'ombre.
    for (int step = 0;
         camera.nearPlane * std::pow(1.07f, static_cast<float>(step)) < settings.shadowDistance;
         ++step)
    {
        const float depth = camera.nearPlane * std::pow(1.07f, static_cast<float>(step));
        for (const float x : {-1.0f, -0.5f, 0.0f, 0.7f, 1.0f})
        {
            for (const float y : {-1.0f, 0.2f, 1.0f})
            {
                const glm::vec4 onScreen = inverseProjection * glm::vec4{x, y, 0.5f, 1.0f};
                const glm::vec3 ray = glm::vec3{onScreen} / onScreen.w;
                const glm::vec3 point =
                    glm::vec3{cameraToWorld * glm::vec4{ray / -ray.z * depth, 1.0f}};
                std::uint32_t cascade = 0;
                while (cascade + 1 < CascadeCount && depth >= splits[cascade + 1])
                {
                    ++cascade;
                }
                const glm::vec4 clip = cascades[cascade].viewProjection * glm::vec4{point, 1.0f};
                CAPTURE(depth);
                CAPTURE(cascade);
                CHECK(std::abs(clip.x) <= 1.0f);
                CHECK(std::abs(clip.y) <= 1.0f);
                CHECK(clip.z >= 0.0f);
                CHECK(clip.z <= 1.0f);
                ++checked;
            }
        }
    }
    CHECK(checked > 500);
}

TEST_CASE("une cascade garde sa taille quand la caméra tourne, et avance par texels entiers")
{
    const Camera camera = testCamera();
    Camera turned = camera;
    turned.target = camera.position + glm::vec3{-1.0f, -0.2f, 0.3f};
    const CascadeSettings settings;
    const auto before = levain::render::cascadesOf(camera, Aspect, sunDirection(), settings);
    const auto after = levain::render::cascadesOf(turned, Aspect, sunDirection(), settings);
    for (std::uint32_t i = 0; i < CascadeCount; ++i)
    {
        CAPTURE(i);
        // L'échelle de la projection : celle du rayon de la sphère, le même dans les deux sens.
        CHECK(glm::length(glm::vec3{before[i].viewProjection[0]}) ==
              doctest::Approx(glm::length(glm::vec3{after[i].viewProjection[0]})));
        // L'origine du monde tombe sur un texel entier.
        const glm::vec4 origin = after[i].viewProjection * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f};
        const glm::vec2 texels =
            glm::vec2{origin} * (static_cast<float>(settings.resolution) * 0.5f);
        CHECK(texels.x == doctest::Approx(std::round(texels.x)).epsilon(1e-3));
        CHECK(texels.y == doctest::Approx(std::round(texels.y)).epsilon(1e-3));
    }
}
