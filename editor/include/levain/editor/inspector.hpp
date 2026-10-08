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

/// Les champs d'un composant décrit de `entity`, un widget grisé chacun, aux valeurs de l'image ;
/// rend leur nombre, 0 sans description. Les widgets dessinent une copie, jamais la table. Dans la
/// fenêtre courante d'ImGui.
int inspectComponent(flecs::world& world, const Inspector& inspector, flecs::entity entity,
                     flecs::entity_t component);

/// La fenêtre « Inspecteur », ancrée au premier affichage au nœud `dock` de la disposition
/// d'`app` : chaque composant de l'entité choisie, si elle vit encore (`selectedIfAlive`).
void drawInspector(flecs::world& world, Inspector& inspector, flecs::entity_t selected,
                   ImGuiID dock);

} // namespace levain::editor
