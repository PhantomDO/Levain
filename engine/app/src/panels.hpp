#pragma once

// Les panneaux de debug du moteur (ADR-0032) : des fenêtres ImGui ancrées autour de l'image.

#include <imgui.h>

struct ImGuiDockNode;

#include "levain/app/ui_layer.hpp"

namespace levain::app
{

struct App;

/// Les fenêtres du moteur, par leur clé de catalogue : `ui::labelOf` en fait le titre (ADR-0036).
inline constexpr const char* ImageWindow = "Image";
inline constexpr const char* PassesWindow = "Passes";
inline constexpr const char* SceneWindow = "Scène";

/// La disposition de départ, recréée à chaque lancement (pas d'`imgui.ini`). **Éditeur**
/// (`editor`, `UiLayer::editorLayout`) : à gauche un nœud vide (la hiérarchie), à droite un nœud
/// vide (l'inspecteur, seul), en bas Image, Passes et Scène en onglets, le centre libre (la Vue).
/// **Sinon** (le sandbox, *Rando*) celle d'avant l'éditeur : Image et Passes à gauche, l'une sur
/// l'autre, Scène à droite sur un nœud sans autre fenêtre, le centre libre (`bottom` reste nul) ;
/// aucun nœud vide qui prendrait des clics.
[[nodiscard]] DockNodes buildLayout(ImGuiID dockspace, ImVec2 size, bool editor);

/// Où se voit la scène (`App::sceneRect`) : le nœud central de la disposition, ou `whole` sans lui.
/// Un nœud central où une fenêtre est ancrée n'est pas un trou : rectangle vide, à son origine.
/// Interne à ImGui (`ImGuiDockNode`), donc ici et non dans `ui_layer.hpp`.
[[nodiscard]] ScreenRect sceneRectOf(const ImGuiDockNode* central, const ScreenRect& whole);

/// L'espace d'ancrage, qui couvre l'image et laisse voir la scène au centre, puis les trois
/// fenêtres du moteur : Image, Passes, Scène. Le programme ajoute les siennes (`FrameHooks::ui`),
/// ou complète « Scène » en rouvrant `ui::labelOf("Scène")` : panneaux ouverts seulement, car
/// fermés, l'espace d'ancrage n'est plus soumis, et ImGui laisse flotter cette fenêtre. La
/// disposition, construite au premier appel, garde ses nœuds dans `app.ui.dock` : l'éditeur y
/// ancre les siennes (ADR-0034).
void drawEnginePanels(App& app);

} // namespace levain::app
