// La grille au sol et le menu « Afficher » de la Vue (ADR-0036, décisions 13 et 15, morceau 17) :
// de la logique pure, sans fenêtre ni GPU. Chaque propriété a son cas, et chacun rougit quand sa
// règle disparaît (les contre-tests sont listés dans la PR du morceau).

#include <algorithm>
#include <cmath>

#include <doctest/doctest.h>
#include <glm/glm.hpp>

#include "levain/editor/grid.hpp"
#include "levain/render/camera.hpp"
#include "levain/render/culling.hpp"
#include "levain/render/debug_lines.hpp"

namespace render = levain::render;
using namespace levain::editor;

namespace
{

/// La vue d'une caméra en `position` : exposition 1, plan lointain à 1 km (celui du sandbox).
GridView viewAt(const glm::vec3& position, float exposure = 1.0f, float farPlane = 1000.0f)
{
    return {.cameraPosition = position, .exposure = exposure, .farPlane = farPlane};
}

/// Les lignes de la grille pour une caméra en `position`.
std::vector<levain::render::DebugLine> linesAt(const glm::vec3& position, float exposure = 1.0f,
                                               float farPlane = 1000.0f)
{
    return gridLinesFor(viewAt(position, exposure, farPlane));
}

/// Les lignes de la seule famille de la hauteur (`gridLevelsFor`, la deuxième) pour une caméra en
/// `position` : un seul écart, comme avant le fondu.
std::vector<levain::render::DebugLine> heightLevelLinesAt(const glm::vec3& position)
{
    return gridLevelLines(viewAt(position), gridLevelsFor(position.y)[1]);
}

} // namespace

TEST_CASE("la grille a un nombre de lignes borné, très en dessous de ce que la passe accepte")
{
    // 5 000 m est au-dessus du plan lointain (1 km) : le sol entier est trop loin, rien à dessiner.
    for (const float height : {0.0f, 0.3f, 2.0f, 60.0f, 140.0f, 5000.0f, 1.0e9f})
    {
        const auto lines = linesAt({3.0f, height, -7.0f});
        CHECK(lines.size() <= MaxGridLines);
        CHECK(lines.size() < levain::render::MaxDebugLines / 10);
        CHECK((height < 900.0f) == !lines.empty());
    }
}

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
    // Une famille invisible n'envoie aucune ligne : celle de 10 m à 1 m de haut, dont l'opacité est
    // nulle, ne remplirait que le buffer.
    CHECK(gridLevelLines(viewAt({0.0f, 1.0f, 0.0f}), start[2]).empty());
    CHECK_FALSE(gridLevelLines(viewAt({0.0f, 1.0f, 0.0f}), start[1]).empty());
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

TEST_CASE("les axes X et Z passent par l'origine, rouge et bleu, les autres lignes sont grises")
{
    const glm::vec3 camera{0.0f, 6.0f, 0.0f};
    const auto lines = linesAt(camera);
    int red = 0;
    int blue = 0;
    for (const auto& line : lines)
    {
        const bool alongX = line.from.z == line.to.z; // le long de X, à z constant
        const bool alongZ = line.from.x == line.to.x;
        if (alongX && line.from.z == 0.0f)
        {
            ++red;
            CHECK(line.color.r > 5.0f * line.color.g);
            CHECK(line.color.r > 5.0f * line.color.b);
        }
        else if (alongZ && line.from.x == 0.0f)
        {
            ++blue;
            CHECK(line.color.b > 3.0f * line.color.r);
        }
        else
        {
            CHECK(line.color.r == line.color.g);
            CHECK(line.color.g == line.color.b);
        }
    }
    // Chaque axe est coupé en `GridSegments` tronçons, par chacune des familles qui se montrent.
    int visibleLevels = 0;
    for (const GridLevel& level : gridLevelsFor(camera.y))
    {
        visibleLevels += gridLevelLines(viewAt(camera), level).empty() ? 0 : 1;
    }
    CHECK(visibleLevels == 3);
    CHECK(red == visibleLevels * GridSegments);
    CHECK(blue == visibleLevels * GridSegments);
    // Loin de l'origine, la grille ne montre plus d'axe.
    for (const auto& line : linesAt({500.0f, 6.0f, 500.0f}))
    {
        CHECK(line.color.r == line.color.g);
    }
}

TEST_CASE("un gris moyen : la ligne assombrit une case claire et éclaircit une case sombre")
{
    // Les cases du bac à sable, en lumière linéaire : 214 et 99 à l'écran (captures de la
    // relecture du morceau 17c), soit environ 0,68 et 0,125. Une ligne plus claire que le sol
    // clair se perdait dans les cases claires (215 contre 214, l'AgX écrase les hautes lumières)
    // et ne faisait que des tirets dans les sombres.
    constexpr float LightSquare = 0.68f;
    constexpr float DarkSquare = 0.125f;
    constexpr float Visible = 0.12f; // l'écart de luminosité linéaire qu'on lit sans chercher
    // Une ligne mineure, pleine au centre : la famille de 10 cm à 1 m de haut, sa ligne x = 0,1.
    const auto lines = linesAt({0.0f, 1.0f, 0.0f});
    const render::DebugLine* minor = nullptr;
    for (const auto& line : lines)
    {
        if (line.from.x == 0.1f && line.to.x == 0.1f && line.to.z == 0.0f)
        {
            minor = &line;
        }
    }
    REQUIRE(minor != nullptr);
    const float grey = minor->color.r;
    const float alpha = minor->alphaTo;
    CHECK(alpha > 0.6f);
    CHECK(grey * alpha + LightSquare * (1.0f - alpha) < LightSquare - Visible);
    CHECK(grey * alpha + DarkSquare * (1.0f - alpha) > DarkSquare + Visible);
}

TEST_CASE("la couleur suit l'inverse de l'exposition : même luminosité à l'écran")
{
    const auto at = [](float exposure)
    { return linesAt({0.0f, 6.0f, 0.0f}, exposure).front().color.r; };
    const float normal = at(1.0f);
    CHECK(normal > 0.0f);
    CHECK(at(2.0f) == doctest::Approx(normal / 2.0f));
    CHECK(at(0.25f) == doctest::Approx(normal * 4.0f));
    // Une exposition nulle ou négative ne divise pas par zéro.
    CHECK(std::isfinite(at(0.0f)));
    CHECK(at(-3.0f) == at(0.0f));
}

TEST_CASE("la grille s'estompe par l'opacité avec la distance, sans jamais noircir")
{
    const auto lines = heightLevelLinesAt({0.0f, 6.0f, 0.0f});
    float nearest = 1.0e30f;
    float alphaAtCenter = 0.0f;
    float edgeAlpha = 1.0f;
    float edgeReach = 0.0f;
    for (const auto& line : lines)
    {
        // Les couleurs ne varient pas avec la distance, seule l'opacité le fait.
        CHECK(line.alphaFrom >= 0.0f);
        CHECK(line.alphaFrom <= 1.0f);
        CHECK(line.alphaTo >= 0.0f);
        CHECK(line.alphaTo <= 1.0f);
        CHECK(std::max({line.color.r, line.color.g, line.color.b}) >= 0.2f); // jamais vers le noir
        if (line.from.x == 1.0f && line.to.x == 1.0f && std::abs(line.from.z) < nearest)
        {
            nearest = std::abs(line.from.z);
            alphaAtCenter = line.alphaFrom;
        }
        // Le bout le plus loin de la ligne à x = 1 : sur la corde du disque, transparent.
        if (line.from.x == 1.0f && line.to.x == 1.0f && line.to.z > edgeReach)
        {
            edgeReach = line.to.z;
            edgeAlpha = line.alphaTo;
        }
    }
    CHECK(alphaAtCenter > 0.4f);
    CHECK(edgeReach > 40.0f); // la ligne va loin du centre
    CHECK(edgeAlpha ==
          doctest::Approx(0.0f).epsilon(1.0e-3)); // au bord de la portée : transparente
    // Dans la longueur : l'opacité décroît d'un tronçon au suivant en s'éloignant du centre.
    float previous = 2.0f;
    for (const auto& line : lines)
    {
        if (line.from.x == 1.0f && line.to.x == 1.0f && line.from.z >= 0.0f)
        {
            CHECK(line.alphaFrom <= previous);
            CHECK(line.alphaTo <= line.alphaFrom);
            previous = line.alphaTo;
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

TEST_CASE("la grille est centrée sur le point du sol sous la caméra, calé sur le pas")
{
    const glm::vec3 camera{23.4f, 6.0f, -81.7f}; // pas mineur 1 : centre (23, -82)
    const auto lines = heightLevelLinesAt(camera);
    float minX = 1.0e30f, maxX = -1.0e30f, minZ = 1.0e30f, maxZ = -1.0e30f;
    for (const auto& line : lines)
    {
        minX = std::min({minX, line.from.x, line.to.x});
        maxX = std::max({maxX, line.from.x, line.to.x});
        minZ = std::min({minZ, line.from.z, line.to.z});
        maxZ = std::max({maxZ, line.from.z, line.to.z});
        // Au ras du sol, sans le toucher : relevée d'un rien pour gagner le test de profondeur.
        CHECK(line.from.y > 0.0f);
        CHECK(line.from.y < 0.01f);
        CHECK(line.to.y == line.from.y);
    }
    CHECK((minX + maxX) / 2.0f == doctest::Approx(23.0f));
    CHECK((minZ + maxZ) / 2.0f == doctest::Approx(-82.0f));
    CHECK(std::abs(camera.x - (minX + maxX) / 2.0f) <= 1.0f);
    // Les lignes restent aux multiples du pas : elles ne glissent pas avec la caméra.
    for (const auto& line : lines)
    {
        const float across = line.from.x == line.to.x ? line.from.x : line.from.z;
        CHECK(across == std::round(across));
    }
}
