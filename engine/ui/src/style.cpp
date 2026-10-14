#include "levain/ui/style.hpp"

#include <algorithm>
#include <cmath>

#include "levain/core/assert.hpp"

namespace levain::ui
{

namespace
{

/// Un gris sRGB opaque. Les niveaux, du plus creusé au plus clair : un champ de saisie est plus
/// sombre que le panneau qui le porte, un menu qui flotte est plus clair que ce qu'il couvre.
constexpr ImVec4 grey(int level)
{
    return srgb8(level, level, level);
}

// L'échelle de gris, proche de celle de l'éditeur d'Unreal 5 (**supposé** : #151515, #242424,
// #2F2F2F et #383838, non relus dans ses sources). Chaque marche se voit à côté de la précédente :
// un onglet dont la barre n'a pas le focus ne se distinguait plus de ses voisins à 21 contre 28.
constexpr ImVec4 Recessed = grey(15); // champs de saisie, fonds de courbes
constexpr ImVec4 Bar = grey(21);      // barre de menus, de titres, d'onglets, glissières
constexpr ImVec4 Panel = grey(36);    // le fond des fenêtres
constexpr ImVec4 Popup = grey(46);    // menus, listes déroulantes, infobulles, en-têtes
constexpr ImVec4 Line = grey(68);     // bordures et séparateurs
constexpr ImVec4 SoftLine = grey(54); // filets entre les cellules d'un tableau
constexpr ImVec4 Text = grey(215);    // le texte
// Le texte grisé : lisible (4,5 contre le plus clair des fonds, le menu) et nettement plus sombre
// que le texte (un rapport de 2 : à 178 il n'en faisait que 1,3 et une entrée grisée ne se voyait
// pas). Il se dessine **plein**, jamais sous `DisabledAlpha` (`actionItem`, `disabledButton`). Le
// test des contrastes (`tests/ui_style_test.cpp`) tient les deux bornes.
constexpr ImVec4 TextDim = grey(150);
constexpr ImVec4 Clear{0.0f, 0.0f, 0.0f, 0.0f};

/// Le grisé d'`ImGui::BeginDisabled` pour les widgets d'ImGui lui-même (les champs de l'inspecteur
/// d'une donnée en lecture seule) : il multiplie l'alpha de tout ce qu'il dessine. 0,62 : le texte
/// y reste à 4,5 contre le menu quand la cible mélange en gamma, et se grise nettement. Les
/// entrées de menu et les boutons de l'éditeur ne passent pas par lui : ils sont dessinés en
/// `TextDisabled` plein (un alpha de 0,8 sur le texte, comme ImGui le fait, ne s'y distinguait pas
/// du texte sur une cible qui mélange en linéaire : 1,22 de rapport).
constexpr float DisabledAlpha = 0.62f;

/// Les tailles, **à l'échelle 1** : `ScaleAllSizes` les multiplie ensuite. Réglées pour Inter à
/// 15 px : une ligne de champ de 23 px, des rondeurs de 2 à 4 px, des fenêtres carrées (celles d'un
/// éditeur à panneaux se touchent).
void setMetrics(ImGuiStyle& style)
{
    style.WindowPadding = {8.0f, 8.0f};
    style.FramePadding = {6.0f, 4.0f};
    style.ItemSpacing = {8.0f, 4.0f};
    style.ItemInnerSpacing = {6.0f, 4.0f};
    style.CellPadding = {6.0f, 3.0f};
    style.IndentSpacing = 18.0f;
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 10.0f;

    style.WindowRounding = 0.0f;
    style.ChildRounding = 2.0f;
    style.PopupRounding = 3.0f;
    style.FrameRounding = 3.0f;
    style.GrabRounding = 2.0f;
    style.ScrollbarRounding = 3.0f;
    style.TabRounding = 3.0f;

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.TabBorderSize = 0.0f;
    style.TabBarOverlineSize = 2.0f;
    style.SeparatorTextBorderSize = 1.0f;
    // Les repères X, Y, Z des champs de vecteur (`ImGuiSliderFlags_ColorMarkers`) : à 0, ImGui n'en
    // dessine aucun, en silence.
    style.ColorMarkerSize = 3.0f;
}

/// Les couleurs, en gris neutres et un accent. Aucun alpha (`applyEditorStyle`), sauf `Clear` là où
/// rien ne doit se voir, et le voile de la modale.
void setColors(ImVec4* colors)
{
    colors[ImGuiCol_Text] = Text;
    colors[ImGuiCol_TextDisabled] = TextDim;
    colors[ImGuiCol_WindowBg] = Panel;
    colors[ImGuiCol_ChildBg] = Clear; // un enfant se pose sur le fond de sa fenêtre
    colors[ImGuiCol_PopupBg] = Popup;
    colors[ImGuiCol_Border] = Line;
    colors[ImGuiCol_BorderShadow] = Clear;

    colors[ImGuiCol_FrameBg] = Recessed;
    colors[ImGuiCol_FrameBgHovered] = grey(28);
    colors[ImGuiCol_FrameBgActive] = grey(38);

    // Une fenêtre flottante : sa barre de titre s'éclaircit quand elle a le focus, et la sépare du
    // panneau qu'elle couvre (même gris de fond, une bordure d'un pixel pour seule autre limite).
    colors[ImGuiCol_TitleBg] = Bar;
    colors[ImGuiCol_TitleBgActive] = grey(44);
    colors[ImGuiCol_TitleBgCollapsed] = Bar;
    colors[ImGuiCol_MenuBarBg] = Bar;

    colors[ImGuiCol_ScrollbarBg] = Bar;
    colors[ImGuiCol_ScrollbarGrab] = grey(72);
    colors[ImGuiCol_ScrollbarGrabHovered] = grey(100);
    colors[ImGuiCol_ScrollbarGrabActive] = grey(128);

    colors[ImGuiCol_CheckMark] = grey(240);
    colors[ImGuiCol_CheckboxSelectedBg] = EditorAccent;
    // L'accent désigne ce qui est actif : la poignée d'un curseur est grise au repos, bleue tant
    // qu'on la tient. Assez sombre pour que la valeur qu'elle recouvre reste lisible (5,6 contre le
    // texte).
    colors[ImGuiCol_SliderGrab] = grey(80);
    colors[ImGuiCol_SliderGrabActive] = EditorAccent;

    colors[ImGuiCol_Button] = grey(56);
    colors[ImGuiCol_ButtonHovered] = grey(72);
    colors[ImGuiCol_ButtonActive] = EditorAccent;

    // Gris : l'accent d'une ligne choisie se pose à part (`pushSelectedRowColors`), car ces trois
    // couleurs font aussi les en-têtes de composant.
    colors[ImGuiCol_Header] = Popup;
    colors[ImGuiCol_HeaderHovered] = grey(62);
    colors[ImGuiCol_HeaderActive] = grey(78);

    colors[ImGuiCol_Separator] = Line;
    colors[ImGuiCol_SeparatorHovered] = EditorAccentLight;
    colors[ImGuiCol_SeparatorActive] = EditorAccent;
    colors[ImGuiCol_ResizeGrip] = grey(70);
    colors[ImGuiCol_ResizeGripHovered] = EditorAccentLight;
    colors[ImGuiCol_ResizeGripActive] = EditorAccent;
    colors[ImGuiCol_InputTextCursor] = grey(240);

    // Des onglets plats : le non choisi se fond dans sa barre, le choisi dans le panneau, sous un
    // filet de deux pixels (`TabBarOverlineSize`), d'accent quand la barre a le focus et gris clair
    // sinon : sans le filet, l'onglet visible d'un nœud sans focus ne se distinguerait que par 15
    // niveaux de gris (le test le garde).
    colors[ImGuiCol_Tab] = Bar;
    colors[ImGuiCol_TabHovered] = grey(56);
    colors[ImGuiCol_TabSelected] = Panel;
    colors[ImGuiCol_TabSelectedOverline] = EditorAccent;
    colors[ImGuiCol_TabDimmed] = Bar;
    colors[ImGuiCol_TabDimmedSelected] = Panel;
    colors[ImGuiCol_TabDimmedSelectedOverline] = grey(115);
    colors[ImGuiCol_DockingPreview] = {EditorAccent.x, EditorAccent.y, EditorAccent.z, 0.45f};
    colors[ImGuiCol_DockingEmptyBg] = Recessed;

    // Courbes et histogrammes : gris au repos, comme la poignée d'un curseur.
    colors[ImGuiCol_PlotLines] = grey(170);
    colors[ImGuiCol_PlotLinesHovered] = EditorAccentLight;
    colors[ImGuiCol_PlotHistogram] = grey(130);
    colors[ImGuiCol_PlotHistogramHovered] = EditorAccentLight;

    colors[ImGuiCol_TableHeaderBg] = Popup;
    colors[ImGuiCol_TableBorderStrong] = Line;
    colors[ImGuiCol_TableBorderLight] = SoftLine;
    colors[ImGuiCol_TableRowBg] = Clear;
    colors[ImGuiCol_TableRowBgAlt] = grey(41);

    colors[ImGuiCol_TextLink] = EditorAccentLight;
    colors[ImGuiCol_TextSelectedBg] = EditorAccentDark;
    colors[ImGuiCol_TreeLines] = grey(88);
    colors[ImGuiCol_DragDropTarget] = EditorAccentLight;
    colors[ImGuiCol_DragDropTargetBg] = Clear;
    colors[ImGuiCol_NavCursor] = EditorAccentLight;
    colors[ImGuiCol_NavWindowingDimBg] = {0.0f, 0.0f, 0.0f, 0.5f};
    colors[ImGuiCol_ModalWindowDimBg] = {0.0f, 0.0f, 0.0f, 0.55f};
}

/// Le canal sRGB `value` (0 à 1) en lumière linéaire (IEC 61966-2-1), comme le matériel le fait en
/// lisant une texture sRGB.
float linearOfSrgbChannel(float value)
{
    return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

} // namespace

void applyEditorStyle(ImGuiStyle& style, float displayScale)
{
    LEVAIN_ASSERT(displayScale > 0.0f,
                  "applyEditorStyle : une échelle d'écran strictement positive");
    ImGuiStyle fresh;
    setMetrics(fresh);
    setColors(fresh.Colors);
    fresh.DisabledAlpha = DisabledAlpha; // un alpha, que `ScaleAllSizes` ne touche pas
    fresh.ScaleAllSizes(displayScale);
    fresh.FontSizeBase = style.FontSizeBase;
    fresh.FontScaleMain = style.FontScaleMain;
    fresh.FontScaleDpi = displayScale;
    style = fresh;
}

int pushSelectedRowColors(bool selected)
{
    if (!selected)
    {
        return 0;
    }
    ImGui::PushStyleColor(ImGuiCol_Header, EditorAccentDark);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, EditorAccentHover);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, EditorAccent);
    return 3;
}

ImVec4 overlayBackground()
{
    return {Bar.x, Bar.y, Bar.z, OverlayAlpha};
}

float relativeLuminance(const ImVec4& srgb)
{
    return (0.2126f * linearOfSrgbChannel(srgb.x)) + (0.7152f * linearOfSrgbChannel(srgb.y)) +
           (0.0722f * linearOfSrgbChannel(srgb.z));
}

float contrastRatio(const ImVec4& first, const ImVec4& second)
{
    const float a = relativeLuminance(first);
    const float b = relativeLuminance(second);
    return (std::max(a, b) + 0.05f) / (std::min(a, b) + 0.05f);
}

} // namespace levain::ui
