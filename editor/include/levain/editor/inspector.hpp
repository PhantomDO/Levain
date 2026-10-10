#pragma once

// L'inspecteur de l'éditeur (ADR-0034, « Les panneaux ») : les composants de l'entité choisie, un
// widget par champ, lus dans la description de flecs sans une ligne propre à un composant. Un
// composant non décrit est une ligne à son nom.

#include <optional>
#include <string>
#include <unordered_map>

#include <flecs.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

#include "levain/assets/asset_id.hpp"
#include "levain/assets/registry.hpp"

namespace levain::editor
{

/// Ce que l'inspecteur garde d'une image à l'autre.
struct Inspector
{
    /// Les types que l'inspecteur dessine à sa façon, lus à sa création : les chercher pendant le
    /// dessin enregistrerait un type absent du monde au milieu du parcours. Les feuilles glm, d'un
    /// bloc ; le quaternion, en angles ; l'entité et l'asset, par leur nom.
    flecs::entity_t vec2 = 0;
    flecs::entity_t vec3 = 0;
    flecs::entity_t quat = 0;
    flecs::entity_t entity = 0;
    flecs::entity_t assetRef = 0;
    /// Le registre qui nomme les assets ; sans lui, leur GUID. Il doit survivre à l'inspecteur.
    const assets::AssetRegistry* registry = nullptr;
    /// Les angles tapés pour chaque quaternion, par identifiant du widget (`eulerHint`).
    std::unordered_map<ImGuiID, glm::vec3> typedAngles;
    /// Les champs dessinés à la dernière image pour la sélection : le bilan que lit la CI.
    int fieldsDrawn = 0;
    /// `commitEdit` a écrit une valeur pendant `drawInspector` : l'éditeur demande alors à `App`
    /// de recomposer les matrices monde après `ui` (`recomposeAfterUi`, ADR-0036, décision 2), pour
    /// que l'image montre le `Transform` tapé. Remis à faux au début de chaque `drawInspector`.
    bool wrote = false;
};

/// L'inspecteur d'un monde qui a importé la scène (ses feuilles glm) ; `registry` nomme les assets.
[[nodiscard]] Inspector createInspector(const flecs::world& world,
                                        const assets::AssetRegistry* registry = nullptr);

/// eulerHint : les angles à montrer pour `rotation`, en degrés : tangage (X), lacet (Y), roulis
/// (Z), lacet d'abord comme `applyFpsInput` (R = Ry·Rx·Rz). Le lacet couvre ±180° et le tangage
/// ±90° : un tangage de 200° se relit en -20° avec le lacet et le roulis retournés. Relire les
/// angles du quaternion à chaque image ferait sauter ce que la personne tape ; on garde donc les
/// angles tapés
/// (`typed`) tant qu'ils donnent encore `rotation`, et on ne relit le quaternion que si quelqu'un
/// d'autre l'a changé.
[[nodiscard]] glm::vec3 eulerHint(const glm::quat& rotation, const std::optional<glm::vec3>& typed);

/// Le quaternion normalisé de ces angles : ce qui s'écrit dans le composant.
[[nodiscard]] glm::quat rotationFromEuler(glm::vec3 degrees);

/// entityFieldOf : l'entité que désigne un champ `flecs::entity`, lu comme tel. Jamais par les
/// fonctions de flecs pour ce type opaque (`EcsOpaque::serialize`, `assign_*`) : il les appelle par
/// un pointeur de fonction d'un autre type, ce que UBSan arrête (ADR-0034). `field` pointe le champ
/// dans la copie du composant.
[[nodiscard]] flecs::entity entityFieldOf(const flecs::world& world, const void* field);

/// Ce que l'inspecteur écrit pour une entité : son chemin, « aucune » pour la nulle, « (détruite) »
/// pour une qui ne vit plus.
[[nodiscard]] std::string entityLabelOf(flecs::entity entity);

/// Ce que l'inspecteur écrit pour un asset : le nom du fichier au registre, sinon le GUID, puis
/// l'indice du sous-asset (« Fox.gltf #0 ») ; « aucun » pour une référence qui ne désigne rien.
[[nodiscard]] std::string assetNameOf(const assets::AssetRegistry* registry,
                                      const assets::AssetRef& ref);

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
int inspectComponent(flecs::world& world, Inspector& inspector, flecs::entity entity,
                     flecs::entity_t component);

/// La fenêtre « Inspecteur », ancrée au premier affichage au nœud `dock` de la disposition
/// d'`app` : chaque composant de l'entité choisie, si elle vit encore (`selectedIfAlive`). Le monde
/// doit être différé (`defer_begin`) : les écritures passent, et leurs observateurs, après le
/// parcours des composants de l'entité.
void drawInspector(flecs::world& world, Inspector& inspector, flecs::entity_t selected,
                   ImGuiID dock);

} // namespace levain::editor
