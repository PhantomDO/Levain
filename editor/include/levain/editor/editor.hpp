#pragma once

// L'éditeur (ADR-0018, ADR-0034, choix 6B) : une bibliothèque au-dessus d'`app`, que seuls les
// exécutables éditeur lient (`levain_sandbox_editor`, plus tard `rando_editor`). Le jeu livré et
// la page web n'en embarquent rien.

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <flecs.h>
#include <nvrhi/nvrhi.h>

#include "levain/app/app.hpp"
#include "levain/core/error.hpp"
#include "levain/core/log_ring.hpp"
#include "levain/editor/console.hpp"
#include "levain/editor/hierarchy.hpp"
#include "levain/editor/inspector.hpp"
#include "levain/editor/mode.hpp"
#include "levain/editor/view.hpp"

namespace levain::editor
{

struct Editor;

/// Ce que la ligne de commande demande à l'éditeur.
struct EditorOptions
{
    /// `--select chemin` : l'entité choisie au démarrage, par son chemin (`grid::cube_0_0`) ; le
    /// nom seul ne trouve qu'une racine. Pour la CI et les captures.
    std::optional<std::string> select;
    /// Appelé à la fin de chaque `ui`, avec l'éditeur tel que cette image l'a laissé : le banc
    /// d'essai de l'App (ADR-0036, morceau 2) y lit ce que les fenêtres ont dessiné, que la boucle
    /// ne montre pas de l'extérieur. Vide hors des tests.
    std::function<void(const Editor&)> afterUi;
};

/// Retire de la ligne de commande les options de l'éditeur, que le programme refuserait, avant
/// qu'il ne lise les siennes. Les options vont par paires, nom et valeur, comme celles d'`app` ;
/// le reste garde son ordre, et un `--select` sans valeur y reste : le programme le refuse.
[[nodiscard]] std::vector<char*> takeEditorOptions(std::span<char* const> arguments,
                                                   EditorOptions& options);

/// Ce que l'éditeur garde d'une image à l'autre.
struct Editor
{
    /// L'entité choisie, par son identifiant complet, génération comprise : une entité détruite,
    /// dont flecs recycle l'index, ne passe pas pour la nouvelle (`selectedIfAlive`).
    flecs::entity_t selected = 0;
    Hierarchy hierarchy;
    Inspector inspector;
    /// Le mode (ADR-0036) : l'éditeur s'ouvre en Édition.
    ModeState mode;
    /// Le journal du processus depuis `withEditor` (et non depuis la première image : la fenêtre et
    /// le device y ont écrit), partagé avec la fonction de démarrage qui le garde en vie.
    std::shared_ptr<core::LogCapture> log;
    Console console;
    /// La Vue (ADR-0036, décision 7) : la scène dans une fenêtre, son rectangle et sa taille.
    View view;
};

/// Le backend graphique où l'éditeur tourne : tous sauf WebGPU (ADR-0036, décision 7). La page web
/// n'a pas d'éditeur, et l'éditeur natif sur Dawn, qui le pourrait, n'est pas du périmètre de M7.7
/// : la Vue (la scène dans une texture, rendue puis montrée) n'y est ni écrite ni éprouvée. Un
/// refus bruyant, qui nomme l'option, plutôt qu'un éditeur dont rien ne dit qu'il marche (règle
/// n°7). **À appeler avant `runApp`** (le `main` de l'exécutable éditeur, une fois les options
/// lues) : `withEditor` ne démarre qu'après la fenêtre et le device, et refuserait trop tard, après
/// avoir ouvert un device Dawn pour rien.
[[nodiscard]] core::Result<void> requireEditorBackend(nvrhi::GraphicsAPI api);

/// L'entité choisie si elle vit encore ; l'entité nulle sinon, ou sans choix. Une sélection
/// survit ainsi à la destruction de son entité, sans jamais désigner une entité morte.
[[nodiscard]] flecs::entity selectedIfAlive(const flecs::world& world, flecs::entity_t selected);

/// La fonction de démarrage du programme, enveloppée (ADR-0029) : `start`, puis l'entité de
/// `--select`, dont l'absence fait échouer le démarrage ; les panneaux de l'éditeur passent après
/// les fenêtres du programme (`FrameHooks::ui`), panneaux ouverts seulement, et son bilan avant
/// celui du programme (`FrameHooks::finish`). **Le `frame` du programme est enveloppé aussi** : en
/// Édition, qui est le mode du départ, il n'est jamais appelé, la simulation est à l'arrêt et le
/// jeu ne reçoit rien (`App::inputRoute`). Alt+P ou la barre de mode passent en « Jouer (sans
/// retour) », qu'Échap quitte (`mode.hpp`).
[[nodiscard]] app::StartFunction withEditor(app::StartFunction start, EditorOptions options);

} // namespace levain::editor
