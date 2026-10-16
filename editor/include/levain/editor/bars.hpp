#pragma once

// La barre d'outils et la barre d'état de l'éditeur (ADR-0036, décision 12), deux bandes qui
// retirent leur hauteur à l'espace d'ancrage, sous la barre de menus et au bas de la fenêtre.
// **La barre d'outils** : le mode (Jouer, ou arrêter), les outils (grisés jusqu'à M7.4)
// et Simuler (grisé jusqu'à M7.5), la vitesse de la caméra de l'éditeur et « Temps réel ».
// **La barre d'état** : le dernier message du journal et les compteurs d'avertissements et
// d'erreurs, ceux de la console (`core::LogRing`). La logique est en fonctions libres dont toutes
// les dépendances sont des paramètres (ADR-0011). Équivalents : la barre d'outils d'Unity (Play,
// Pause, les outils de la Scene View) et d'Unreal (Play, le menu de vitesse de la caméra du
// viewport), la barre d'état d'Unity (le dernier message de la console) et d'Unreal (la ligne de
// commande et le journal en bas).

#include <optional>
#include <string>
#include <vector>

#include <imgui.h>

#include "levain/core/log_ring.hpp"
#include "levain/editor/mode.hpp"
#include "levain/editor/shortcuts.hpp"

namespace levain::editor
{

/// Où la barre d'outils a été dessinée (le centre de chaque contrôle, en pixels de l'écran).
struct ToolbarLayout
{
    ImVec2 mode{};
    ImVec2 tool{};     ///< Le premier outil (grisé).
    ImVec2 simulate{}; ///< Simuler (grisé).
    ImVec2 speed{};    ///< Le curseur de la vitesse de la caméra.
    ImVec2 realTime{}; ///< La case « Temps réel ».
    float speedWidth = 0.0f;
};

/// Ce que la barre d'outils a décidé à cette image.
struct ToolbarResult
{
    std::optional<Mode> mode;    ///< Le bouton de mode : le mode qu'il propose.
    std::vector<Action> actions; ///< Les actions cliquées qui sont là (Temps réel).
    std::optional<float>
        flySpeed; ///< La nouvelle vitesse de la caméra, **bornée** (`clampFlySpeed`).
};

/// Dessine la barre d'outils sous la barre de menus. `flySpeed` est la vitesse de vol de la caméra
/// de l'éditeur, `realTime` l'état de la case. Panneaux ouverts seulement.
[[nodiscard]] ToolbarResult drawToolbar(Mode mode, float flySpeed, bool realTime,
                                        ToolbarLayout& layout);

/// Ce que la barre d'état dit : le dernier message de l'anneau (vide s'il est vide) et les comptes
/// de tous les messages reçus.
struct StatusLine
{
    std::string text;
    core::LogSeverity severity = core::LogSeverity::Info;
    core::LogCounts counts;
};

/// Le dernier message de l'anneau, sur une seule ligne (`singleLine` de la console), et ses
/// comptes : sous le mutex de l'anneau, car des fils journalisent (ADR-0036, décision 16), et sans
/// recopier les entrées que la console garde.
[[nodiscard]] StatusLine statusOf(const core::LogRing& ring);

/// Dessine la barre d'état au bas de la fenêtre. Panneaux ouverts seulement.
void drawStatusBar(const StatusLine& line);

} // namespace levain::editor
