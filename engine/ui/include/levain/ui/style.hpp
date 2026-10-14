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

/// Règle le style de l'éditeur : ses couleurs, des gris neutres et un accent, à l'échelle
/// de l'écran. **Repart d'un style neuf** : `ScaleAllSizes` tronque chaque taille à l'entier
/// (`ImTrunc` : à 0,75 un bord d'un pixel tombe à 0), et appelé sur un style déjà mis à l'échelle
/// (celui de `createUiContext`) il tronquerait deux fois. **Idempotente** : le style rendu ne
/// dépend que de `displayScale` et des polices du style donné, de sorte qu'on peut la rappeler à
/// chaque changement d'échelle de l'écran.
/// Les polices (`FontSizeBase`, `FontScaleMain`) ne bougent pas, `FontScaleDpi` prend
/// `displayScale`.
///
/// **Aucun alpha dans les couleurs** hors de celles qui en ont besoin par nature (le voile de la
/// modale, la cible d'un glisser-déposer) : l'interface se mélange en linéaire sur une cible sRGB,
/// où un alpha de 94 % se lit autour de 73 % (README, « Fonds opaques »). `WindowBg`, `PopupBg` et
/// `MenuBarBg` sont donc opaques comme ceux de `createUiContext`, et le reste aussi : le mélange en
/// espace gamma, s'il vient, ne changera pas l'aspect.
void applyEditorStyle(ImGuiStyle& style, float displayScale);

/// La luminance relative d'une couleur sRGB (WCAG 2.x) : les canaux **linéarisés** d'abord, puis
/// pondérés par la sensibilité de l'œil. Moyenner des valeurs sRGB brutes donne un rapport faux :
/// la courbe sRGB n'est pas linéaire.
[[nodiscard]] float relativeLuminance(const ImVec4& srgb);

/// Le rapport de contraste de deux couleurs sRGB, de 1 (identiques) à 21 (noir sur blanc) ; 4,5 est
/// le seuil de lisibilité d'un texte courant (WCAG 2.x).
[[nodiscard]] float contrastRatio(const ImVec4& first, const ImVec4& second);

} // namespace levain::ui
