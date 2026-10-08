#pragma once

// L'inspecteur de l'éditeur (ADR-0034, « Les panneaux ») : les composants de l'entité choisie, un
// widget par champ, lus dans la description de flecs sans une ligne propre à un composant. Un
// composant non décrit est une ligne à son nom.

#include <string>

#include <flecs.h>
#include <imgui.h>

namespace levain::editor
{

/// Ce que l'inspecteur garde d'une image à l'autre.
struct Inspector
{
    /// Les feuilles glm, dessinées d'un bloc, lues à sa création : les chercher pendant le dessin
    /// enregistrerait un type absent du monde au milieu du parcours.
    flecs::entity_t vec2 = 0;
    flecs::entity_t vec3 = 0;
    /// Les champs dessinés à la dernière image pour la sélection : le bilan que lit la CI.
    int fieldsDrawn = 0;
};

/// L'inspecteur d'un monde qui a importé la scène (ses feuilles glm).
[[nodiscard]] Inspector createInspector(const flecs::world& world);

/// Le nom d'un composant dans l'inspecteur : sa clé de sauvegarde (`stableKeyOf`), sinon son
/// chemin. Une paire ou un identifiant à drapeaux : ce qu'en écrit flecs.
[[nodiscard]] std::string componentLabelOf(const flecs::world& world, flecs::id component);

/// Le nom de la constante d'une enum que `value` désigne ; nul si aucune ne correspond (un entier
/// écrit hors des constantes).
[[nodiscard]] const char* enumNameOf(const flecs::world& world, flecs::entity_t type,
                                     const void* value);

/// commitEdit : la seule écriture de l'inspecteur. Écrit `edited`, la copie que les widgets ont
/// changée, par `setComponentValue` (un `OnSet`), si le composant est une donnée d'auteur
/// (`Authored`) et que la copie diffère de `before` (`sameValue`). Un composant en lecture seule
/// n'est jamais écrit, même si un widget a changé sa copie. Rend si l'écriture a eu lieu.
bool commitEdit(flecs::world& world, flecs::entity entity, flecs::entity_t component,
                const void* before, const void* edited);

/// Les champs d'un composant décrit de `entity`, un widget chacun, aux valeurs de l'image ; rend
/// leur nombre, 0 sans description. Les widgets éditent une copie, jamais la table, et grisés sans
/// `Authored` ; une modification s'écrit par `commitEdit`, bornes imposées (`AlwaysClamp`). Dans la
/// fenêtre courante d'ImGui, le monde différé.
int inspectComponent(flecs::world& world, const Inspector& inspector, flecs::entity entity,
                     flecs::entity_t component);

/// La fenêtre « Inspecteur », ancrée au premier affichage au nœud `dock` de la disposition
/// d'`app` : chaque composant de l'entité choisie, si elle vit encore (`selectedIfAlive`). Le monde
/// doit être différé (`defer_begin`) : les écritures passent, et leurs observateurs, après le
/// parcours des composants de l'entité.
void drawInspector(flecs::world& world, Inspector& inspector, flecs::entity_t selected,
                   ImGuiID dock);

} // namespace levain::editor
