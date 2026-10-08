#include <array>
#include <string>

#include <doctest/doctest.h>

#include "levain/platform/process.hpp"

using levain::platform::runProcess;

// LEVAIN_PROCESS_HELPER (tests/CMakeLists.txt) : un programme de la cible, compilé avec les tests
// (tests/process_helper.cpp). Le test lance donc la même chose sous Linux et sous Windows ; le
// cmake de l'hôte, lui, n'est pas un programme que l'exe Windows sache lancer.

TEST_CASE("runProcess rend la sortie et le code de retour d'un programme")
{
    const std::array<std::string, 3> command{LEVAIN_PROCESS_HELPER, "echo", "bonjour"};
    const auto result = runProcess(command);

    REQUIRE(result.has_value());
    CHECK(result->exitCode == 0);
    CHECK(result->output == "bonjour\n");
}

TEST_CASE("runProcess rend le code d'échec d'un programme qui échoue")
{
    const std::array<std::string, 3> command{LEVAIN_PROCESS_HELPER, "exit", "3"};
    const auto result = runProcess(command);

    REQUIRE(result.has_value());
    CHECK(result->exitCode == 3);
}

TEST_CASE("runProcess signale un programme introuvable comme un échec récupérable")
{
    const std::array<std::string, 1> command{"/chemin/qui/n/existe/pas"};
    const auto result = runProcess(command);

    // Selon la plateforme, l'échec vient du lancement lui-même ou du code de retour du
    // processus enfant, qui n'a pas pu exécuter le programme.
    CHECK((!result.has_value() || result->exitCode != 0));
}
