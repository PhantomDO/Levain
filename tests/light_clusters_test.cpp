#include <cmath>
#include <vector>

#include <doctest/doctest.h>

#include "levain/render/camera.hpp"
#include "levain/render/light_clusters.hpp"

namespace
{

using levain::render::ClusterBox;
using levain::render::ClusterGrid;
using levain::render::ClusterView;
using levain::render::PointLight;

ClusterView testView()
{
    const levain::render::Camera camera{.position = {0.0f, 2.0f, 6.0f},
                                        .target = {0.0f, 1.0f, 0.0f},
                                        .verticalFovRadians = glm::radians(60.0f),
                                        .nearPlane = 0.1f,
                                        .farPlane = 100.0f};
    const float aspect = 16.0f / 9.0f;
    return ClusterView{.view = levain::render::viewOf(camera),
                       .projection = levain::render::projectionOf(camera, aspect),
                       .nearPlane = camera.nearPlane,
                       .farPlane = camera.farPlane};
}

bool contains(const ClusterBox& box, glm::vec3 point)
{
    constexpr float Epsilon = 1e-4f;
    return glm::all(glm::greaterThanEqual(point, box.min - Epsilon)) &&
           glm::all(glm::lessThanEqual(point, box.max + Epsilon));
}

/// Le cluster d'un point du repère de la caméra, par le chemin qu'emprunteront les shaders : sa
/// position à l'écran (coordonnées normalisées) et sa profondeur.
glm::uvec3 cellOf(const ClusterGrid& grid, const ClusterView& view, glm::vec3 point)
{
    const glm::vec4 clip = view.projection * glm::vec4{point, 1.0f};
    const glm::vec2 ndc = glm::vec2{clip} / clip.w;
    const float depth = -point.z;
    const float slice = std::log(depth / view.nearPlane) / std::log(view.farPlane / view.nearPlane);
    return {static_cast<std::uint32_t>((ndc.x * 0.5f + 0.5f) * static_cast<float>(grid.x)),
            static_cast<std::uint32_t>((ndc.y * 0.5f + 0.5f) * static_cast<float>(grid.y)),
            static_cast<std::uint32_t>(slice * static_cast<float>(grid.z))};
}

} // namespace

TEST_CASE("les tranches vont du plan proche au plan lointain")
{
    const ClusterGrid grid;
    const ClusterView view = testView();
    CHECK(levain::render::sliceDepthOf(grid, view, 0) == doctest::Approx(view.nearPlane));
    CHECK(levain::render::sliceDepthOf(grid, view, grid.z) == doctest::Approx(view.farPlane));
    // Logarithmiques : chaque tranche est plus épaisse que la précédente.
    for (std::uint32_t slice = 1; slice < grid.z; ++slice)
    {
        const float before = levain::render::sliceDepthOf(grid, view, slice) -
                             levain::render::sliceDepthOf(grid, view, slice - 1);
        const float after = levain::render::sliceDepthOf(grid, view, slice + 1) -
                            levain::render::sliceDepthOf(grid, view, slice);
        CHECK(after > before);
    }
}

TEST_CASE("un point devant la caméra est dans la boîte de son cluster")
{
    const ClusterGrid grid;
    const ClusterView view = testView();
    // Des points répartis dans le volume de vue, à toutes les profondeurs.
    for (const float depth : {0.2f, 1.0f, 7.5f, 42.0f, 99.0f})
    {
        for (const float x : {-0.9f, -0.31f, 0.0f, 0.47f, 0.88f})
        {
            for (const float y : {-0.8f, 0.13f, 0.77f})
            {
                // Un point de l'écran (x, y), reporté à la profondeur voulue.
                const glm::vec4 onScreen =
                    glm::inverse(view.projection) * glm::vec4{x, y, 0.5f, 1.0f};
                const glm::vec3 ray = glm::vec3{onScreen} / onScreen.w;
                const glm::vec3 point = ray / -ray.z * depth;
                const glm::uvec3 cell = cellOf(grid, view, point);
                CAPTURE(depth);
                CAPTURE(x);
                CAPTURE(y);
                CHECK(contains(levain::render::clusterBoxOf(grid, view, cell), point));
            }
        }
    }
}

TEST_CASE("une sphère touche une boîte jusqu'à son rayon, pas au-delà")
{
    const ClusterBox box{.min = {0.0f, 0.0f, -2.0f}, .max = {1.0f, 1.0f, -1.0f}};
    CHECK(levain::render::sphereTouchesBox({0.5f, 0.5f, -1.5f}, 0.1f, box)); // dedans
    CHECK(levain::render::sphereTouchesBox({2.0f, 0.5f, -1.5f}, 1.0f, box)); // au contact
    CHECK_FALSE(levain::render::sphereTouchesBox({2.0f, 0.5f, -1.5f}, 0.9f, box));
    // Près d'un coin, la distance est en diagonale : 1 + 1 > 1,2².
    CHECK_FALSE(levain::render::sphereTouchesBox({2.0f, 2.0f, -1.5f}, 1.2f, box));
}

TEST_CASE("une lumière se range dans le cluster où elle est, et pas à l'autre bout")
{
    const ClusterGrid grid;
    const ClusterView view = testView();
    // Une petite lumière juste devant la caméra, au centre de l'écran.
    const glm::vec3 inView{0.0f, 0.0f, -3.0f};
    const glm::vec3 world = glm::vec3{glm::inverse(view.view) * glm::vec4{inView, 1.0f}};
    const std::vector<PointLight> lights{PointLight{.position = world, .range = 0.05f}};
    const auto perCluster = levain::render::lightsPerClusterOf(grid, view, lights);

    REQUIRE(perCluster.size() == levain::render::clusterCountOf(grid));
    const std::uint32_t home = levain::render::clusterIndexOf(grid, cellOf(grid, view, inView));
    CHECK(perCluster[home] == std::vector<std::uint32_t>{0});
    // Au plus quelques voisins (la lumière peut chevaucher une frontière), jamais tout l'écran.
    std::size_t touched = 0;
    for (const auto& list : perCluster)
    {
        touched += list.size();
    }
    CHECK(touched <= 8);
    CHECK(perCluster[levain::render::clusterIndexOf(grid, {0, 0, grid.z - 1})].empty());
}
