#include <algorithm>
#include <array>
#include <cmath>

#include <doctest/doctest.h>
#include <imgui.h>

#include "levain/ui/context.hpp"
#include "levain/ui/style.hpp"

using levain::ui::applyEditorStyle;
using levain::ui::contrastRatio;
using levain::ui::EditorAccent;
using levain::ui::EditorAccentDark;
using levain::ui::EditorAccentHover;
using levain::ui::EditorAccentLight;

namespace
{

/// Le style de l'éditeur à l'échelle `scale`, sans contexte d'ImGui (`applyEditorStyle` n'en a pas
/// besoin : un `ImGuiStyle` se construit seul).
ImGuiStyle editorStyleAt(float scale)
{
    ImGuiStyle style;
    applyEditorStyle(style, scale);
    return style;
}

/// `ImVec4` n'a pas d'`==` hors de `imgui_internal.h` : composante par composante.
bool sameColor(const ImVec4& a, const ImVec4& b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}

/// La couleur sans son alpha.
bool sameRgb(const ImVec4& a, const ImVec4& b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

bool isNeutral(const ImVec4& color)
{
    return color.x == color.y && color.y == color.z;
}

/// Les entrées du style qui portent l'accent, sous une de ses nuances. Tout le reste est gris : la
/// poignée d'un curseur et un histogramme n'y sont qu'une fois tenus ou survolés.
constexpr std::array<int, 15> AccentColors = {
    ImGuiCol_CheckboxSelectedBg, ImGuiCol_SliderGrabActive,     ImGuiCol_ButtonActive,
    ImGuiCol_SeparatorHovered,   ImGuiCol_SeparatorActive,      ImGuiCol_ResizeGripHovered,
    ImGuiCol_ResizeGripActive,   ImGuiCol_TabSelectedOverline,  ImGuiCol_DockingPreview,
    ImGuiCol_PlotLinesHovered,   ImGuiCol_PlotHistogramHovered, ImGuiCol_TextLink,
    ImGuiCol_TextSelectedBg,     ImGuiCol_DragDropTarget,       ImGuiCol_NavCursor};

/// Les couleurs qui ont de l'alpha par nature : rien ne doit se voir (`Clear`), un voile, un
/// aperçu.
bool mayBeTranslucent(int color)
{
    return color == ImGuiCol_ChildBg || color == ImGuiCol_BorderShadow ||
           color == ImGuiCol_TableRowBg || color == ImGuiCol_DragDropTargetBg ||
           color == ImGuiCol_DockingPreview || color == ImGuiCol_NavWindowingHighlight ||
           color == ImGuiCol_NavWindowingDimBg || color == ImGuiCol_ModalWindowDimBg;
}

} // namespace

TEST_CASE(
    "le style de l'éditeur : fonds opaques, et aucun alpha hors des couleurs qui en ont besoin")
{
    const ImGuiStyle style = editorStyleAt(1.0f);
    // Les trois fonds que `createUiContext` rend opaques : un menu ne laisse rien voir dessous.
    for (const ImGuiCol color : {ImGuiCol_WindowBg, ImGuiCol_PopupBg, ImGuiCol_MenuBarBg})
    {
        INFO("fond " << ImGui::GetStyleColorName(color));
        CHECK(style.Colors[color].w == 1.0f);
    }
    // L'interface se mélange en linéaire sur une cible sRGB : un alpha de 94 % se lit autour de 73
    // % (README). Les autres couleurs sont opaques aussi, que le mélange en espace gamma, s'il
    // vient, ne change pas l'aspect.
    for (int color = 0; color < ImGuiCol_COUNT; ++color)
    {
        if (!mayBeTranslucent(color))
        {
            INFO("alpha de " << ImGui::GetStyleColorName(color));
            CHECK(style.Colors[color].w == 1.0f);
        }
    }
}

TEST_CASE(
    "le style de l'éditeur : un seul accent, sur les éléments actifs ou choisis, le reste en gris")
{
    const ImGuiStyle style = editorStyleAt(1.0f);
    // Les éléments actifs ou choisis, à la nuance voulue.
    CHECK(sameColor(style.Colors[ImGuiCol_CheckboxSelectedBg], EditorAccent));
    CHECK(sameColor(style.Colors[ImGuiCol_SliderGrabActive], EditorAccent));
    CHECK(sameColor(style.Colors[ImGuiCol_ButtonActive], EditorAccent));
    CHECK(sameColor(style.Colors[ImGuiCol_TabSelectedOverline], EditorAccent));
    CHECK(sameColor(style.Colors[ImGuiCol_SeparatorActive], EditorAccent));
    CHECK(sameColor(style.Colors[ImGuiCol_TextLink], EditorAccentLight));
    CHECK(sameColor(style.Colors[ImGuiCol_TextSelectedBg], EditorAccentDark));
    // Un onglet, un bouton, un en-tête, la poignée d'un curseur ou un histogramme au repos ne sont
    // pas colorés : l'accent désigne, il ne décore pas.
    for (const ImGuiCol color : {ImGuiCol_Tab, ImGuiCol_TabSelected, ImGuiCol_Button,
                                 ImGuiCol_Header, ImGuiCol_HeaderActive, ImGuiCol_FrameBg,
                                 ImGuiCol_WindowBg, ImGuiCol_SliderGrab, ImGuiCol_PlotHistogram})
    {
        INFO("gris : " << ImGui::GetStyleColorName(color));
        CHECK(isNeutral(style.Colors[color]));
    }
    // Aucune autre couleur que celles de la liste n'a de teinte : une couleur qui sortirait du gris
    // sans y être serait un deuxième accent. Celles de la liste portent une des trois nuances du
    // style (`EditorAccentHover` ne sert qu'à `pushSelectedRowColors`).
    for (int color = 0; color < ImGuiCol_COUNT; ++color)
    {
        INFO("couleur " << ImGui::GetStyleColorName(color));
        const ImVec4& value = style.Colors[color];
        if (std::ranges::find(AccentColors, color) != AccentColors.end())
        {
            CHECK((sameRgb(value, EditorAccent) || sameRgb(value, EditorAccentLight) ||
                   sameRgb(value, EditorAccentDark)));
        }
        else
        {
            CHECK(isNeutral(value));
        }
    }
}

TEST_CASE(
    "le style de l'éditeur : les tailles suivent l'échelle de l'écran, sans les tronquer deux fois")
{
    const ImGuiStyle one = editorStyleAt(1.0f);
    const ImGuiStyle two = editorStyleAt(2.0f);
    CHECK(one.FontScaleDpi == 1.0f);
    CHECK(two.FontScaleDpi == 2.0f);
    CHECK(two.FramePadding.x == 2.0f * one.FramePadding.x);
    CHECK(two.FramePadding.y == 2.0f * one.FramePadding.y);
    CHECK(two.ItemSpacing.y == 2.0f * one.ItemSpacing.y);
    CHECK(two.ScrollbarSize == 2.0f * one.ScrollbarSize);
    CHECK(two.FrameRounding == 2.0f * one.FrameRounding);
    CHECK(two.ColorMarkerSize == 2.0f * one.ColorMarkerSize);
    // Des rondeurs de 2 à 4 px, et des repères X, Y, Z assez larges pour se voir (à 0, ImGui n'en
    // dessine aucun, sans rien dire).
    CHECK(one.FrameRounding >= 2.0f);
    CHECK(one.FrameRounding <= 4.0f);
    CHECK(one.ColorMarkerSize >= 2.0f);

    // Le style de `createUiContext` est déjà à l'échelle : repartir de lui, c'est le seul chemin du
    // contexte, et `ScaleAllSizes` tronque à chaque appel. Les polices passent telles quelles.
    ImGuiStyle already;
    already.ScaleAllSizes(1.5f);
    already.FontSizeBase = 15.0f;
    already.FontScaleMain = 1.25f;
    applyEditorStyle(already, 2.0f);
    CHECK(already.FramePadding.x == two.FramePadding.x);
    CHECK(already.ScrollbarSize == two.ScrollbarSize);
    // Des tailles que `applyEditorStyle` ne règle pas elle-même : celles d'ImGui, mises à l'échelle
    // une fois et pas deux (un style qui ne repartirait pas d'un style neuf les multiplierait par
    // 3).
    CHECK(already.ScrollbarPadding == two.ScrollbarPadding);
    CHECK(already.DockingSeparatorSize == two.DockingSeparatorSize);
    CHECK(already.WindowBorderHoverPadding == two.WindowBorderHoverPadding);
    CHECK(already.FontSizeBase == 15.0f);
    CHECK(already.FontScaleMain == 1.25f);
    CHECK(already.FontScaleDpi == 2.0f);
}

namespace
{

/// Ce que dessine `ImGui::BeginDisabled` : `alpha` fois l'avant-plan sur le fond, **mélangé en
/// espace gamma**, sur les valeurs sRGB. Le pire des deux mélanges **pour un avant-plan plus clair
/// que son fond** : en linéaire il ressort toujours plus clair (la courbe d'encodage est concave),
/// donc plus lisible. Pour un fond sombre translucide sur une scène claire, c'est l'inverse : le
/// linéaire est le pire (`blendInLinear`).
ImVec4 blendInGamma(const ImVec4& foreground, const ImVec4& background, float alpha)
{
    return {(alpha * foreground.x) + ((1.0f - alpha) * background.x),
            (alpha * foreground.y) + ((1.0f - alpha) * background.y),
            (alpha * foreground.z) + ((1.0f - alpha) * background.z), 1.0f};
}

/// Le mélange que fait une cible sRGB : en lumière linéaire, puis réencodé (IEC 61966-2-1). C'est
/// ce que font D3D12 et Vulkan sur la 4070 ; la cible de lavapipe, UNORM, mélange en gamma (#493).
float blendChannelInLinear(float foreground, float background, float alpha)
{
    const auto toLinear = [](float value)
    { return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f); };
    const float linear = (alpha * toLinear(foreground)) + ((1.0f - alpha) * toLinear(background));
    return linear <= 0.0031308f ? linear * 12.92f
                                : (1.055f * std::pow(linear, 1.0f / 2.4f)) - 0.055f;
}

ImVec4 blendInLinear(const ImVec4& foreground, const ImVec4& background, float alpha)
{
    return {blendChannelInLinear(foreground.x, background.x, alpha),
            blendChannelInLinear(foreground.y, background.y, alpha),
            blendChannelInLinear(foreground.z, background.z, alpha), 1.0f};
}

/// 4,5 : le seuil de lisibilité d'un texte courant (WCAG 2.x).
constexpr float Readable = 4.5f;

/// « Se voit grisé » : le texte plein et le texte grisé, l'un contre l'autre, au moins ce rapport
/// (Text 215 contre TextDisabled 150 : 2,06 ; à 196 contre 215 on ne voyait pas la différence).
constexpr float LooksDisabled = 1.8f;

} // namespace

TEST_CASE("le texte grisé de l'éditeur reste lisible et se voit grisé, dessiné plein")
{
    const ImGuiStyle style = editorStyleAt(1.0f);
    const ImVec4& disabled = style.Colors[ImGuiCol_TextDisabled];
    const ImVec4& text = style.Colors[ImGuiCol_Text];
    for (const ImGuiCol background :
         {ImGuiCol_WindowBg, ImGuiCol_PopupBg, ImGuiCol_MenuBarBg, ImGuiCol_FrameBg})
    {
        INFO("sur " << ImGui::GetStyleColorName(background));
        const ImVec4& behind = style.Colors[background];
        // `TextDisabled` **plein** : les entrées de menu (`greyedMenuItem`) et les boutons
        // (`disabledButton`) l'ont à un alpha de 1. Un texte plein n'est mélangé avec rien : le
        // mélange de la cible (gamma ou linéaire) n'y change rien.
        CHECK(contrastRatio(disabled, behind) >= Readable);
        CHECK(contrastRatio(text, behind) >= 7.0f);
    }
    // « Grisé » se voit : le libellé grisé contre le libellé, sur le gris le plus bas comme le plus
    // haut des menus.
    CHECK(contrastRatio(text, disabled) >= LooksDisabled);
}

TEST_CASE("les widgets d'ImGui grisés par DisabledAlpha restent lisibles et se voient grisés, "
          "mélangés en gamma comme en linéaire")
{
    const ImGuiStyle style = editorStyleAt(1.0f);
    const ImVec4& text = style.Colors[ImGuiCol_Text];
    const float alpha = style.DisabledAlpha;
    CHECK(alpha > 0.0f);
    CHECK(alpha < 1.0f);
    for (const ImGuiCol background :
         {ImGuiCol_WindowBg, ImGuiCol_PopupBg, ImGuiCol_MenuBarBg, ImGuiCol_FrameBg})
    {
        INFO("sur " << ImGui::GetStyleColorName(background));
        const ImVec4& behind = style.Colors[background];
        const ImVec4 inGamma = blendInGamma(text, behind, alpha);
        const ImVec4 inLinear = blendInLinear(text, behind, alpha);
        // Lisible : le gamma est le pire (le plus sombre)...
        CHECK(contrastRatio(inGamma, behind) >= Readable);
        CHECK(contrastRatio(inLinear, behind) >= Readable);
        // ... se voit grisé : le linéaire est le pire (le plus clair, donc le plus proche du
        // texte). 1,56 à 0,6 : un rapport de 1,8 y est hors d'atteinte sans tomber sous 4,5 en
        // gamma ; les entrées de menu, elles, ne passent pas par là.
        CHECK(contrastRatio(text, inGamma) >= LooksDisabled);
        CHECK(contrastRatio(text, inLinear) >= 1.5f);
    }
}

TEST_CASE("blendInGamma, pour un avant-plan clair : jamais plus clair que le mélange en linéaire")
{
    // Aux extrémités, un alpha de 1 donne l'avant-plan et un alpha de 0 le fond.
    const ImVec4 white = levain::ui::srgb8(255, 255, 255);
    const ImVec4 black = levain::ui::srgb8(0, 0, 0);
    CHECK(blendInGamma(white, black, 1.0f).x == 1.0f);
    CHECK(blendInGamma(white, black, 0.0f).x == 0.0f);
    CHECK(blendInGamma(white, black, 0.5f).x == doctest::Approx(0.5f));
    // 140 à 60 % sur 34 : 97,6 en gamma (c'est ce que mesure une capture d'un raccourci grisé :
    // 98), 113 en linéaire.
    const ImVec4 composed =
        blendInGamma(levain::ui::srgb8(140, 140, 140), levain::ui::srgb8(34, 34, 34), 0.6f);
    CHECK(composed.x * 255.0f == doctest::Approx(97.6f).epsilon(0.001));
    CHECK(blendChannelInLinear(140.0f / 255.0f, 34.0f / 255.0f, 0.6f) * 255.0f ==
          doctest::Approx(112.6f).epsilon(0.005));
    // Ce qui rend le mélange en gamma sûr pour un test de contraste : sur un avant-plan plus clair
    // que son fond, le mélange en linéaire n'est jamais plus sombre.
    for (const float foreground : {0.4f, 0.7f, 1.0f})
    {
        for (const float background : {0.0f, 0.1f, 0.3f})
        {
            for (const float alpha : {0.3f, 0.6f, 0.8f})
            {
                INFO("avant-plan " << foreground << ", fond " << background << ", alpha " << alpha);
                const float gamma = (alpha * foreground) + ((1.0f - alpha) * background);
                CHECK(gamma <= blendChannelInLinear(foreground, background, alpha) + 1e-6f);
            }
        }
    }
}

namespace
{

/// Le plus petit écart de gris qu'on voit entre deux plans voisins de l'interface : 1,12 de
/// contraste (15 niveaux de gris à cet endroit de l'échelle). En deçà, un plan se fond dans son
/// voisin.
constexpr float VisibleStep = 1.12f;

/// Le seuil des éléments d'interface qui ne sont pas du texte, filet d'onglet compris (WCAG 2.x,
/// 1.4.11).
constexpr float VisibleLine = 3.0f;

} // namespace

TEST_CASE("l'échelle des gris : chaque plan se voit à côté de son voisin")
{
    const ImGuiStyle style = editorStyleAt(1.0f);
    const auto contrast = [&style](ImGuiCol first, ImGuiCol second)
    { return contrastRatio(style.Colors[first], style.Colors[second]); };
    // Un champ creusé dans le panneau, le panneau sous sa barre, un menu sur le panneau.
    CHECK(contrast(ImGuiCol_FrameBg, ImGuiCol_WindowBg) >= VisibleStep);
    CHECK(contrast(ImGuiCol_MenuBarBg, ImGuiCol_WindowBg) >= VisibleStep);
    CHECK(contrast(ImGuiCol_PopupBg, ImGuiCol_WindowBg) >= VisibleStep);
    // Un en-tête de composant sur le fond de l'inspecteur.
    CHECK(contrast(ImGuiCol_Header, ImGuiCol_WindowBg) >= VisibleStep);
    // La barre de titre d'une fenêtre flottante s'éclaircit quand elle a le focus : c'est ce qui
    // la sépare du panneau qu'elle couvre (le même gris de fond, une bordure d'un pixel).
    CHECK(contrast(ImGuiCol_TitleBgActive, ImGuiCol_TitleBg) >= 1.2f);
    CHECK(contrast(ImGuiCol_Border, ImGuiCol_WindowBg) >= 1.5f);
}

TEST_CASE("l'onglet visible d'un nœud sans le focus se distingue de ses voisins")
{
    const ImGuiStyle style = editorStyleAt(1.0f);
    const auto contrast = [&style](ImGuiCol first, ImGuiCol second)
    { return contrastRatio(style.Colors[first], style.Colors[second]); };
    // Le fond de l'onglet visible contre celui des autres : sans le focus aussi (l'aperçu du
    // morceau 17b ne distinguait « Image » de « Passes » que par la croix).
    CHECK(contrast(ImGuiCol_TabSelected, ImGuiCol_Tab) >= VisibleStep);
    CHECK(contrast(ImGuiCol_TabDimmedSelected, ImGuiCol_TabDimmed) >= VisibleStep);
    // Et son filet de deux pixels, contre la barre d'onglets : l'accent avec le focus, un gris
    // clair sans.
    CHECK(contrast(ImGuiCol_TabSelectedOverline, ImGuiCol_Tab) >= VisibleLine);
    CHECK(contrast(ImGuiCol_TabDimmedSelectedOverline, ImGuiCol_TabDimmed) >= VisibleLine);
    // Le filet sans le focus n'est pas celui du focus : on voit quelle barre le tient.
    CHECK_FALSE(sameColor(style.Colors[ImGuiCol_TabDimmedSelectedOverline],
                          style.Colors[ImGuiCol_TabSelectedOverline]));
}

TEST_CASE("contrastRatio linéarise l'sRGB : le noir sur le blanc fait 21, un gris moyen 0,214")
{
    const ImVec4 black = levain::ui::srgb8(0, 0, 0);
    const ImVec4 white = levain::ui::srgb8(255, 255, 255);
    CHECK(contrastRatio(black, white) == doctest::Approx(21.0f).epsilon(0.001));
    CHECK(contrastRatio(white, black) == doctest::Approx(21.0f).epsilon(0.001));
    CHECK(contrastRatio(white, white) == doctest::Approx(1.0f));
    // Le gris sRGB de 50 % n'a que 21,4 % de la lumière du blanc ; sans la linéarisation, 50 %.
    CHECK(levain::ui::relativeLuminance({0.5f, 0.5f, 0.5f, 1.0f}) ==
          doctest::Approx(0.2140f).epsilon(0.001));
}

TEST_CASE("une ligne choisie prend l'accent le temps d'une ligne, les trois couleurs de survol et "
          "de clic")
{
    const levain::ui::UiContext context = levain::ui::createUiContext(1.0f);
    ImGuiStyle& style = ImGui::GetStyle();
    applyEditorStyle(style, 1.0f);
    const ImVec4 greyHeader = style.Colors[ImGuiCol_Header];
    const ImVec4 greyHovered = style.Colors[ImGuiCol_HeaderHovered];
    CHECK(isNeutral(greyHovered));

    // Pas choisie : rien n'est poussé, l'en-tête reste gris.
    CHECK(levain::ui::pushSelectedRowColors(false) == 0);
    CHECK(sameColor(ImGui::GetStyleColorVec4(ImGuiCol_Header), greyHeader));

    const int pushed = levain::ui::pushSelectedRowColors(true);
    CHECK(pushed == 3);
    CHECK(sameColor(ImGui::GetStyleColorVec4(ImGuiCol_Header), EditorAccentDark));
    // Le survol d'une ligne choisie reprend `HeaderHovered` : bleu aussi, sinon la ligne virerait
    // au gris.
    CHECK(sameColor(ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered), EditorAccentHover));
    CHECK(sameColor(ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive), EditorAccent));
    ImGui::PopStyleColor(pushed);
    CHECK(sameColor(ImGui::GetStyleColorVec4(ImGuiCol_Header), greyHeader));
    CHECK(sameColor(ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered), greyHovered));
}

TEST_CASE("le fond d'un calque sur la scène est un gris de la palette, presque opaque")
{
    const ImVec4 overlay = levain::ui::overlayBackground();
    CHECK(isNeutral(overlay)); // pas de teinte : le bandeau de mode n'est pas un deuxième accent
    CHECK(overlay.w == levain::ui::OverlayAlpha);
    CHECK(overlay.w < 1.0f);
    CHECK(overlay.w >= 0.93f);
    // Le gris de la barre des menus et des onglets : celui de la palette, pas un bleu-gris de plus.
    const ImGuiStyle style = editorStyleAt(1.0f);
    CHECK(sameRgb(overlay, style.Colors[ImGuiCol_MenuBarBg]));
}

TEST_CASE("le rappel de touche d'un calque reste lisible sur une scène blanche, mélangé en "
          "linéaire comme en gamma")
{
    const ImGuiStyle style = editorStyleAt(1.0f);
    const ImVec4 overlay = levain::ui::overlayBackground();
    const ImVec4 white = levain::ui::srgb8(255, 255, 255);
    const ImVec4 hint = style.Colors[ImGuiCol_Text]; // `drawModeBarContents` : le rappel en Text
    // Un fond sombre translucide s'éclaircit plus en linéaire (131 à 0,78 sur du blanc, 78 à 0,93)
    // qu'en gamma : c'est ce mélange le pire, contrairement au texte clair sur fond sombre.
    const ImVec4 inLinear =
        blendInLinear({overlay.x, overlay.y, overlay.z, 1.0f}, white, overlay.w);
    const ImVec4 inGamma = blendInGamma({overlay.x, overlay.y, overlay.z, 1.0f}, white, overlay.w);
    CHECK(contrastRatio(hint, inLinear) >= Readable);
    CHECK(contrastRatio(hint, inGamma) >= Readable);
    // Le contrôle du contrôle : le gris éteint de l'ancien rappel (178) n'y suffisait pas.
    CHECK(contrastRatio(levain::ui::srgb8(178, 178, 178), inLinear) < Readable);
}
