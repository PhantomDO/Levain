#pragma once

// La grille au sol de la Vue (ADR-0036, décision 15) : des lignes de debug (M6.2, ADR-0027) que
// l'éditeur dessine dans l'étape Transparent (après le ciel, mêlées par leur opacité) par **sa
// propre** `DebugLinesPass`, un appel par command list (render/debug_lines.hpp : le buffer de
// sommets appartient à la passe, le contour de la sélection du programme a la sienne). La logique
// est en fonctions libres dont toutes les dépendances sont des paramètres (ADR-0011) :
// `gridLinesFor` ne touche ni le GPU ni ImGui. Équivalents : la grille de la Scene View d'Unity
// (Gizmos, « Show Grid »), la grille du viewport d'Unreal (Show > Grid), View > Grid de Godot.

#include <array>
#include <cstddef>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include "levain/render/debug_lines.hpp"
#include "levain/render/stages.hpp"

namespace levain::editor
{

/// Le nombre de cases mineures de chaque côté du point central : 50, soit 101 lignes par sens.
inline constexpr int GridHalfCells = 50;
/// Une ligne majeure tous les 10 pas mineurs.
inline constexpr int GridMajorEvery = 10;
/// Les tronçons de chaque ligne : l'opacité varie d'un tronçon à l'autre, la ligne s'estompe avec
/// la distance dans sa longueur aussi.
inline constexpr int GridSegments = 10;
/// Les familles de lignes que la grille montre à la fois : la plus fine, celle de la hauteur, la
/// plus grosse (`gridLevelsFor`).
inline constexpr std::size_t GridLevelCount = 3;
/// Le plafond du nombre de lignes (tronçons) : très en dessous de `render::MaxDebugLines`.
inline constexpr std::size_t MaxGridLines = GridLevelCount * 2 *
                                            (2 * static_cast<std::size_t>(GridHalfCells) + 1) *
                                            static_cast<std::size_t>(GridSegments);
static_assert(MaxGridLines < render::MaxDebugLines, "la grille tient dans le buffer de la passe");

/// L'écart des lignes, en mètres.
struct GridSpacing
{
    float minor = 1.0f;
    float major = 10.0f;
};

/// **L'écart suit la hauteur de la caméra**, par puissances de 10 comme Unreal et Unity : à 6 m les
/// lignes sont à 1 m, à 60 m à 10 m. Sans cela la grille serait un pelage trop fin de loin et trois
/// lignes de près. La hauteur est bornée (`std::clamp` à [0,1 ; 100 000]) : au ras du sol l'écart
/// ne tombe pas à zéro, et loin il reste un nombre fini. C'est l'écart **nominal** :
/// `gridLevelsFor` dit comment la grille passe de l'un à l'autre sans saut.
[[nodiscard]] GridSpacing gridSpacingFor(float cameraHeight);

/// Une famille de lignes, à un écart `step` : ses lignes sont aux multiples de `step`.
struct GridLevel
{
    float step = 1.0f;
    /// 0 : les lignes sont mineures, 1 : majeures (plus opaques). L'opacité est mêlée par ce
    /// nombre, non commutée.
    float role = 0.0f;
    /// 0 à 1 : la part de l'opacité avec laquelle la famille se montre, 0 étant invisible.
    float visibility = 1.0f;
};

/// **Les trois familles que la grille montre à la hauteur `cameraHeight`, mêlées sans saut.** Avec
/// `u = log10(hauteur)`, `L = ⌊u⌋` et `t = u − L` : la famille à 10^(L−1) (la plus fine) est
/// mineure et **s'efface** quand la caméra monte (visibilité `1 − t`), celle à 10^L est pleine
/// et passe de majeure à mineure (rôle `1 − t`), celle à 10^(L+1) est majeure et **apparaît**
/// (visibilité `t`). À une puissance de 10 de la hauteur, chaque famille prend la place de celle
/// que l'ordre suivant lui donne exactement : la plus fine arrive à zéro et disparaît, celle de
/// 10^L, qui était majeure, devient celle de la hauteur, la plus grosse apparaît à zéro. Avant, un
/// seul écart par ordre : à 9,9 m des lignes tous les mètres, à 10,1 m tous les 10 m, d'un coup. Le
/// fondu suit la partie fractionnaire du logarithme de la hauteur ; c'est ce que je suppose de la
/// grille de la Scene View d'Unity (supposé, non relu pour ce morceau).
[[nodiscard]] std::array<GridLevel, GridLevelCount> gridLevelsFor(float cameraHeight);

/// Une famille moins visible que cela (une demi-valeur de 8 bits sur l'opacité d'une ligne) n'est
/// pas envoyée : elle ne changerait aucun pixel.
inline constexpr float MinGridVisibility = 0.005f;

/// Ce que la grille sait de l'image : la projection de la caméra et la taille en pixels de l'image
/// de la scène, de quoi mesurer l'écart que deux lignes voisines ont à l'écran.
struct GridScreen
{
    glm::mat4 viewProjection{1.0f};
    glm::vec2 pixels{0.0f}; ///< largeur et hauteur de l'image de la scène
};

/// Ce que la grille lit de la vue : où est la caméra, l'exposition du tonemapper, jusqu'où elle
/// voit (le plan lointain) et, si on le lui donne, l'image. Rien d'autre : la logique ne touche ni
/// le GPU ni ImGui.
struct GridView
{
    glm::vec3 cameraPosition{0.0f};
    /// Celle que le tonemapper applique (`App::settings.tonemap`).
    float exposure = 1.0f;
    /// La distance de l'œil au plan lointain (`farDistanceOf`), en mètres. Rien de la grille ne
    /// doit en dépasser les neuf dixièmes : voir `gridReachFor`.
    float farPlane = 1000.0f;
    /// L'image : avec elle, les lignes trop serrées pour l'écran s'effacent (`spacingFade`) ; sans
    /// elle (un test de la géométrie), elles restent telles quelles.
    std::optional<GridScreen> screen;
};

/// **Sous `GridMinPixels` de l'écart entre deux lignes voisines, la grille ne se voit plus** ;
/// l'opacité remonte jusqu'à `GridMinPixels + GridFadePixels`. Une ligne de 1 pixel, sans
/// anti-crénelage, que sa voisine suit à 2 ou 3 pixels (le sol vu de biais, vers l'horizon) forme
/// des marches d'escalier qui crépitent dès que la caméra bouge : on les efface avant qu'elles ne
/// se touchent.
inline constexpr float GridMinPixels = 2.0f;
inline constexpr float GridFadePixels = 4.0f;

/// L'opacité que l'écart de `pixels` entre deux lignes voisines laisse à la grille : 0 jusqu'à
/// `GridMinPixels`, 1 à partir de `GridMinPixels + GridFadePixels`, linéaire entre les deux.
[[nodiscard]] float spacingFade(float pixels);

/// **L'écart, en pixels de l'image, entre la ligne qui passe par `point` et sa voisine**, à
/// `toNeighbour` mètres de là, la ligne allant dans la direction `alongLine` : la distance
/// *perpendiculaire* des deux lignes à l'écran, non celle de deux points. Sur une ligne qui file
/// de biais (le long de X, vue du dessus d'un sol), les points correspondants sont aussi décalés
/// le long de la ligne, ce qui gonflerait l'écart et laisserait des lignes collées à 1 pixel
/// (c'est ce qu'une première version, mesurant deux points, laissait passer). `alongLine` ne doit
/// pas traverser le plan de l'œil : sa longueur est celle d'un pas. **Un point derrière l'œil, ou
/// sa voisine, n'a pas d'écart à l'écran** : l'infini, la ligne n'a rien à y perdre
/// (`clipToFrustum` en coupera le bout derrière l'œil). Une ligne vue par le bout (qui se projette
/// en un point : l'axe de la caméra) a pour écart celui des deux points.
[[nodiscard]] float projectedSpacing(const glm::vec3& point, const glm::vec3& alongLine,
                                     const glm::vec3& toNeighbour, const GridScreen& screen);

/// La part du plan lointain que la grille garde : au-delà, une ligne serait coupée net par le plan
/// lointain, et l'horizon aurait une clôture au lieu de se fondre dans le ciel.
inline constexpr float GridFarFraction = 0.9f;

/// **La portée de la grille**, en mètres autour du point du sol sous la caméra, pour des lignes à
/// `step` : 50 pas, **au plus ce que le plan lointain laisse voir**. Un point du sol à `d` mètres
/// du centre est à `√(d² + h²)` de l'œil (`h` la hauteur de la caméra), et le plan lointain coupe
/// tout ce qui en est à plus de `farPlane` : la portée est donc celle où `√(d² + h²)` atteint
/// `GridFarFraction · farPlane`, moins la marge du calage du centre sur le pas (au plus 0,71 pas
/// d'écart avec la caméra). Sans cette limite, une caméra à 100 m (pas de 100 m, portée de 5 km)
/// voyait ses lignes coupées net, opaques, à 1 km. Nulle quand le sol entier est trop loin.
[[nodiscard]] float gridReachFor(float step, float cameraHeight, float farPlane);

/// La distance de `cameraPosition` au plan lointain de `frustum` (le dernier de ses six plans,
/// `render::frustumOf`), mesurée le long de l'axe de la vue. Les plans sont normalisés et tournés
/// vers l'intérieur : la distance signée de l'œil au plan est celle-là.
[[nodiscard]] float farDistanceOf(const render::Frustum& frustum, const glm::vec3& cameraPosition);

/// Les lignes d'**une** famille : un disque de `gridReachFor` autour du point du sol sous la
/// caméra, calé sur le pas de la famille, avec les axes si l'origine y tombe ; vide si la famille
/// est invisible (`MinGridVisibility`) ou que le plan lointain ne laisse pas même une case. Séparée
/// de `gridLinesFor` pour qu'un test regarde une famille à la fois.
[[nodiscard]] std::vector<render::DebugLine> gridLevelLines(const GridView& view,
                                                            const GridLevel& level);

/// Les lignes de la grille pour la vue `view`. Le monde de Levain est **Y en haut, main droite**
/// (`lookAtRH`, la caméra regarde vers −Z) : la grille est le plan du sol, y = 0, relevé d'un rien
/// (`GridLift` dans grid.cpp) pour ne pas se battre avec un sol de la scène. Chaque famille de
/// `gridLevelsFor` est un **disque** (`gridReachFor`) centré sur le point du sol sous la caméra,
/// **calé sur son pas** (les lignes restent aux multiples du pas : elles ne glissent pas quand la
/// caméra bouge) ; chaque ligne s'arrête à la corde du disque, où son opacité est nulle. Une ligne
/// que deux familles ont en commun (un multiple de 10 pas) est tracée par les deux : leurs
/// opacités se composent, elle en ressort plus marquée, et c'est ce qui la fait majeure. L'axe X
/// est rouge et l'axe Z bleu, comme les gizmos d'Unity et d'Unreal, quand ils tombent dans la
/// grille ; les lignes majeures sont plus opaques que les mineures, et toutes
/// s'estompent avec la distance au centre (par l'opacité, jamais vers le noir). **Les couleurs sont
/// divisées par l'exposition** : à `--exposure 2` la grille garde la même luminosité à l'écran.
/// Elles sont un gris moyen, entre le sol clair et le sol sombre : plus clair, il se perd dans une
/// case claire.
[[nodiscard]] std::vector<render::DebugLine> gridLinesFor(const GridView& view);

/// Ce que la grille garde d'une image à l'autre : sa passe, et de quoi qu'un test voie qu'elle a
/// dessiné (`framesDrawn` ne bouge que les images où des lignes sont parties).
struct GridState
{
    std::optional<render::DebugLinesPass> pass;
    int framesDrawn = 0;
    /// Les lignes **envoyées** à la dernière image où l'étape a tourné (après la coupe au volume
    /// de vue, `render::drawDebugLines`) ; 0 si cachée, ou si tout est tombé dehors.
    int lastLines = 0;
};

/// L'étape Transparent de la grille : dessine si `visible` et que la passe existe, sinon note zéro
/// ligne. `exposure` est celle que le tonemapper applique (`App::settings.tonemap`) ; le plan
/// lointain est celui de `context.frustum`. Une ligne dans `withEditor`.
void drawGrid(GridState& grid, const render::StageContext& context, bool visible, float exposure);

} // namespace levain::editor
