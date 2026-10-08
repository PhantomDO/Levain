#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace levain::core
{

/// La valeur d'une variable d'environnement, ou `std::nullopt` si elle n'existe pas (une variable
/// vide existe). Le piège : la CRT de Microsoft déclare `getenv` « unsafe », un avertissement qui
/// devient une erreur sous `-Werror`, et définir `_CRT_SECURE_NO_WARNINGS` couperait un
/// avertissement (règle n°4 de AGENTS.md). Cette fonction n'emploie pas l'API déconseillée.
[[nodiscard]] std::optional<std::string> environmentVariable(std::string_view name);

} // namespace levain::core
