#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "levain/app/app.hpp"

// La commande qui relance le build des shaders (ADR-0014), et ce qu'elle devient pour un exe
// Windows compilé dans une distro WSL (ADR-0035, décision 5). Une fonction pure : les trois cas se
// testent depuis Linux, sans lancer ni cmake ni wsl.exe.

using levain::app::ExeSystem;
using levain::app::shaderReloadCommand;
using levain::app::WslBuild;

namespace
{

levain::app::ShaderBuild aBuild()
{
    return {.cmakeCommand = "/usr/bin/cmake",
            .buildDir = "/home/dev/Levain/build/windows-debug",
            .sourceDir = "/home/dev/Levain/shaders"};
}

WslBuild inDistro()
{
    return {.distro = "levain-dev", .winsysroot = "/home/dev/winsysroot"};
}

} // namespace

TEST_CASE("sous Linux, le build se relance par cmake, tel qu'avant")
{
    // Ce que l'arbre sait de WSL n'entre pas dans la commande : un exe Linux lance cmake lui-même.
    for (const WslBuild& wsl : {WslBuild{}, inDistro()})
    {
        CAPTURE(wsl.distro);
        const auto command = shaderReloadCommand(aBuild(), ExeSystem::Linux, wsl);
        REQUIRE(command.has_value());
        CHECK(command->arguments == std::vector<std::string>{"/usr/bin/cmake", "--build",
                                                             "/home/dev/Levain/build/windows-debug",
                                                             "--target", "levain_shaders"});
        CHECK(command->environment.empty());
    }
}

TEST_CASE("un exe Windows compilé dans une distro relance cmake par wsl.exe, dans cette distro")
{
    const auto command = shaderReloadCommand(aBuild(), ExeSystem::Windows, inDistro());
    REQUIRE(command.has_value());
    // --exec : sans shell ; env : le winsysroot que ~/.bashrc ne donne pas à --exec.
    CHECK(command->arguments ==
          std::vector<std::string>{"wsl.exe", "-d", "levain-dev", "--exec", "/usr/bin/env",
                                   "LEVAIN_WINSYSROOT=/home/dev/winsysroot", "/usr/bin/cmake",
                                   "--build", "/home/dev/Levain/build/windows-debug", "--target",
                                   "levain_shaders"});
    // Les messages de wsl.exe en UTF-8, pas en UTF-16.
    REQUIRE(command->environment.size() == 1);
    CHECK(command->environment[0].name == "WSL_UTF8");
    CHECK(command->environment[0].value == "1");
}

TEST_CASE("le dossier de build va tel quel à wsl.exe, sans shell")
{
    // « -- » passerait la commande au shell de la distro, où « ; » la couperait et « $ » se
    // développerait : --exec garde chaque argument entier.
    levain::app::ShaderBuild build = aBuild();
    build.buildDir = "/home/dev/Mes Jeux;$HOME/build/windows-debug";
    const auto command = shaderReloadCommand(build, ExeSystem::Windows, inDistro());
    REQUIRE(command.has_value());
    CHECK(command->arguments.at(3) == "--exec");
    CHECK(command->arguments.at(8) == "/home/dev/Mes Jeux;$HOME/build/windows-debug");
}

TEST_CASE("un exe Windows sans distro refuse de recharger, et le dit")
{
    // L'exe de la CI : compilé hors d'une distro, il ne sait lancer ni cmake ni slangc.
    const auto command = shaderReloadCommand(aBuild(), ExeSystem::Windows, WslBuild{});
    REQUIRE_FALSE(command.has_value());
    CHECK(command.error().code == levain::core::ErrorCode::Unsupported);
    CHECK(command.error().message.contains("WSL"));
}
