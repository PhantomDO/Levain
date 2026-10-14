#include "levain/editor/grid.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace levain::editor
{

namespace
{

// **Les couleurs sont ce que la ligne doit valoir devant l'objectif**, avant l'exposition : la
// grille est dessinée dans l'image HDR, que le tonemapper multiplie par l'exposition
// (`--exposure`), et une couleur absolue s'éteindrait ou brûlerait avec elle. On divise donc par
// l'exposition (`exposureRelative`).
//
// **Un gris moyen, ni plus clair ni plus sombre que le sol** : plus clair qu'un sol éclairé, la
// ligne se perd dans une case claire (le tonemapper AgX écrase les hautes lumières : 215 contre
// 214 sur une case de 214), et fait des tirets dans les cases sombres ; ce gris-là assombrit
// la case claire et éclaircit la sombre, et se lit sur les deux. Le sol clair du sandbox vaut
// environ 0,68 en lumière linéaire, le sombre 0,125 (214 et 99 à l'écran, captures de la
// relecture du morceau 17c), le gris 0,33. Les axes sont saturés à luminance moyenne : plus
// clairs, l'AgX les délave en rose et en bleu pâle.
constexpr float LineGrey = 0.33f;
constexpr glm::vec3 AxisXTarget{1.0f, 0.0f, 0.0f};
constexpr glm::vec3 AxisZTarget{0.0f, 0.02f, 0.9f};
/// L'opacité d'une ligne au centre de la grille : les mineures laissent mieux voir le sol que les
/// majeures, l'axe ne le laisse pas voir.
constexpr float MinorAlpha = 0.7f;
constexpr float MajorAlpha = 0.95f;
constexpr float AxisAlpha = 1.0f;
/// **Les lignes sont posées à `GridLift` pas au-dessus du plan y = 0** : une ligne dans le plan
/// même d'un sol de la scène perdrait le test de profondeur à égalité, et la grille scintillerait
/// ou disparaîtrait. Le sol du bac à sable est à y = −1, donc la grille flotte un mètre au-dessus
/// de lui, et c'est le comportement voulu pour l'instant (elle est le plan y = 0 du monde, pas
/// celui d'un sol) ; le décalage sert au sol qui est en y = 0 (le monde vide, un terrain). Cinq
/// millièmes de pas ne se voient pas, et dépassent la résolution du tampon de profondeur (D32,
/// plans proche et lointain en 1/10 000) jusqu'à environ 800 m.
constexpr float GridLift = 0.005f;
/// Une exposition nulle ou négative ferait diviser par zéro : elle est ramenée à ce plancher.
constexpr float MinExposure = 1.0e-3f;
/// **Le centre est calé sur le pas** : il est à au plus un demi-pas de la caméra sur chaque axe,
/// soit `√2 / 2` pas, 0,71 en arrondissant par excès. La portée en tient compte (`gridReachFor`).
constexpr float GridCenterSnap = 0.71f;
/// L'indice du plan lointain parmi les six de `render::frustumOf` : gauche, droite, bas, haut,
/// proche, **lointain**.
constexpr std::size_t FarPlaneIndex = 5;

/// La couleur qui donne `target` une fois multipliée par l'exposition du tonemapper.
glm::vec3 exposureRelative(const glm::vec3& target, float exposure)
{
    return target / std::max(exposure, MinExposure);
}

/// L'opacité à `distanceFromCenter` mètres du point sous la caméra : pleine au centre, nulle à la
/// portée de la grille, de sorte qu'elle s'efface dans le sol au lieu de finir net.
float fadeAt(float distanceFromCenter, float reach)
{
    return std::clamp(1.0f - distanceFromCenter / reach, 0.0f, 1.0f);
}

/// Le style d'une ligne : sa couleur (déjà divisée par l'exposition) et son opacité au centre.
struct LineStyle
{
    glm::vec3 color;
    float alpha;
};

/// La ligne d'indice absolu `index` (en pas de sa famille depuis l'origine) : l'axe si c'est
/// l'indice 0 (tracé par chaque famille visible, au prorata de sa visibilité), sinon le gris des
/// lignes et l'opacité de la famille, mêlée entre mineure et majeure par son rôle.
LineStyle styleOf(std::int64_t index, const glm::vec3& axisTarget, const GridLevel& level,
                  float exposure)
{
    if (index == 0)
    {
        return {exposureRelative(axisTarget, exposure), AxisAlpha * level.visibility};
    }
    const float alpha = glm::mix(MinorAlpha, MajorAlpha, level.role);
    return {exposureRelative(glm::vec3{LineGrey}, exposure), alpha * level.visibility};
}

/// La demi-longueur de la corde du disque de rayon `reach` à `offset` mètres de son centre : la
/// ligne s'arrête là, où son opacité tombe à zéro. Une ligne qui irait jusqu'à `reach` de part et
/// d'autre du centre sortirait du disque par les coins (jusqu'à `reach · √2`), au-delà de ce que
/// `gridReachFor` a permis.
float chordHalfLength(float offset, float reach)
{
    return std::sqrt(std::max(reach * reach - offset * offset, 0.0f));
}

/// Ce qui règle l'opacité le long d'une ligne : la distance au centre de la grille.
struct Fade
{
    glm::vec2 center;
    float reach;
};

/// Une ligne de `from` à `to`, coupée en `GridSegments` tronçons dont l'opacité suit la distance
/// au centre de `fade` : le rasterizer interpole l'opacité à l'intérieur d'un tronçon, et la ligne
/// entière s'estompe, y compris dans sa longueur.
void appendFadedLine(std::vector<render::DebugLine>& lines, const glm::vec3& from,
                     const glm::vec3& to, const LineStyle& style, const Fade& fade)
{
    const auto alphaAt = [&](const glm::vec3& point)
    {
        const float toCenter = glm::distance(glm::vec2{point.x, point.z}, fade.center);
        return style.alpha * fadeAt(toCenter, fade.reach);
    };
    // **`from + (to - from) * t`, pas `glm::mix`** : mix(a, a, t) = a(1-t) + a t peut différer de
    // `a` d'un ulp, et une ligne le long de X ne serait plus exactement à z constant (ni sur la
    // grille).
    const auto along = [&](int index)
    { return from + (to - from) * (static_cast<float>(index) / static_cast<float>(GridSegments)); };
    for (int segment = 0; segment < GridSegments; ++segment)
    {
        const glm::vec3 start = along(segment);
        const glm::vec3 end = along(segment + 1);
        lines.push_back({.from = start,
                         .to = end,
                         .color = style.color,
                         .alphaFrom = alphaAt(start),
                         .alphaTo = alphaAt(end)});
    }
}

} // namespace

float gridReachFor(float step, float cameraHeight, float farPlane)
{
    // Un plan lointain négatif mis au carré redeviendrait positif : on l'écarte avant. `!(x > 0)`
    // et non `x <= 0` : un plan lointain NaN donne une portée nulle lui aussi.
    if (!(farPlane > 0.0f))
    {
        return 0.0f;
    }
    const float visible = GridFarFraction * farPlane;
    const float onGround =
        std::sqrt(std::max(visible * visible - cameraHeight * cameraHeight, 0.0f));
    const float reach = onGround - GridCenterSnap * step;
    return !(reach > 0.0f) ? 0.0f : std::min(reach, static_cast<float>(GridHalfCells) * step);
}

float farDistanceOf(const render::Frustum& frustum, const glm::vec3& cameraPosition)
{
    const glm::vec4& plane = frustum.planes.at(FarPlaneIndex);
    return glm::dot(glm::vec3{plane}, cameraPosition) + plane.w;
}

std::array<GridLevel, GridLevelCount> gridLevelsFor(float cameraHeight)
{
    const float height = std::clamp(std::abs(cameraHeight), 0.1f, 100000.0f);
    const float exponent = std::log10(height);
    const float decade = std::floor(exponent);
    // La partie fractionnaire du logarithme : 0 à une puissance de 10, 1 juste avant la suivante.
    const float blend = exponent - decade;
    const float step = std::pow(10.0f, decade);
    const float tenth = static_cast<float>(GridMajorEvery);
    return {{{.step = step / tenth, .role = 0.0f, .visibility = 1.0f - blend},
             {.step = step, .role = 1.0f - blend, .visibility = 1.0f},
             {.step = step * tenth, .role = 1.0f, .visibility = blend}}};
}

GridSpacing gridSpacingFor(float cameraHeight)
{
    const std::array<GridLevel, GridLevelCount> levels = gridLevelsFor(cameraHeight);
    return {.minor = levels[1].step, .major = levels[2].step};
}

std::vector<render::DebugLine> gridLevelLines(const GridView& view, const GridLevel& level)
{
    std::vector<render::DebugLine> lines;
    const glm::vec3& camera = view.cameraPosition;
    const float step = level.step;
    const float reach = gridReachFor(step, camera.y, view.farPlane);
    if (level.visibility < MinGridVisibility || reach < step)
    {
        return lines; // invisible, ou pas même une case autour du centre : le sol est trop loin
    }
    // Le centre, calé sur le pas : les lignes sont toujours aux mêmes endroits du monde.
    const std::int64_t centerX = std::llround(camera.x / step);
    const std::int64_t centerZ = std::llround(camera.z / step);
    const float y = GridLift * step;
    const float x0 = static_cast<float>(centerX) * step;
    const float z0 = static_cast<float>(centerZ) * step;
    const glm::vec2 center{x0, z0};
    const int half = static_cast<int>(reach / step);
    lines.reserve(MaxGridLines / GridLevelCount);
    for (int i = -half; i <= half; ++i)
    {
        // Les cordes du disque : la ligne à `i` pas du centre s'arrête là où il finit.
        const float chord = chordHalfLength(static_cast<float>(i) * step, reach);
        if (chord <= 0.0f)
        {
            continue; // la ligne qui ne fait que toucher le disque : rien à tracer
        }
        // Une ligne le long de Z, à x constant : à x = 0 c'est l'axe Z (bleu).
        const std::int64_t ix = centerX + i;
        const float x = static_cast<float>(ix) * step;
        appendFadedLine(lines, {x, y, z0 - chord}, {x, y, z0 + chord},
                        styleOf(ix, AxisZTarget, level, view.exposure), {center, reach});
        // Une ligne le long de X, à z constant : à z = 0 c'est l'axe X (rouge).
        const std::int64_t iz = centerZ + i;
        const float z = static_cast<float>(iz) * step;
        appendFadedLine(lines, {x0 - chord, y, z}, {x0 + chord, y, z},
                        styleOf(iz, AxisXTarget, level, view.exposure), {center, reach});
    }
    return lines;
}

std::vector<render::DebugLine> gridLinesFor(const GridView& view)
{
    std::vector<render::DebugLine> lines;
    lines.reserve(MaxGridLines);
    for (const GridLevel& level : gridLevelsFor(view.cameraPosition.y))
    {
        const std::vector<render::DebugLine> family = gridLevelLines(view, level);
        lines.insert(lines.end(), family.begin(), family.end());
    }
    return lines;
}

} // namespace levain::editor
