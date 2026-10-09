#pragma once

// Windows seulement : la CRT de Microsoft et ses fenêtres n'existent nulle part ailleurs. Un
// include depuis un autre système est une erreur, pas un fichier vide (règle n°7).
#ifndef _WIN32
#error "crt_report.hpp est réservé à Windows (engine/core/README.md)."
#endif

#include <cstdint>
#include <span>
#include <string_view>

namespace levain::core
{

/// Le code de sortie d'un arrêt par un rapport de la CRT : celui d'`abort()`, pour que l'arrêt soit
/// le même qu'il vienne de `std::abort`, de la STL ou d'un `_ASSERT`.
inline constexpr unsigned CrtReportExitCode = 3;

/// Ce que le crochet de la CRT fait d'un rapport.
enum class CrtReportAction : std::uint8_t
{
    Continue,          ///< Un avertissement (`_CRT_WARN`) : la CRT le traite comme avant.
    BreakIntoDebugger, ///< Un débogueur est attaché : le « Retry » de l'ancienne fenêtre.
    Stop,              ///< Sinon : le message va sur stderr, le programme s'arrête.
};

/// `reportType` est `_CRT_WARN`, `_CRT_ERROR` ou `_CRT_ASSERT` (`crtdbg.h`). Un type inconnu arrête
/// le programme : mieux vaut un arrêt de trop qu'un rapport avalé (règle n°7).
[[nodiscard]] CrtReportAction crtReportActionFor(int reportType, bool debuggerPresent);

/// La ligne de stderr pour un rapport : « rapport de la CRT (assertion) : <message> », terminée par
/// « \n », écrite dans `buffer` sans allouer : le tas est peut-être détruit. `buffer` a 2 octets au
/// moins, la place du « \n » et du zéro final de snprintf. Le message de la CRT contient déjà
/// « fichier(ligne) ».
[[nodiscard]] std::string_view describeCrtReport(int reportType, std::string_view message,
                                                 std::span<char> buffer);

/// La ligne de stderr pour une exception que personne n'a gérée : code, nom et adresse. Sans
/// allouer non plus, même précondition sur `buffer`.
[[nodiscard]] std::string_view describeException(unsigned long code, const void* address,
                                                 std::span<char> buffer);

/// En Debug, plus de fenêtre de la CRT ni d'une exception Windows quand le programme échoue : les
/// rapports de la CRT et les plantages vont sur stderr et arrêtent le programme, sauf sous un
/// débogueur, qui reprend la main. En Release, ne fait rien : la Release reste à décider
/// (engine/core/README.md). Personne n'a à l'appeler : l'ancre de crt_report.cpp le fait avant
/// `main`, dans tout exécutable qui lie `levain::core`. Idempotente.
void routeCrtReportsToStderr() noexcept;

} // namespace levain::core
