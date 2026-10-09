// Les fonctions pures du routage des rapports de la CRT. Les effets (arrêt, stderr, plus de
// fenêtre) demandent un processus qui échoue, pas un cas de doctest ; « un débogueur est attaché »
// se décide ici.
#include <array>
#include <ostream> // doctest affiche une string_view par operator<<, que la STL de Microsoft ne déclare pas seule
#include <string_view>

#include <crtdbg.h>
#include <doctest/doctest.h>

#include "levain/core/crt_report.hpp"

using namespace levain::core;

TEST_CASE(
    "crtReportActionFor : un avertissement passe, une erreur arrête, un débogueur reprend la main")
{
    CHECK(crtReportActionFor(_CRT_WARN, false) == CrtReportAction::Continue);
    CHECK(crtReportActionFor(_CRT_WARN, true) == CrtReportAction::Continue);
    CHECK(crtReportActionFor(_CRT_ERROR, false) == CrtReportAction::Stop);
    CHECK(crtReportActionFor(_CRT_ASSERT, false) == CrtReportAction::Stop);
    CHECK(crtReportActionFor(_CRT_ASSERT, true) == CrtReportAction::BreakIntoDebugger);
    CHECK(crtReportActionFor(_CRT_ERRCNT + 4, false) ==
          CrtReportAction::Stop); // un type inconnu n'est pas un avertissement
}

TEST_CASE("describeCrtReport et describeException : une ligne qui finit par un retour à la ligne, "
          "sans déborder")
{
    std::array<char, 128> roomy{};
    CHECK(
        describeCrtReport(_CRT_ASSERT, "vector(1939) : Assertion failed: out of range\n", roomy) ==
        "rapport de la CRT (assertion) : vector(1939) : Assertion failed: out of range\n");
    CHECK(describeCrtReport(_CRT_ERROR, "abort() has been called", roomy) ==
          "rapport de la CRT (erreur) : abort() has been called\n"); // le message d'abort() n'a pas
                                                                     // de « \n »

    std::array<char, 16> tight{};
    const std::string_view cut = describeCrtReport(_CRT_ERROR, "un message bien trop long", tight);
    CHECK(cut.size() == tight.size() - 1);
    CHECK(cut.back() == '\n');

    // Le plus petit tampon admis : le « \n » et le zéro final de snprintf, rien d'autre.
    std::array<char, 2> smallest{};
    CHECK(describeCrtReport(_CRT_ERROR, "abort() has been called", smallest) == "\n");

    const std::string_view line =
        describeException(0xC0000005, reinterpret_cast<const void*>(0x1234), roomy);
    CHECK(line.find("0xc0000005 (violation d'accès)") != std::string_view::npos);
    CHECK(line.back() == '\n');
    // Le code d'un `throw` du C++, l'échec le plus courant : nommé, pas « exception inconnue ».
    CHECK(describeException(0xE06D7363, nullptr, roomy).find("exception C++") !=
          std::string_view::npos);
}
