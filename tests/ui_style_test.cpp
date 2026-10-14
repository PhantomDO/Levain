#include <algorithm>
#include <array>

#include <doctest/doctest.h>
#include <imgui.h>

#include "levain/ui/style.hpp"

using levain::ui::applyEditorStyle;
using levain::ui::EditorAccent;
using levain::ui::EditorAccentDark;
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
    // style.
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
