#pragma once

// Les panneaux de debug du moteur (ADR-0032) : des fenêtres ImGui ancrées autour de l'image.

namespace levain::app
{

struct App;

/// L'espace d'ancrage, qui couvre l'image et laisse voir la scène au centre, puis les trois
/// fenêtres du moteur : Image, Passes, Scène. Le programme ajoute les siennes (`FrameHooks::ui`),
/// ou complète « Scène » en rouvrant une fenêtre du même nom : panneaux ouverts seulement, car
/// fermés, l'espace d'ancrage n'est plus soumis, et ImGui laisse flotter cette fenêtre.
void drawEnginePanels(App& app);

} // namespace levain::app
