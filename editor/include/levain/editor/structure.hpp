#pragma once

// Renommer une entité (ADR-0038, décisions 6 et 10) : la commande de structure de la pile et son
// opération, **sans la pile** (qui est dans `undo.hpp`). `RenameCommand` garde les deux noms.
//
// La logique est en fonctions libres dont toutes les dépendances sont des paramètres (ADR-0011).
// Équivalents : `Undo.RecordObject` d'Unity sur le nom, une transaction `FScopedTransaction`
// d'Unreal.

#include <optional>
#include <string>
#include <string_view>

#include <flecs.h>

#include "levain/core/error.hpp"

namespace levain::editor
{

struct RenameCommand
{
    flecs::entity_t entity = 0;
    /// `nullopt` : l'entité n'avait pas de nom (une entité de la scène en a toujours un, sauf
    /// celles d'un module de test).
    std::optional<std::string> before;
    std::string after;
};

// --- Les refus, dits avant d'écrire -------------------------------------------------------------

/// Pourquoi un nom ne convient pas à `entity` (déjà vivante), ou rien s'il convient. La phrase que
/// le champ de renommage montre et que la console répète : une entité qui n'est pas de la scène, un
/// nœud `Generated` (son générateur le nomme), un nom que `refusalOfName` refuse (vide, « . »,
/// « :: », « #12 », « _ », « * », « $ » à la racine), un frère qui le porte déjà. Un nom égal au
/// sien n'est pas un refus : ce n'est pas un changement (`renameEntity` ne pose rien).
[[nodiscard]] std::optional<std::string>
renameRefusalOf(const flecs::world& world, flecs::entity_t entity, std::string_view wanted);

/// stepRename : remet l'ancien nom (annuler) ou le nouveau (refaire). Refuse, le monde intact, une
/// entité morte ou un nom que `renameRefusalOf` ne laisserait plus passer.
[[nodiscard]] core::Result<void> stepRename(flecs::world& world, const RenameCommand& command,
                                            bool redo);

/// Écrit le nom d'une entité : le crochet de flecs qui met l'index de noms à jour (`lookup`).
void writeName(flecs::world& world, flecs::entity_t entity, const std::optional<std::string>& name);

} // namespace levain::editor
