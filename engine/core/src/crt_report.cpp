#include "levain/core/crt_report.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <utility>

#include <crtdbg.h>
#include <windows.h>

#include "levain/core/assert.hpp"

// En Debug seulement : la Release reste à décider. Quel mécanisme répond à quelle fenêtre, et
// pourquoi aucun gestionnaire de SIGABRT : engine/core/README.md. Sous un débogueur, le filtre
// n'est jamais appelé (le débogueur reçoit l'exception) et le crochet répond « Retry ».

namespace levain::core
{

namespace
{

// Le code que lève un `throw` du C++ sous Windows (« msc » en ASCII, après 0xE0).
constexpr unsigned long CppExceptionCode = 0xE06D7363;

// Des littéraux : rien à allouer dans un plantage.
const char* exceptionName(unsigned long code)
{
    switch (code)
    {
    case EXCEPTION_ACCESS_VIOLATION:
        return "violation d'accès";
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        return "instruction illégale, le ud2 d'une vérification de la STL sous clang";
    case EXCEPTION_BREAKPOINT:
        return "point d'arrêt sans débogueur, comme celui de LEVAIN_ASSERT";
    case CppExceptionCode:
        return "exception C++ que personne n'a attrapée";
    default:
        return "exception inconnue";
    }
}

// Termine la ligne par « \n », même tronquée : `written` est ce que snprintf annonce, qui peut
// dépasser le tampon. Avec un seul octet, il n'y aurait de place que pour le zéro final.
std::string_view finishLine(std::span<char> buffer, int written)
{
    LEVAIN_ASSERT(buffer.size() >= 2, "un tampon de 2 octets au moins (crt_report.hpp)");
    const std::size_t length = std::min<std::size_t>(std::max(written, 0), buffer.size() - 1);
    const bool terminated = length > 0 && buffer[length - 1] == '\n';
    const std::size_t end = terminated ? length : std::min(length + 1, buffer.size() - 1);
    buffer[end - 1] = '\n';
    return {buffer.data(), end};
}

#ifdef _DEBUG

// WriteFile plutôt que fprintf : ni verrou de stdio ni tampon, donc utilisable depuis un plantage.
void writeReportLine(std::string_view line)
{
    const HANDLE standardError = GetStdHandle(STD_ERROR_HANDLE);
    DWORD written = 0;
    if (standardError != nullptr && standardError != INVALID_HANDLE_VALUE)
    {
        WriteFile(standardError, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    }
    // Aussi au débogueur (DebugView) : sans stderr, un programme GUI ne se tait pas tout à fait.
    // Copie sur la pile avec son zéro (une vue n'en a pas) : le tas est peut-être détruit.
    std::array<char, 1024> terminated{};
    std::ranges::copy(line.substr(0, terminated.size() - 1), terminated.begin());
    OutputDebugStringA(terminated.data());
}

// TerminateProcess ne laisse rien tourner après lui (ni destructeurs ni DLL), ce qu'on veut après
// une précondition violée ; _Exit si Windows le refuse.
[[noreturn]] void stopProcess(unsigned exitCode)
{
    TerminateProcess(GetCurrentProcess(), exitCode);
    std::_Exit(static_cast<int>(exitCode));
}

// Le filtre que posait la vcruntime avant le nôtre (.CRT$XCAA, exe_common.inl) : pour une exception
// C++, il appelle std::terminate ([except.handle]/9), donc le gestionnaire de std::set_terminate.
// Le remplacer sans le garder casserait le C++ standard.
LPTOP_LEVEL_EXCEPTION_FILTER previousFilter = nullptr;

// Sans ce filtre, un plantage ouvre la fenêtre de Windows Error Reporting. Il écrit la ligne, puis
// termine avec le code de l'exception, comme Windows avec SEM_NOGPFAULTERRORBOX, mais sans en
// dépendre : un pilote ou une DLL qui remettrait l'error mode à zéro rouvrirait la fenêtre.
LONG WINAPI onUnhandledException(EXCEPTION_POINTERS* pointers)
{
    const EXCEPTION_RECORD& record = *pointers->ExceptionRecord;
    if (record.ExceptionCode == CppExceptionCode && previousFilter != nullptr)
    {
        // std::terminate, puis abort(), dont le rapport passe par le crochet (code 3). Le filtre ne
        // rend la main que pour une exception qui n'est pas celle de ce runtime : on finit ici.
        static_cast<void>(previousFilter(pointers));
    }
    std::array<char, 512> buffer{};
    writeReportLine(describeException(record.ExceptionCode, record.ExceptionAddress, buffer));
    stopProcess(record.ExceptionCode);
}

// La CRT appelle les crochets étroits d'abord, même pour un rapport large (`_ASSERT`) : mesuré, le
// crochet large n'a rien à faire. Le message n'est en UTF-8 (cmake/windows/utf8.manifest) que pour
// un rapport étroit, celui de la STL : la CRT convertit un rapport large (`_ASSERT`, paramètre
// invalide) dans la locale C (wcstombs_s, ucrt/misc/dbgrptt.cpp), donc en Latin-1, et remplace le
// message entier par DBGRPT_INVALIDMSG dès un caractère au-delà de U+00FF.
int __cdecl onReport(int reportType, char* message, int* returnValue)
{
    const CrtReportAction action = crtReportActionFor(reportType, IsDebuggerPresent() != 0);
    if (action == CrtReportAction::Continue)
    {
        return FALSE; // la CRT fait comme avant
    }
    std::array<char, 1024> buffer{};
    const std::string_view line =
        describeCrtReport(reportType, message != nullptr ? message : "", buffer);
    if (action == CrtReportAction::BreakIntoDebugger)
    {
        writeReportLine(line);
        // 1 est le « Retry » de l'ancienne fenêtre : la macro appelante (_ASSERT, _RPTF, ou le code
        // de la CRT) exécute elle-même __debugbreak, et le débogueur s'arrête sur la ligne fautive,
        // pas ici.
        if (returnValue != nullptr)
        {
            *returnValue = 1;
        }
        return TRUE;
    }
    // Vider stdout avant la ligne : un journal qui fusionne les deux flux les garde dans l'ordre.
    // Risque accepté : fflush prend les verrous de stdio pendant que la CRT tient le sien autour
    // des crochets (__acrt_debug_lock) ; un thread qui tiendrait un FILE en attendant de rapporter
    // bloquerait les deux, et le délai du test le montrerait au lieu d'une fenêtre.
    std::fflush(nullptr);
    writeReportLine(line);
    stopProcess(CrtReportExitCode);
}

#endif // _DEBUG

} // namespace

CrtReportAction crtReportActionFor(int reportType, bool debuggerPresent)
{
    if (reportType == _CRT_WARN)
    {
        return CrtReportAction::Continue;
    }
    return debuggerPresent ? CrtReportAction::BreakIntoDebugger : CrtReportAction::Stop;
}

std::string_view describeCrtReport(int reportType, std::string_view message, std::span<char> buffer)
{
    const char* kind = reportType == _CRT_ASSERT ? "assertion" : "erreur";
    const int written = std::snprintf(buffer.data(), buffer.size(), "rapport de la CRT (%s) : %.*s",
                                      kind, static_cast<int>(message.size()), message.data());
    return finishLine(buffer, written);
}

std::string_view describeException(unsigned long code, const void* address, std::span<char> buffer)
{
    const int written = std::snprintf(buffer.data(), buffer.size(),
                                      "exception non gérée 0x%08lx (%s) à l'adresse %p\n", code,
                                      exceptionName(code), address);
    return finishLine(buffer, written);
}

void routeCrtReportsToStderr() noexcept
{
#ifdef _DEBUG
    static bool routed = false;
    if (std::exchange(routed, true))
    {
        return;
    }
    // Les fenêtres d'erreur critique et de Windows Error Reporting.
    SetErrorMode(GetErrorMode() | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    // Le message d'un assert() du C et de la CRT (R6xxx) va sur stderr, même pour un programme sans
    // console, où il ouvrirait une boîte (ucrt/startup/assert.cpp).
    _set_error_mode(_OUT_TO_STDERR);
    previousFilter = SetUnhandledExceptionFilter(&onUnhandledException);
    // Si quelqu'un retirait le crochet, aucune fenêtre ne reviendrait : le mode FILE vers stderr
    // remplace celle des erreurs et des assertions. Les avertissements restent dans le débogueur.
    for (const int type : {_CRT_ERROR, _CRT_ASSERT})
    {
        _CrtSetReportMode(type, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
        _CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
    }
    // -1 : EINVAL ou ENOMEM. Sans crochet, un rapport n'arrêterait plus le programme, en silence
    // (règle n°7) : on s'arrête avant `main`, en le disant.
    if (_CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, &onReport) == -1)
    {
        writeReportLine("routage des rapports de la CRT impossible : _CrtSetReportHook2 a "
                        "échoué (engine/core/src/crt_report.cpp)\n");
        stopProcess(CrtReportExitCode);
    }
#endif
}

} // namespace levain::core

// L'ancre. Une bibliothèque statique ne livre que les fichiers objets dont l'éditeur de liens a
// besoin : rien ne référence celui-ci, qui serait écarté avec le routage, sans un mot. `/INCLUDE:`
// (CMakeLists racine et engine/core/CMakeLists.txt) force la référence à ce symbole dans tout
// exécutable, donc l'initialisation dynamique, avant `main`. init_seg(lib) : avant celles des
// autres fichiers, pour qu'une assertion d'un global soit déjà routée. En Release, elle reste.
#pragma init_seg(lib)
extern "C" const bool LevainCrtReportRouted = (levain::core::routeCrtReportsToStderr(), true);
