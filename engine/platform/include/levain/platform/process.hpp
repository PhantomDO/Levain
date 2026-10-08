#pragma once

#include <span>
#include <string>

#include "levain/core/error.hpp"

namespace levain::platform
{

/// Une variable ajoutée à l'environnement du programme lancé, qui hérite du reste.
struct EnvironmentVariable
{
    std::string name;
    std::string value;
};

struct ProcessOutput
{
    int exitCode = 0;
    std::string output; ///< Sortie standard et sortie d'erreur, mêlées dans l'ordre d'écriture.
};

/// Lance `arguments[0]` avec les arguments suivants, attend sa fin et rend sa sortie. Bloquant.
/// Un programme introuvable est un échec récupérable (ADR-0008) ; un programme qui échoue rend un
/// `exitCode` non nul, que l'appelant interprète. `environment` s'ajoute à l'environnement de ce
/// processus, dont le programme hérite.
[[nodiscard]] core::Result<ProcessOutput>
runProcess(std::span<const std::string> arguments,
           std::span<const EnvironmentVariable> environment = {});

} // namespace levain::platform
