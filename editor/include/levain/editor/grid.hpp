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

#include <glm/glm.hpp>

#include "levain/render/debug_lines.hpp"

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

} // namespace levain::editor
