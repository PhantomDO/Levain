#pragma once

// Les panneaux de debug du moteur (ADR-0032) : des fenêtres ImGui ancrées autour de l'image.

#include <imgui.h>

#include "levain/app/ui_layer.hpp"

namespace levain::app
{

struct App;

/// Les fenêtres du moteur, par leur clé de catalogue : `ui::labelOf` en fait le titre (ADR-0036).
inline constexpr const char* ImageWindow = "Image";
inline constexpr const char* PassesWindow = "Passes";
inline constexpr const char* SceneWindow = "Scène";

/// La disposition de départ, recréée à chaque lancement (pas d'`imgui.ini`) : Image et Passes à
/// gauche, l'une sur l'autre ; Scène à droite, sur un nœud encore vide, celui de l'inspecteur ; le
/// centre libre.
[[nodiscard]] DockNodes buildLayout(ImGuiID dockspace, ImVec2 size);

/// L'espace d'ancrage, qui couvre l'image et laisse voir la scène au centre, puis les trois
/// fenêtres du moteur : Image, Passes, Scène. Le programme ajoute les siennes (`FrameHooks::ui`),
/// ou complète « Scène » en rouvrant `ui::labelOf("Scène")` : panneaux ouverts seulement, car
/// fermés, l'espace d'ancrage n'est plus soumis, et ImGui laisse flotter cette fenêtre. La
/// disposition, construite au premier appel, garde ses nœuds dans `app.ui.dock` : l'éditeur y
/// ancre les siennes (ADR-0034).
void drawEnginePanels(App& app);

} // namespace levain::app
