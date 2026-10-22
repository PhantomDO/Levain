#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <flecs.h>

#include "levain/core/error.hpp"

/// Les noms d'entités (ADR-0038, décision 6). Un nom est ce que `lookup` et le fichier de scène
/// retrouvent : il doit être unique entre frères, et ne pas être un mot que flecs interprète. Dans
/// `scene` parce que le chargeur, `takeSnapshot` (qui refuse deux frères du même nom) et le champ
/// de renommage de l'éditeur s'en servent tous.
namespace levain::scene
{

/// Pourquoi un nom est refusé : une cause par piège, pour que le champ de l'éditeur la dise.
enum class NameRefusal : std::uint8_t
{
    /// Un nom vide : `lookup("")` ne trouve rien.
    Empty,
    /// « . » : le chemin « a.b » éclate au rechargement (#254).
    ContainsDot,
    /// « :: » : le séparateur de chemin de flecs.
    ContainsScope,
    /// « # » suivi de chiffres : `lookup` rend l'entité n° 12 de flecs, pas la nôtre.
    IdentifierSyntax,
    /// Tout nom qui commence par « # », à la racine : le chemin lit « # » comme un identifiant.
    HashAtRoot,
    /// « _ », « * », « $ » seuls, à la racine : `EcsAny`, `Wildcard`, une variable.
    WildcardAtRoot,
};

/// Le refus d'un nom, ou rien s'il est bon. `atRoot` : « _ », « * » et « $ » ne sont pris pour des
/// mots de flecs qu'à la racine ; sous un parent, `lookup("P::_")` les retrouve (essayé,
/// flecs 4.1.6).
[[nodiscard]] std::optional<NameRefusal> refusalOfName(std::string_view name, bool atRoot);

/// La phrase du refus, que le champ de renommage montre telle quelle.
[[nodiscard]] std::string_view reasonOf(NameRefusal refusal);

[[nodiscard]] inline bool isValidEntityName(std::string_view name, bool atRoot)
{
    return !refusalOfName(name, atRoot).has_value();
}

/// uniqueNameAmong : le nom voulu s'il est libre parmi les enfants de `parent` (0 : les racines),
/// sinon « Entité 2 », « Entité 3 »… Un nom nu commence à 2 et prend le plus petit numéro libre. Un
/// nom déjà numéroté reprend **après** son numéro : « Cube 2 » pris donne « Cube 3 », « Lampe 10 »
/// pris donne « Lampe 11 » (pas « Lampe 2 », qui ferait remonter la liste), et jamais « Cube 2 2 ».
/// Un nom refusé par `refusalOfName` est une erreur qui le dit. `self` : l'entité qu'on renomme,
/// que son propre nom ne rend pas « pris ».
[[nodiscard]] core::Result<std::string> uniqueNameAmong(const flecs::world& world,
                                                        flecs::entity_t parent,
                                                        std::string_view wanted,
                                                        flecs::entity_t self = 0);

} // namespace levain::scene
