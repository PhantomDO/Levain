#include <string>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include "levain/app/app.hpp"

// La commande qui relance le build des shaders (ADR-0014), et ce qu'elle devient pour un exe
// Windows compilé dans une distro WSL (ADR-0035, décision 5). Une fonction pure : les trois cas se
// testent depuis Linux, sans lancer ni cmake ni wsl.exe ; et le décodage de ce que wsl.exe écrit.

namespace
{

levain::app::ShaderBuild buildIn(std::string distro)
{
    return {.cmakeCommand = "/usr/bin/cmake",
            .buildDir = "/home/dev/Levain/build/windows-debug",
            .sourceDir = "/home/dev/Levain/shaders",
            .wslDistro = std::move(distro)};
}

} // namespace

TEST_CASE("sous Linux, le build se relance par cmake, tel qu'avant")
{
    // La distro n'entre pas dans la commande : un build Linux n'en a pas, et s'il en avait une, il
    // lancerait cmake lui-même.
    for (const char* distro : {"", "levain-dev"})
    {
        CAPTURE(distro);
        const auto command =
            levain::app::shaderReloadCommand(buildIn(distro), levain::app::HostSystem::Linux);
        REQUIRE(command.has_value());
        CHECK(*command == std::vector<std::string>{"/usr/bin/cmake", "--build",
                                                   "/home/dev/Levain/build/windows-debug",
                                                   "--target", "levain_shaders"});
    }
}

TEST_CASE("un exe Windows compilé dans une distro relance cmake par wsl.exe, dans cette distro")
{
    const auto command =
        levain::app::shaderReloadCommand(buildIn("levain-dev"), levain::app::HostSystem::Windows);
    REQUIRE(command.has_value());
    CHECK(*command == std::vector<std::string>{
                          "wsl.exe", "-d", "levain-dev", "--exec", "/usr/bin/cmake", "--build",
                          "/home/dev/Levain/build/windows-debug", "--target", "levain_shaders"});
}

TEST_CASE("un dossier de build à espace reste un seul argument de wsl.exe")
{
    // « -- » le ferait passer par le shell de la distro, qui couperait cet espace : --exec non.
    levain::app::ShaderBuild build = buildIn("levain-dev");
    build.buildDir = "/home/dev/Mes Jeux/build/windows-debug";
    const auto command = levain::app::shaderReloadCommand(build, levain::app::HostSystem::Windows);
    REQUIRE(command.has_value());
    CHECK(command->at(3) == "--exec");
    CHECK(command->at(6) == "/home/dev/Mes Jeux/build/windows-debug");
}

TEST_CASE("un exe Windows sans distro refuse de recharger, et le dit")
{
    // L'exe de la CI : compilé hors d'une distro, il ne sait lancer ni cmake ni slangc.
    const auto command =
        levain::app::shaderReloadCommand(buildIn(""), levain::app::HostSystem::Windows);
    REQUIRE_FALSE(command.has_value());
    CHECK(command.error().code == levain::core::ErrorCode::Unsupported);
    CHECK(command.error().message.contains("WSL"));
}

TEST_CASE(
    "les messages UTF-16 de wsl.exe deviennent de l'UTF-8, la sortie d'un processus de la distro "
    "reste telle quelle")
{
    // « Il n'existe… » comme wsl.exe l'écrit pour une distro inconnue (relevé à l'octet près) : un
    // octet nul après chaque lettre, l'apostrophe en U+2019, le « é » en U+00E9.
    constexpr char WslMessage[] = "I\0l\0 \0n\0\x19 e\0x\0i\0s\0t\0e\0 \0\xE9\0\r\0\n\0";
    const std::string utf16{WslMessage, sizeof WslMessage - 1};
    CHECK(levain::app::utf8FromWslOutput(utf16) == "Il n\xE2\x80\x99"
                                                   "existe \xC3\xA9\r\n");
    // U+1F600 en paire de substitution (D83D DE00) : quatre octets en UTF-8 ; une moitié seule
    // devient U+FFFD plutôt que de produire de l'UTF-8 invalide.
    CHECK(levain::app::utf8FromWslOutput(std::string{"\x3D\xD8\x00\xDE", 4}) == "\xF0\x9F\x98\x80");
    CHECK(levain::app::utf8FromWslOutput(std::string{"a\0\x3D\xD8", 4}) == "a\xEF\xBF\xBD");
    // Un processus de la distro écrit de l'UTF-8, sans octet nul : la sortie de cmake ne change
    // pas.
    const std::string cmake = "FAILED: shaders/mesh.spv\nerror: unexpected token \xC3\xA9\n";
    CHECK(levain::app::utf8FromWslOutput(cmake) == cmake);
    CHECK(levain::app::utf8FromWslOutput("").empty());
}
