// La grille au sol et le menu « Afficher » de la Vue (ADR-0036, décisions 13 et 15, morceau 17) :
// de la logique pure, sans fenêtre ni GPU. Chaque propriété a son cas, et chacun rougit quand sa
// règle disparaît (les contre-tests sont listés dans la PR du morceau).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

#include <doctest/doctest.h>
#include <glm/glm.hpp>

#include "levain/editor/actions.hpp"
#include "levain/editor/grid.hpp"
#include "levain/editor/mode.hpp"
#include "levain/editor/shortcuts.hpp"
#include "levain/editor/show.hpp"
#include "levain/render/camera.hpp"
#include "levain/render/culling.hpp"
#include "levain/render/debug_lines.hpp"

namespace render = levain::render;
using namespace levain::editor;

namespace
{

constexpr std::uint16_t ScanG = 10, ScanF = 9;

/// La vue d'une caméra en `position` : exposition 1, plan lointain à 1 km (celui du sandbox), sans
/// image (les tests de la géométrie ne mesurent pas l'écart des lignes à l'écran).
GridView viewAt(const glm::vec3& position, float exposure = 1.0f, float farPlane = 1000.0f)
{
    return {.cameraPosition = position,
            .exposure = exposure,
            .farPlane = farPlane,
            .screen = std::nullopt};
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

/// Ce qu'on voit d'une ligne le long de Z à `x`, au point où elle passe par z = 0 (la caméra est
/// sur l'axe z = 0, le centre de chaque famille aussi) : la couleur la plus claire des familles qui
/// la tracent, et leurs opacités **composées**, comme le mélange du GPU (1 − Π(1 − opacité)).
struct Ink
{
    float brightness = 0.0f;
    float opacity = 0.0f;
};

Ink inkOfZLineAt(const std::vector<levain::render::DebugLine>& lines, float x)
{
    Ink ink;
    float transparent = 1.0f;
    for (const auto& line : lines)
    {
        if (line.from.x == x && line.to.x == x && line.to.z == 0.0f)
        {
            ink.brightness = std::max(ink.brightness, line.color.r);
            transparent *= 1.0f - line.alphaTo;
        }
    }
    ink.opacity = 1.0f - transparent;
    return ink;
}

/// La plus grande distance, le long d'un axe, entre deux extrémités de lignes.
float spanOf(const std::vector<levain::render::DebugLine>& lines, int axis)
{
    float low = 1.0e30f;
    float high = -1.0e30f;
    for (const auto& line : lines)
    {
        low = std::min({low, line.from[axis], line.to[axis]});
        high = std::max({high, line.from[axis], line.to[axis]});
    }
    return high - low;
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
    // Et la grille entière suit : plus haut, elle couvre plus de sol (sans limite de plan
    // lointain).
    CHECK(spanOf(linesAt({0, 60.0f, 0}, 1.0f, 1.0e6f), 0) >
          9.0f * spanOf(linesAt({0, 6.0f, 0}, 1.0f, 1.0e6f), 0));
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

TEST_CASE("à 10 m de haut, les lignes ne sautent pas : mêmes lignes, même encre, des deux côtés")
{
    // 9,99 m : un écart nominal de 1 m ; 10,01 m : de 10 m. Les lignes du mètre, celles de 10 m
    // et celles de 20 m ont la même encre des deux côtés ; celles de 5 dixièmes, qui n'ont que la
    // famille de 10 cm, s'effacent.
    const auto before = linesAt({0.3f, 9.99f, 0.2f});
    const auto after = linesAt({0.3f, 10.01f, 0.2f});
    for (const float x : {1.0f, 3.0f, 10.0f, -10.0f, 20.0f, 100.0f})
    {
        const Ink was = inkOfZLineAt(before, x);
        const Ink is = inkOfZLineAt(after, x);
        CHECK(was.opacity > 0.1f);
        CHECK(is.opacity == doctest::Approx(was.opacity).epsilon(0.02));
        CHECK(is.brightness == doctest::Approx(was.brightness).epsilon(0.02));
    }
    CHECK(inkOfZLineAt(before, 0.5f).opacity < 0.01f);
    CHECK(inkOfZLineAt(after, 0.5f).opacity == 0.0f);
    // Et le nombre de lignes ne change pas d'un coup (avant : 2 020 d'un côté, 2 020 de l'autre,
    // mais de 1 m puis de 10 m : le sol couvert passait de 100 m à 1 km).
    CHECK(spanOf(before, 0) == doctest::Approx(spanOf(after, 0)).epsilon(0.02));
}

TEST_CASE("l'axe X à 60 m devant reste visible juste sous et juste au-dessus de 10 m de haut")
{
    // À 9,9 m l'écart était de 1 m et la portée de 50 m : l'axe à 60 m disparaissait, puis revenait
    // à 10,1 m. La famille de 10 m, qui se montre dès avant, le garde.
    for (const float height : {9.9f, 10.1f})
    {
        float transparent = 1.0f;
        for (const auto& line : linesAt({0.0f, height, 60.0f}))
        {
            // Le tronçon de l'axe X (z = 0) qui finit à x = 0 : le plus près de la caméra.
            if (line.from.z == 0.0f && line.to.z == 0.0f && line.to.x == 0.0f)
            {
                transparent *= 1.0f - line.alphaTo;
            }
        }
        CHECK(1.0f - transparent > 0.5f);
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

TEST_CASE("les lignes majeures sont plus opaques que les mineures, du même gris")
{
    // À 9,5 m la famille de 10 m est presque pleine (visibilité 0,98) : x = ±10 est majeure, x = 9
    // et x = −11 ne sont que des lignes du mètre.
    const auto lines = linesAt({0.0f, 9.5f, 0.0f});
    for (const auto& [major, minor] :
         {std::pair{10.0f, 9.0f}, std::pair{-10.0f, -11.0f}, std::pair{20.0f, 21.0f}})
    {
        CHECK(inkOfZLineAt(lines, major).opacity > inkOfZLineAt(lines, minor).opacity);
        CHECK(inkOfZLineAt(lines, major).brightness == inkOfZLineAt(lines, minor).brightness);
    }
    // Et chacune a l'opacité propre de son rôle, sans compter ce que la composition de deux
    // familles ajoute : la ligne x = 100, que seule la famille de 10 m trace (le mètre s'arrête à
    // 50 m), contre une ligne du mètre, x = 9.
    const GridView view = viewAt({0.0f, 9.5f, 0.0f});
    const std::array<GridLevel, GridLevelCount> levels = gridLevelsFor(9.5f);
    const auto ownAlpha = [&view](const GridLevel& level, float x)
    {
        const float reach = gridReachFor(level.step, view.cameraPosition.y, view.farPlane);
        for (const auto& line : gridLevelLines(view, level))
        {
            if (line.from.x == x && line.to.x == x && line.to.z == 0.0f)
            {
                return line.alphaTo / ((1.0f - std::abs(x) / reach) * level.visibility);
            }
        }
        return 0.0f;
    };
    CHECK(ownAlpha(levels[2], 100.0f) > ownAlpha(levels[1], 9.0f) * 1.2f);
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

TEST_CASE("rien de la grille ne dépasse les neuf dixièmes du plan lointain")
{
    // Les caméras à des hauteurs de chaque ordre de grandeur, calées ou non sur le pas (le centre
    // s'écarte de la caméra d'au plus 0,71 pas), et deux plans lointains. Chaque extrémité de ligne
    // est à moins de 0,9 · lointain de l'œil, et là même où l'opacité n'est pas nulle : aucune
    // ligne n'est coupée net par le plan lointain.
    for (const float farPlane : {300.0f, 1000.0f})
    {
        for (const float height : {0.5f, 2.0f, 25.0f, 99.0f, 140.0f, 450.0f, 800.0f})
        {
            for (const glm::vec3 offset :
                 {glm::vec3{0.0f}, glm::vec3{0.49f, 0.0f, 0.49f}, glm::vec3{-0.5f, 0.0f, 0.5f}})
            {
                const float step = gridSpacingFor(height).minor;
                const glm::vec3 camera = glm::vec3{offset.x * step + 37.0f * step, height,
                                                   offset.z * step - 81.0f * step};
                for (const auto& line : linesAt(camera, 1.0f, farPlane))
                {
                    for (const glm::vec3& end : {line.from, line.to})
                    {
                        CHECK(glm::distance(end, camera) <=
                              doctest::Approx(GridFarFraction * farPlane).epsilon(1.0e-4));
                    }
                }
            }
        }
    }
}

TEST_CASE("à 140 m de haut, la grille est visible et ses lignes s'arrêtent avant le plan lointain")
{
    // La caméra par défaut de la vallée : 140 m. Le pas est de 100 m, et sans limite la portée de
    // 5 km faisait une clôture à 1 km.
    const auto lines = linesAt({30.0f, 140.0f, 480.0f});
    CHECK_FALSE(lines.empty());
    float farthest = 0.0f;
    for (const auto& line : lines)
    {
        farthest = std::max({farthest, glm::distance(line.from, glm::vec3{30.0f, 140.0f, 480.0f}),
                             glm::distance(line.to, glm::vec3{30.0f, 140.0f, 480.0f})});
    }
    CHECK(farthest <= 900.1f);
    CHECK(farthest > 600.0f); // et elle va tout de même loin
}

TEST_CASE("l'opacité suit l'écart des lignes à l'écran : rien sous 2 pixels, tout à partir de 6")
{
    CHECK(spacingFade(0.0f) == 0.0f);
    CHECK(spacingFade(1.9f) == 0.0f);
    CHECK(spacingFade(GridMinPixels) == 0.0f);
    CHECK(spacingFade(GridMinPixels + GridFadePixels / 2.0f) == doctest::Approx(0.5f));
    CHECK(spacingFade(GridMinPixels + GridFadePixels) == 1.0f);
    CHECK(spacingFade(500.0f) == 1.0f);
    CHECK(spacingFade(std::numeric_limits<float>::infinity()) == 1.0f);
}

TEST_CASE("l'écart de deux lignes voisines à l'écran se mesure en pixels, perpendiculairement")
{
    // À l'horizontale, 60° de champ vertical, 1 080 pixels : f = 540 / tan 30° ≈ 935,3.
    const render::Camera camera{.position = {0.0f, 2.0f, 0.0f}, .target = {0.0f, 2.0f, -1.0f}};
    const GridScreen screen{.viewProjection = render::viewProjectionOf(camera, 16.0f / 9.0f),
                            .pixels = {1920.0f, 1080.0f}};
    const float focal = 540.0f / std::tan(glm::radians(30.0f));
    const glm::vec3 alongX{1.0f, 0.0f, 0.0f};
    const glm::vec3 alongZ{0.0f, 0.0f, 1.0f};
    // Les lignes le long de Z, à 20 m, 1 m l'une de l'autre : f / 20 pixels d'écart, quelle que
    // soit leur place. Ici la ligne x = 0 (au sol, sous l'axe de la caméra) est droite à l'écran.
    CHECK(projectedSpacing({0.0f, 0.0f, -20.0f}, alongZ, {1.0f, 0.0f, 0.0f}, screen) ==
          doctest::Approx(focal / 20.0f).epsilon(0.001));
    // Deux fois plus loin, deux fois plus serré.
    CHECK(projectedSpacing({0.0f, 0.0f, -40.0f}, alongZ, {1.0f, 0.0f, 0.0f}, screen) ==
          doctest::Approx(focal / 40.0f).epsilon(0.001));
    // Les lignes le long de X, au sol à 20 m : l'écart à l'écran est celui de la hauteur, f · h ·
    // pas / (d · (d + pas)) = 935 · 2 / 420 : **pas la distance de deux points**, qui compterait en
    // plus le décalage de côté (11 pixels à 5 m du centre) et ferait paraître ces lignes trois fois
    // moins serrées qu'elles ne le sont.
    const float flat = focal * 2.0f / (20.0f * 21.0f);
    CHECK(projectedSpacing({5.0f, 0.0f, -20.0f}, alongX, {0.0f, 0.0f, -1.0f}, screen) ==
          doctest::Approx(flat).epsilon(0.001));
    CHECK(projectedSpacing({-30.0f, 0.0f, -20.0f}, alongX, {0.0f, 0.0f, -1.0f}, screen) ==
          doctest::Approx(flat).epsilon(0.001));
    // Une ligne vue par le bout (celle de l'axe de la caméra, un point à l'écran) : les deux
    // points.
    CHECK(projectedSpacing({0.0f, 2.0f, -20.0f}, alongZ, {1.0f, 0.0f, 0.0f}, screen) ==
          doctest::Approx(focal / 20.0f).epsilon(0.001));
    // Derrière l'œil, sa voisine, ou le bout de la ligne : rien à mesurer, l'infini.
    CHECK(projectedSpacing({0.0f, 2.0f, 5.0f}, alongZ, {1.0f, 0.0f, 0.0f}, screen) ==
          std::numeric_limits<float>::infinity());
    CHECK(projectedSpacing({0.0f, 2.0f, -0.00005f}, alongZ, {1.0f, 0.0f, 20.0f}, screen) ==
          std::numeric_limits<float>::infinity());
    CHECK(projectedSpacing({0.0f, 0.0f, -0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, screen) ==
          std::numeric_limits<float>::infinity());
}

TEST_CASE("les lignes trop serrées pour l'écran s'effacent, les autres gardent leur opacité")
{
    // Une caméra à 2 m, à l'horizontale : les lignes le long de X, vues de biais, se serrent vite
    // (f · h · pas / d², sous 2 pixels vers 30 m pour un pas de 1 m) ; celles le long de Z
    // convergent plus loin. Chacune des trois familles, avec son propre pas.
    const glm::vec3 position{0.0f, 2.0f, 0.0f};
    const render::Camera camera{.position = position, .target = {0.0f, 2.0f, -1.0f}};
    const GridView withScreen{
        .cameraPosition = position,
        .exposure = 1.0f,
        .farPlane = 1000.0f,
        .screen = GridScreen{.viewProjection = render::viewProjectionOf(camera, 16.0f / 9.0f),
                             .pixels = {1920.0f, 1080.0f}}};
    GridView without = withScreen;
    without.screen.reset();
    int tight = 0;
    int wide = 0;
    // Chaque famille a son écart : sa voisine est à `level.step` mètres.
    for (const GridLevel& level : gridLevelsFor(position.y))
    {
        const auto faded = gridLevelLines(withScreen, level);
        const auto plain = gridLevelLines(without, level);
        REQUIRE(faded.size() == plain.size());
        for (std::size_t i = 0; i < faded.size(); ++i)
        {
            const bool alongZ = faded[i].from.x == faded[i].to.x;
            const glm::vec3 along =
                alongZ ? glm::vec3{0.0f, 0.0f, level.step} : glm::vec3{level.step, 0.0f, 0.0f};
            const glm::vec3 toNeighbour =
                alongZ ? glm::vec3{level.step, 0.0f, 0.0f} : glm::vec3{0.0f, 0.0f, level.step};
            const std::array<std::pair<glm::vec3, float>, 2> ends{
                {{faded[i].from, faded[i].alphaFrom}, {faded[i].to, faded[i].alphaTo}}};
            const std::array<float, 2> plainAlpha{plain[i].alphaFrom, plain[i].alphaTo};
            for (std::size_t end = 0; end < ends.size(); ++end)
            {
                const float pixels =
                    projectedSpacing(ends[end].first, along, toNeighbour, *withScreen.screen);
                if (pixels < GridMinPixels && plainAlpha[end] > 0.0f)
                {
                    ++tight;
                    CHECK(ends[end].second == 0.0f); // trop serrée : invisible
                }
                else if (pixels >= GridMinPixels + GridFadePixels)
                {
                    wide += plainAlpha[end] > 0.0f ? 1 : 0;
                    CHECK(ends[end].second == plainAlpha[end]); // assez large : intacte
                }
                CHECK(ends[end].second <= plainAlpha[end]); // l'écran n'ajoute jamais d'opacité
            }
        }
    }
    // Le test n'est pas creux : des extrémités serrées et des extrémités larges, toutes avec une
    // opacité à perdre.
    CHECK(tight > 20);
    CHECK(wide > 100);
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

TEST_CASE("G bascule la vue de jeu dans la Vue, ni en vol, ni dans un champ de texte, ni ailleurs")
{
    const ShortcutTable table = defaultShortcuts();
    const KeyPress g{.scancode = ScanG, .keycode = 'g', .modifiers = {}};
    const ActiveContexts inView{.flight = false, .view = true, .game = false, .textInput = false};
    CHECK(viewKeyActionOf(table, g, inView) == Action::GameView);
    // La touche est à la position, pas à la lettre : sur un AZERTY elle ne change pas de place.
    CHECK(viewKeyActionOf(table, {.scancode = ScanG, .keycode = 'x', .modifiers = {}}, inView) ==
          Action::GameView);
    ActiveContexts flying = inView;
    flying.flight = true;
    CHECK_FALSE(viewKeyActionOf(table, g, flying).has_value());
    ActiveContexts typing = inView;
    typing.textInput = true;
    CHECK_FALSE(viewKeyActionOf(table, g, typing).has_value());
    ActiveContexts elsewhere = inView;
    elsewhere.view = false;
    CHECK_FALSE(viewKeyActionOf(table, g, elsewhere).has_value());
    // F reste aux gestes, qui connaissent la souris.
    CHECK_FALSE(viewKeyActionOf(table, {.scancode = ScanF, .keycode = 'f', .modifiers = {}}, inView)
                    .has_value());
}

TEST_CASE("la vue de jeu et Jouer cachent les aides, la grille suit")
{
    CHECK(helpersVisibleIn(Mode::Edit, false));
    CHECK_FALSE(helpersVisibleIn(Mode::Edit, true));
    CHECK_FALSE(helpersVisibleIn(Mode::PlayWithoutReturn, false));
    CHECK_FALSE(helpersVisibleIn(Mode::PlayWithoutReturn, true));

    ShowState show;
    CHECK(gridVisibleIn(show, true));
    CHECK_FALSE(gridVisibleIn(show, false)); // aides cachées : pas de grille
    show.grid = false;
    CHECK_FALSE(gridVisibleIn(show, true)); // « Grille » décochée
}

TEST_CASE("seule l'action « Vue de jeu » bascule la vue de jeu")
{
    ShowState show;
    CHECK_FALSE(applyShowAction(show, Action::FrameSelection));
    CHECK_FALSE(show.gameView);
    CHECK(applyShowAction(show, Action::GameView));
    CHECK(show.gameView);
    CHECK(applyShowAction(show, Action::GameView));
    CHECK_FALSE(show.gameView);
    CHECK(show.grid); // la grille n'est pas touchée
}
