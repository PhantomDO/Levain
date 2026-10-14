#include "levain/ui/style.hpp"

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
constexpr ImVec4 Clear{0.0f, 0.0f, 0.0f, 0.0f};

/// Les couleurs, en gris neutres et un accent. Aucun alpha (`applyEditorStyle`), sauf `Clear` là où
/// rien ne doit se voir, et le voile de la modale.
void setColors(ImVec4* colors)
{
    colors[ImGuiCol_Text] = Text;
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

    // Gris : ces trois couleurs font aussi les en-têtes de composant.
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

} // namespace

void applyEditorStyle(ImGuiStyle& style, float displayScale)
{
    LEVAIN_ASSERT(displayScale > 0.0f,
                  "applyEditorStyle : une échelle d'écran strictement positive");
    ImGuiStyle fresh;
    setColors(fresh.Colors);
    fresh.ScaleAllSizes(displayScale);
    fresh.FontSizeBase = style.FontSizeBase;
    fresh.FontScaleMain = style.FontScaleMain;
    fresh.FontScaleDpi = displayScale;
    style = fresh;
}

} // namespace levain::ui
