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

TEST_CASE("runProcess ajoute des variables à l'environnement du programme, qui hérite du reste")
{
    // Le hot-reload d'un exe Windows en a besoin : WSL_UTF8=1 pour wsl.exe (ADR-0035).
    const std::array<levain::platform::EnvironmentVariable, 1> environment{
        {{.name = "LEVAIN_PROCESS_TEST", .value = "valeur \xC3\xA9"}}};

    const std::array<std::string, 3> added{LEVAIN_PROCESS_HELPER, "env", "LEVAIN_PROCESS_TEST"};
    const auto result = runProcess(added, environment);
    REQUIRE(result.has_value());
    CHECK(result->exitCode == 0);
    CHECK(result->output == "valeur \xC3\xA9\n");

    // Le reste de l'environnement suit : PATH, que tout système définit.
    const std::array<std::string, 3> inherited{LEVAIN_PROCESS_HELPER, "env", "PATH"};
    const auto path = runProcess(inherited, environment);
    REQUIRE(path.has_value());
    CHECK(path->exitCode == 0);

    // Sans elle, la variable n'existe pas : le code 65 du programme le dit.
    const auto without = runProcess(added);
    REQUIRE(without.has_value());
    CHECK(without->exitCode == 65);
}

TEST_CASE("runProcess signale un programme introuvable comme un échec récupérable")
{
    const std::array<std::string, 1> command{"/chemin/qui/n/existe/pas"};
    const auto result = runProcess(command);

    // Selon la plateforme, l'échec vient du lancement lui-même ou du code de retour du
    // processus enfant, qui n'a pas pu exécuter le programme.
    CHECK((!result.has_value() || result->exitCode != 0));
}
