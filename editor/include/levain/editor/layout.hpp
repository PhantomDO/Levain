#pragma once

// La disposition des fenêtres et les préférences, côté ImGui (ADR-0036, décision 10) : ce qui
// relie le fichier de `preferences.hpp` à l'éditeur vivant. À l'ouverture, le fichier est relu
// **avant la première image** : la disposition d'ImGui est rechargée, donc l'espace d'ancrage
// existe et `buildLayout` ne tourne pas (`drawEnginePanels` ne le bâtit que s'il manque) ; les
// nœuds que l'éditeur donne à ses fenêtres (`App::ui.dock`) sont retrouvés **par le nom des
// fenêtres** dans le texte relu, jamais par des identifiants gardés à part. À la fermeture, ou
// après « Réinitialiser la disposition », il est réécrit. **Une exécution scriptée n'en lit ni
// n'en écrit** (`isScriptedRun`).
// Équivalents : `Layout > Save/Load/Reset` d'Unity (les `.wlt`), `Window > Load Layout` d'Unreal,
// `Éditeur > Disposition` de Godot.

#include <filesystem>
#include <optional>

#include <imgui.h>

#include "levain/app/app.hpp"
#include "levain/editor/preferences.hpp"

namespace levain::editor
{

struct Editor;

/// L'organisation et l'application du dossier de l'utilisateur (`platform::prefPath`).
inline constexpr const char* PreferencesOrganization = "Levain";
inline constexpr const char* PreferencesApplication = "Editor";

/// **Les nœuds de l'éditeur retrouvés par le nom de ses fenêtres** dans la disposition d'ImGui
/// qu'on vient de relire : la Hiérarchie à gauche, l'Inspecteur à droite, la Vue au centre, en bas
/// la première de Console, Image, Passes ou Scène qui a gardé un nœud. Un nœud n'est gardé que
/// s'il existe (`DockBuilderGetNode`) : une fenêtre jamais dessinée ne dit rien, et le rôle reste
/// à 0 (la fenêtre flotte, plutôt que de viser un nœud mort).
[[nodiscard]] app::DockNodes dockNodesOfSavedLayout();

/// Ce que l'éditeur garde aujourd'hui : la disposition d'ImGui, vide si l'espace d'ancrage n'existe
/// pas (jamais bâti ni relu : la défaut se construira), la vitesse de la caméra, le choix de «
/// Temps réel » en Édition (celui d'avant, pendant qu'on joue), les fenêtres ouvertes et les
/// raccourcis relus.
[[nodiscard]] Preferences preferencesOf(const app::App& app, const Editor& editor);

/// Pose les préférences relues sur l'éditeur, avant la première image. Une disposition qui ne
/// crée pas l'espace d'ancrage (un texte vide de sens) est écartée et dite à la console : la
/// disposition par défaut se construira.
void applyPreferences(app::App& app, Editor& editor, const Preferences& preferences);

/// L'ouverture : où sont les préférences (`preferencesFileFor`), puis leur lecture. Un fichier
/// absent est une première session ; un fichier refusé est dit à la console, **mis de côté** en
/// `.bad` (le prochain enregistrement ne l'écrase pas : un raccourci écrit à la main y reste
/// lisible ; un second fichier refusé remplace le premier `.bad`) et la disposition par défaut se
/// construit. Si la mise de côté échoue, **la session n'enregistre pas** : le fichier refusé serait
/// écrasé à la fermeture. Un fichier **d'une version plus récente** n'est pas mis de côté non plus
/// et la session n'enregistre pas : l'écrire en v1 à la fermeture le rétrograderait.
/// `testDirectory` : voir `EditorOptions`.
void startPreferences(app::App& app, Editor& editor,
                      const std::optional<std::filesystem::path>& testDirectory);

/// Écrit les préférences s'il y a un fichier. Un échec est une erreur de la console, jamais un
/// arrêt : l'éditeur qui se ferme ne perd pas la session pour un disque plein.
void savePreferencesNow(const app::App& app, const Editor& editor);

/// **« Réinitialiser la disposition »** : la disposition par défaut sur l'espace d'ancrage, toutes
/// les fenêtres rouvertes et ancrées sur leurs nœuds ; la vitesse et « Temps réel » restent. À
/// appeler dans le `frame`, entre `NewFrame` et la soumission de l'espace d'ancrage.
void resetLayout(app::App& app, Editor& editor);

} // namespace levain::editor
