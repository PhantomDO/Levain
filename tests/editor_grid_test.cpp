// La grille au sol et le menu « Afficher » de la Vue (ADR-0036, décisions 13 et 15, morceau 17) :
// de la logique pure, sans fenêtre ni GPU. Chaque propriété a son cas, et chacun rougit quand sa
// règle disparaît (les contre-tests sont listés dans la PR du morceau).

#include <cmath>

#include <doctest/doctest.h>
#include <glm/glm.hpp>

#include "levain/editor/grid.hpp"
#include "levain/render/camera.hpp"
#include "levain/render/culling.hpp"

namespace render = levain::render;
using namespace levain::editor;

TEST_CASE("l'écart des lignes suit la hauteur de la caméra par puissances de 10")
{
    CHECK(gridSpacingFor(0.5f).minor == doctest::Approx(0.1f));
    CHECK(gridSpacingFor(2.0f).minor == doctest::Approx(1.0f));
    CHECK(gridSpacingFor(6.0f).minor == doctest::Approx(1.0f));
    CHECK(gridSpacingFor(60.0f).minor == doctest::Approx(10.0f));
    CHECK(gridSpacingFor(60.0f).major == doctest::Approx(100.0f));
    // Sous le sol, la hauteur compte en valeur absolue ; à zéro l'écart ne s'effondre pas.
    CHECK(gridSpacingFor(-60.0f).minor == doctest::Approx(10.0f));
    CHECK(gridSpacingFor(0.0f).minor == doctest::Approx(0.1f));
}

TEST_CASE("trois familles de lignes : la plus fine s'efface quand la caméra monte, la plus grosse "
          "apparaît")
{
    // À 1 m (le début de l'ordre) : 10 cm pleins et mineurs, 1 m pleins et majeurs, 10 m absents.
    const auto start = gridLevelsFor(1.0f);
    CHECK(start[0].step == doctest::Approx(0.1f));
    CHECK(start[0].visibility == doctest::Approx(1.0f));
    CHECK(start[0].role == doctest::Approx(0.0f));
    CHECK(start[1].step == doctest::Approx(1.0f));
    CHECK(start[1].visibility == doctest::Approx(1.0f));
    CHECK(start[1].role == doctest::Approx(1.0f));
    CHECK(start[2].step == doctest::Approx(10.0f));
    CHECK(start[2].visibility == doctest::Approx(0.0f).epsilon(1.0e-3));
    // À mi-ordre (√10 m) : le fondu est à moitié.
    const auto middle = gridLevelsFor(std::sqrt(10.0f));
    CHECK(middle[0].visibility == doctest::Approx(0.5f));
    CHECK(middle[1].role == doctest::Approx(0.5f));
    CHECK(middle[2].visibility == doctest::Approx(0.5f));
    // Juste avant 10 m : 1 m mineur, 10 m presque pleins, 10 cm disparus.
    const auto end = gridLevelsFor(9.999f);
    CHECK(end[0].visibility == doctest::Approx(0.0f).epsilon(1.0e-3));
    CHECK(end[1].role == doctest::Approx(0.0f).epsilon(1.0e-3));
    CHECK(end[2].visibility == doctest::Approx(1.0f).epsilon(1.0e-3));
    // L'écart nominal est celui de la famille de la hauteur, et son majeur celui de la plus grosse.
    CHECK(gridSpacingFor(6.0f).minor == gridLevelsFor(6.0f)[1].step);
    CHECK(gridSpacingFor(6.0f).major == gridLevelsFor(6.0f)[2].step);
}

TEST_CASE("à une puissance de 10 de la hauteur, aucune famille ne saute")
{
    struct Look
    {
        float visibility = 0.0f;
        float role = 0.0f;
    };

    // Ce que la grille donne à la famille d'écart `step` : rien si elle n'y est pas.
    const auto lookOf = [](float height, float step)
    {
        for (const GridLevel& level : gridLevelsFor(height))
        {
            if (std::abs(level.step - step) < 1.0e-4f * step)
            {
                return Look{level.visibility, level.role};
            }
        }
        return Look{};
    };
    for (const float decade : {0.1f, 1.0f, 10.0f, 100.0f, 1000.0f})
    {
        const float below = decade * (1.0f - 1.0e-4f);
        const float above = decade * (1.0f + 1.0e-4f);
        // Les écarts de 10 fois moins à 100 fois plus : les familles qui peuvent se montrer.
        for (const float step :
             {decade / 100.0f, decade / 10.0f, decade, decade * 10.0f, decade * 100.0f})
        {
            const Look before = lookOf(below, step);
            const Look after = lookOf(above, step);
            CHECK(std::abs(before.visibility - after.visibility) < 2.0e-3f);
            if (before.visibility > 0.05f && after.visibility > 0.05f)
            {
                CHECK(std::abs(before.role - after.role) < 2.0e-3f);
            }
        }
    }
}

TEST_CASE("la portée de la grille est celle que le plan lointain laisse voir")
{
    // 1 km de plan lointain, une caméra à 2 m : 50 pas de 1 m, moins la marge du calage.
    CHECK(gridReachFor(1.0f, 2.0f, 1000.0f) == doctest::Approx(50.0f));
    // À 140 m (pas de 100 m, 5 km de portée sans limite) : le sol visible est à √(900² − 140²) =
    // 889 m, moins 0,71 pas.
    CHECK(gridReachFor(100.0f, 140.0f, 1000.0f) == doctest::Approx(889.1f - 71.0f).epsilon(0.002));
    // Une caméra au-dessus des neuf dixièmes du plan lointain, ou un plan lointain absurde : rien.
    CHECK(gridReachFor(100.0f, 950.0f, 1000.0f) == 0.0f);
    CHECK(gridReachFor(1.0f, 2.0f, 0.0f) == 0.0f);
    CHECK(gridReachFor(1.0f, 2.0f, std::nanf("")) == 0.0f);
    CHECK(gridReachFor(1.0f, 2.0f, -5.0f) == 0.0f);
}

TEST_CASE("la distance au plan lointain est celle de l'objectif, quel que soit le regard")
{
    for (const glm::vec3 target :
         {glm::vec3{0.0f}, glm::vec3{40.0f, 2.0f, -60.0f}, glm::vec3{0.0f, -30.0f, 5.0f}})
    {
        const render::Camera camera{.position = {3.0f, 20.0f, 7.0f},
                                    .target = target,
                                    .verticalFovRadians = glm::radians(60.0f),
                                    .nearPlane = 0.5f,
                                    .farPlane = 750.0f};
        const render::Frustum frustum =
            render::frustumOf(render::viewProjectionOf(camera, 16.0f / 9.0f));
        CHECK(farDistanceOf(frustum, camera.position) == doctest::Approx(750.0f).epsilon(1.0e-4));
    }
}
