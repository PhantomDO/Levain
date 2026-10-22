#pragma once

// Le style de l'éditeur (M7.7, morceau 17b, ADR-0036 décision 15) : sombre, plat et lisible, dans
// l'esprit de l'éditeur d'Unreal. Une fonction libre sur un `ImGuiStyle`, que l'éditeur appelle une
// fois ; le sandbox et *Rando* gardent le style par défaut d'ImGui (un HUD n'a pas l'air d'un
// outil).

#include <imgui.h>

namespace levain::ui
{

/// Un gris ou une couleur de 0 à 255, **sRGB et opaque** : c'est ce qu'ImGui attend (la passe d'UI
/// linéarise sur une cible sRGB, `linearOnSrgbTarget`), et aucun alpha ne s'y glisse (voir
/// `applyEditorStyle`).
[[nodiscard]] constexpr ImVec4 srgb8(int red, int green, int blue)
{
    return {static_cast<float>(red) / 255.0f, static_cast<float>(green) / 255.0f,
            static_cast<float>(blue) / 255.0f, 1.0f};
}

/// **L'accent**, le seul de l'éditeur : l'élément actif ou choisi (onglet, case cochée, curseur,
/// pression d'un bouton, ligne de la hiérarchie). Le bleu de l'éditeur d'Unreal 5, ni le rouge ni
/// le vert des axes X et Y, et celui d'Unity et de Godot aussi (un bleu).
inline constexpr ImVec4 EditorAccent = srgb8(0, 112, 224);

/// Le même, clair : ce qui s'allume au survol ou se lit en texte (lien, poignée tenue, curseur).
inline constexpr ImVec4 EditorAccentLight = srgb8(88, 166, 255);

/// Le même, foncé : le fond d'une ligne choisie, sous un texte clair.
inline constexpr ImVec4 EditorAccentDark = srgb8(24, 80, 148);

/// Le même, entre le foncé et le franc : le survol d'une ligne déjà choisie
/// (`pushSelectedRowColors`).
inline constexpr ImVec4 EditorAccentHover = srgb8(32, 98, 176);

/// **La teinte du jeu** (ADR-0037, décision 6) : le bord de la Vue et le fond de la barre d'outils
/// quand le jeu tourne (`PlayTint`, un orange brûlé : Unreal teinte l'éditeur, Unity l'assombrit),
/// et quand il est en pause (`PausedTint`, un bleu : une autre teinte, non une nuance de la
/// première, pour qu'on ne les confonde pas d'un coup d'œil). Ni l'un ni l'autre n'est l'accent
/// (`EditorAccent`, un bleu foncé) : l'accent désigne ce qu'on a choisi, la teinte dit que le
/// monde bouge. **Le bord est dessiné sur la scène**, qui peut être un ciel clair ou un sol sombre
/// : une couleur claire se perd sur le ciel, une sombre sur le sol. Leur luminance relative est
/// donc au milieu (0,16 à 0,19), où l'on garde 3 de contraste (WCAG 1.4.11) contre l'un comme
/// l'autre, et contre le gris des panneaux (`tests/ui_style_test.cpp`). **Une luminance moyenne
/// n'a pas 3 contre toute scène** (une herbe, un terrain de luminance voisine la perdent) : ce qui
/// garde le bord visible alors, c'est son côté extérieur, qui touche le gris des panneaux, non la
/// teinte seule. Le texte clair du calque de la Vue se lit sur le fond tinté.
inline constexpr ImVec4 PlayTint = srgb8(190, 85, 10);
inline constexpr ImVec4 PausedTint = srgb8(35, 115, 205);

/// Le fond de la barre d'outils quand le jeu est teinté : le gris des barres, tiré de 20 % vers la
/// teinte, opaque. Assez peu pour que le texte, grisé compris, garde ses 4,5 de contraste (WCAG) ;
/// assez pour qu'on voie d'un coup d'œil que la barre n'est plus celle de l'Édition.
[[nodiscard]] ImVec4 tintedBarBackground(const ImVec4& tint);

/// L'opacité d'un calque posé sur la scène (le bandeau de mode de la Vue) : presque opaque. Mesuré
/// quand la 4070 mélangeait encore en linéaire (avant le morceau 17d) : un fond sombre translucide
/// sur une scène claire s'éclaircissait bien plus qu'en gamma, à 0,78 le fond montait à 131 sur du
/// blanc ; à 0,93 il reste à 78, et le texte (`Text`) à 5,7 de contraste. Le gamma d'aujourd'hui
/// n'a pas fait changer la valeur.
inline constexpr float OverlayAlpha = 0.93f;

/// Le fond d'un tel calque : un gris de la palette (celui des barres), à `OverlayAlpha`.
[[nodiscard]] ImVec4 overlayBackground();

/// Règle le style de l'éditeur : couleurs, rondeurs, espacements, pour Inter à 15 px, à l'échelle
/// de l'écran. **Repart d'un style neuf** : `ScaleAllSizes` tronque chaque taille à l'entier
/// (`ImTrunc` : à 0,75 un bord d'un pixel tombe à 0), et appelé sur un style déjà mis à l'échelle
/// (celui de `createUiContext`) il tronquerait deux fois. **Idempotente** : le style rendu ne
/// dépend que de `displayScale` et des polices du style donné, de sorte qu'on peut la rappeler à
/// chaque changement d'échelle de l'écran (`app::setUiStyle`).
/// Les polices (`FontSizeBase`, `FontScaleMain`) ne bougent pas, `FontScaleDpi` prend
/// `displayScale`.
///
/// **Aucun alpha dans les couleurs** hors de celles qui en ont besoin par nature (le voile de la
/// modale, la cible d'un glisser-déposer) : l'interface se mélangeait en linéaire sur une cible
/// sRGB, où un alpha de 94 % se lit autour de 73 % ; elle se mélange en gamma depuis le morceau 17d
/// (README, « Fonds opaques »), et les fonds restent opaques : un choix de style.
void applyEditorStyle(ImGuiStyle& style, float displayScale);

/// Colorie, le temps d'une ligne, une ligne **choisie** (hiérarchie, liste) en accent : `Header`,
/// `HeaderHovered`, `HeaderActive` valent aussi les en-têtes de composant de l'inspecteur, qui
/// restent gris. Rend le nombre de couleurs poussées, à passer à `ImGui::PopStyleColor` : 0 pour
/// une ligne qui n'est pas choisie. Les trois, car un survol d'une ligne choisie reprend
/// `HeaderHovered` (`Selectable`, imgui_widgets.cpp) : le bleu virerait au gris sous la souris.
[[nodiscard]] int pushSelectedRowColors(bool selected);

/// La luminance relative d'une couleur sRGB (WCAG 2.x) : les canaux **linéarisés** d'abord, puis
/// pondérés par la sensibilité de l'œil. Moyenner des valeurs sRGB brutes donne un rapport faux :
/// la courbe sRGB n'est pas linéaire.
[[nodiscard]] float relativeLuminance(const ImVec4& srgb);

/// Le rapport de contraste de deux couleurs sRGB, de 1 (identiques) à 21 (noir sur blanc) ; 4,5 est
/// le seuil de lisibilité d'un texte courant (WCAG 2.x).
[[nodiscard]] float contrastRatio(const ImVec4& first, const ImVec4& second);

} // namespace levain::ui
