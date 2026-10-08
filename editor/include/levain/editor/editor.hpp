#pragma once

// L'éditeur (ADR-0018, ADR-0034, choix 6B) : une bibliothèque au-dessus d'`app`, que seuls les
// exécutables éditeur lient (`levain_sandbox_editor`, plus tard `rando_editor`). Le jeu livré et
// la page web n'en embarquent rien.

#include <optional>
#include <span>
#include <string>
#include <vector>

#include <flecs.h>

#include "levain/app/app.hpp"
#include "levain/editor/hierarchy.hpp"
#include "levain/editor/inspector.hpp"

namespace levain::editor
{

/// Ce que la ligne de commande demande à l'éditeur.
struct EditorOptions
{
    /// `--select chemin` : l'entité choisie au démarrage, par son chemin (`grid::cube_0_0`) ; le
    /// nom seul ne trouve qu'une racine. Pour la CI et les captures.
    std::optional<std::string> select;
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
};

/// L'entité choisie si elle vit encore ; l'entité nulle sinon, ou sans choix. Une sélection
/// survit ainsi à la destruction de son entité, sans jamais désigner une entité morte.
[[nodiscard]] flecs::entity selectedIfAlive(const flecs::world& world, flecs::entity_t selected);

/// La fonction de démarrage du programme, enveloppée (ADR-0029) : `start`, puis l'entité de
/// `--select`, dont l'absence fait échouer le démarrage ; les panneaux de l'éditeur passent après
/// les fenêtres du programme (`FrameHooks::ui`), panneaux ouverts seulement, et son bilan avant
/// celui du programme (`FrameHooks::finish`). La boucle ne change pas.
[[nodiscard]] app::StartFunction withEditor(app::StartFunction start, EditorOptions options);

} // namespace levain::editor
