#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

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

} // namespace levain::scene
