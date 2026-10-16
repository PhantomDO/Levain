#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <flecs.h>

#include "levain/core/error.hpp"

/// L'instantané de la scène (ADR-0037) : ce que Play garde pour que Stop rende la scène telle
/// qu'elle était, en place, avec les mêmes identifiants. Sans éditeur ni GPU : seul l'éditeur
/// l'appelle, mais `scene` le teste, web compris.
namespace levain::scene
{

/// Posée sur l'entité d'un composant, comme `Authored` : ce que le module range là n'est pas la
/// scène. L'instantané ne le copie pas (même s'il ne se copie pas), et Stop le retire des
/// entités. C'est le cas des handles de Jolt, qui désigneraient des corps détruits.
///
///     world.component<BodyHandle>().add<scene::Transient>();
struct Transient
{
};

/// Une table d'entités de flecs plutôt que de la scène : modules, systèmes, observateurs,
/// requêtes, composants, prefabs. La hiérarchie les tait, table par table et non entité par
/// entité ; un composant qui porte sa propre valeur va dans « Singletons ».
[[nodiscard]] bool isEngineInternal(const flecs::table& table);

/// Une entité de la scène : ce que la Hiérarchie pourrait montrer. Vivante, ni flecs lui-même (un
/// index sous `EcsFirstUserEntityId`), ni `isEngineInternal`, ni rangée par flecs sous un module :
/// sa **table** porte `(ChildOf, *)`. On lit la table, car un enfant de `flecs::Parent` répond oui
/// à `entity.has(ChildOf, *)` sans être sous un module (ADR-0015, ADR-0037).
[[nodiscard]] bool isSceneEntity(const flecs::world& world, flecs::entity_t entity);

/// Une valeur de composant copiée par les crochets de son type (`ecs_value_copy`), jamais par
/// octets : un type qui possède de la mémoire se copie bien. Elle garde le monde pour se libérer :
/// **un instantané ne survit pas à son monde**.
class ValueBox
{
public:
    ValueBox(const flecs::world& world, flecs::entity_t type, const void* source);
    ValueBox(const ValueBox&) = delete;
    ValueBox& operator=(const ValueBox&) = delete;
    ValueBox(ValueBox&& other) noexcept;
    ValueBox& operator=(ValueBox&& other) noexcept;
    ~ValueBox();

    [[nodiscard]] const void* data() const { return m_data; }

    [[nodiscard]] flecs::entity_t type() const { return m_type; }

private:
    ecs_world_t* m_world;
    flecs::entity_t m_type;
    void* m_data;
};

/// Un composant (ou une paire à valeur) d'une entité, avec sa valeur.
struct ComponentValue
{
    flecs::entity_t id; ///< L'identifiant dans le type de l'entité, paire comprise.
    ValueBox value;
};

/// Une entité de la scène telle que Play la trouve.
struct EntityRecord
{
    flecs::entity_t id = 0; ///< Avec sa génération : un identifiant gardé ne vaut que pour elle.
    std::optional<std::string> name;
    flecs::entity_t parent = 0; ///< `flecs::Parent`, 0 pour une racine.
    /// Le type complet, trié : composants, étiquettes et paires. Sans ce que flecs tient lui-même
    /// (le nom, `flecs::Parent`, `(ParentDepth, n)`) ni ce qu'un module marque `Transient`.
    std::vector<flecs::entity_t> type;
    std::vector<ComponentValue> values;    ///< Celles des composants du type qui ont une valeur.
    std::vector<flecs::entity_t> children; ///< Les enfants de la scène, dans l'ordre de flecs.
    /// Les enfants internes (l'observateur d'un `onEnter`), qui restent après les autres. Un
    /// interne détruit en jeu ne revient pas : `differencesFrom` le nomme.
    std::vector<flecs::entity_t> internalChildren;
};

/// Toute la scène, une entité par ligne, triée par index.
struct Snapshot
{
    std::vector<EntityRecord> entities;
};

/// takeSnapshot : copie la scène. Refuse, en nommant l'entité et la cause, ce que Stop ne saurait
/// rendre : un composant qui ne se copie pas ou ne se construit pas par défaut
/// (`ECS_TYPE_HOOK_COPY_ILLEGAL`, `ECS_TYPE_HOOK_CTOR_ILLEGAL`) ; une instance de prefab
/// `(IsA, *)`, dont les valeurs héritées ne se copient pas entité par entité ; deux frères du
/// même nom ; une entité à `Transform` rangée par ChildOf hors d'un module. Play ne part pas, et
/// l'Édition reste.
[[nodiscard]] core::Result<Snapshot> takeSnapshot(const flecs::world& world);

/// Le premier index de la plage du jeu : ce qui naît en jeu a un identifiant d'au moins 2^24.
inline constexpr std::uint32_t PlayFirstIndex = 1U << 24U;

/// Les deux plages d'identifiants du monde (ADR-0037, décision 2), rendues par flecs et qu'il
/// garde : le monde les possède, et `IdRanges` ne lui survit pas. Chaque plage a sa liste d'index à
/// réutiliser : l'index d'une caisse détruite en jeu ne peut pas être repris par un observateur ou
/// une requête créés ensuite, qui restent après Stop. La caisse peut donc toujours revenir.
struct IdRanges
{
    const ecs_entity_range_t* edit = nullptr; ///< [1, 2^24 - 1], créée par flecs lui-même.
    const ecs_entity_range_t* play = nullptr; ///< [2^24, ...[, celle du jeu.
};

/// createIdRanges : à appeler juste après le monde, avant la moindre destruction. flecs refuse une
/// plage créée après une destruction, mais en Debug seulement (`ecs_check`, src/world.c:1518-1520,
/// retiré sous `FLECS_NDEBUG`) : en Release il corromprait le monde sans rien dire. Levain vérifie
/// donc lui-même (règle n°7) : aucune entité détruite, aucun index déjà au-delà de la plage de
/// l'édition, aucune plage déjà créée. Sinon une erreur, et le monde n'est pas touché.
[[nodiscard]] core::Result<IdRanges> createIdRanges(flecs::world& world);

/// Les identifiants que le monde donne aux nouvelles entités : ceux de l'édition ou ceux du jeu.
void useEditIds(flecs::world& world, const IdRanges& ranges);
void usePlayIds(flecs::world& world, const IdRanges& ranges);

/// Né en jeu : une entité de la scène d'index au moins 2^24. Le reste de ce que le jeu crée reste
/// (observateurs, systèmes, requêtes, composants).
[[nodiscard]] bool bornInPlay(const flecs::world& world, flecs::entity_t entity);

/// L'index d'une entité : les 32 bits bas de son identifiant, sans la génération.
[[nodiscard]] constexpr std::uint32_t entityIndexOf(flecs::entity_t id)
{
    return static_cast<std::uint32_t>(id);
}

/// Le comparateur de flecs (`order_by`) qui range les entités par **index**, jamais par identifiant
/// entier : la génération est dans les bits hauts, et une entité réveillée par Stop
/// (`ecs_make_alive`) passerait avant toutes les autres. Un seul pour la physique (l'ordre de
/// construction des corps) et la Hiérarchie (l'ordre des racines) : ce que Stop rend, c'est
/// l'index.
[[nodiscard]] constexpr int byEntityIndex(flecs::entity_t first, const void*,
                                          flecs::entity_t second, const void*)
{
    const std::uint32_t a = entityIndexOf(first);
    const std::uint32_t b = entityIndexOf(second);
    return (a > b) - (a < b);
}

/// Les entités de la scène vivantes, par index croissant.
[[nodiscard]] std::vector<flecs::entity_t> sceneEntities(const flecs::world& world);

/// Ce que la restauration n'a pas pu rendre sans erreur : l'observateur d'un `onEnter` ou d'un
/// `onExit` détruit en jeu part avec son volume (il est rangé dessous par `ChildOf`), et son code
/// ne se réveille pas. Le volume revient sans ses rappels : la console le dit.
struct RestoreReport
{
    std::vector<std::string> lostInternalChildren; ///< Une ligne par enfant interne perdu.
};

/// restoreSnapshot : rend la scène en place, avec les identifiants d'avant Play. Appelée **hors du
/// mode différé** (un refus sinon, le monde intact), après que la physique a fini le jeu. Les
/// étapes de l'ADR-0037, décision 3 :
///  1. détruire les entités nées en jeu (leurs enfants de la scène partent avec elles) ;
///  2. revenir à la plage de l'édition, **avant** le réveil : un index de la scène détruit en jeu
///     attend dans la liste de cette plage (src/storage/entity_index.c:275-285) ;
///  3. réveiller les entités détruites, avec leur génération. flecs refuse en Debug un index vivant
///     d'une autre génération (src/entity.c:3133-3139) ; Levain le vérifie avant l'étape 1, dans
///     tous les builds, et sort en erreur sans avoir touché au monde ;
///  4. rendre les parents (par profondeur croissante), les composants (ceux de trop retirés,
///     `Transient` compris), les valeurs (réécrites si elles ont changé, toujours si on ne sait pas
///     les comparer) et les noms ;
///  5. rendre l'ordre des enfants, la taille de la liste vérifiée ;
///  6. vérifier par `differencesFrom` : la moindre différence est une erreur qui les nomme, hors
///     les enfants internes perdus, que le rapport rend.
[[nodiscard]] core::Result<RestoreReport>
restoreSnapshot(flecs::world& world, const IdRanges& ranges, const Snapshot& snapshot);

/// differencesFrom : ce qui diffère entre le monde et l'instantané, une ligne par différence, vide
/// si la scène est identique. Les premières seulement (`MaxReportedDifferences`), puis le compte
/// des autres : la console les imprime telles quelles. Compare les identifiants et leur
/// génération, les entités en trop et manquantes, les noms (et `lookup(chemin) == id`), les
/// parents, les types, les valeurs, l'ordre des enfants et les enfants internes. Une valeur dont
/// le type n'est ni décrit ni comparable par un crochet `cmp` ne se compare pas.
[[nodiscard]] std::vector<std::string> differencesFrom(const flecs::world& world,
                                                       const Snapshot& snapshot);

inline constexpr std::size_t MaxReportedDifferences = 20;

/// Le résultat de la comparaison, les vraies différences d'un côté et les enfants internes perdus
/// de l'autre (que Stop ne peut pas réparer, et qui ont leur avertissement) : un compte ne se
/// déduit pas d'une soustraction de lignes plafonnées.
struct Comparison
{
    /// Le nombre de vraies différences, **sans plafond** : la CI lit ce compte (`editor.stop`).
    std::size_t differenceCount = 0;
    /// Les premières seulement (`MaxReportedDifferences`), puis la ligne qui compte les autres.
    std::vector<std::string> differences;
    std::vector<std::string> lostInternalChildren;
};

/// compareWithSnapshot : ce que fait `differencesFrom`, mais en deux listes et un compte.
[[nodiscard]] Comparison compareWithSnapshot(const flecs::world& world, const Snapshot& snapshot);

} // namespace levain::scene
