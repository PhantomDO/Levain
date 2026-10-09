#include "levain/core/crt_report.hpp"

#include <algorithm>
#include <array>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <utility>

#include <crtdbg.h>
#include <intrin.h>
#include <windows.h>

#include "levain/core/assert.hpp"

// Debug : la ligne sur stderr, puis le programme s'arrête lui-même. Release : la même ligne, puis
// Windows Error Reporting (WER) reçoit l'échec, comme pour un programme sans Levain (ADR-0035,
// docs/QA.md) ; `failureEndingFor` en décide. Quel mécanisme répond à quelle fenêtre, et ce que
// chaque configuration en fait : engine/core/README.md. Sous un débogueur, le filtre n'est jamais
// appelé (le débogueur reçoit l'exception) et le crochet répond « Retry ».

namespace levain::core
{

namespace
{

// Le code que lève un `throw` du C++ sous Windows (« msc » en ASCII, après 0xE0).
constexpr unsigned long CppExceptionCode = 0xE06D7363;

// Seule lecture de la configuration : la décision passe par `failureEndingFor`, que le test garde.
#ifdef _DEBUG
constexpr bool IsDebugBuild = true;
#else
constexpr bool IsDebugBuild = false;
#endif

// Le filtre rend la main à `UnhandledExceptionFilter`, qui appelle WER : le « Pass the exception to
// the OS » de Godot (platform/windows/crash_handler_windows_seh.cpp). Un nom, parce que la valeur
// ne dit pas qu'elle mène à WER.
constexpr LONG PassToWindowsErrorReporting = EXCEPTION_CONTINUE_SEARCH;

// Les lignes des gestionnaires de la CRT : des littéraux, rien à allouer dans un plantage.
constexpr std::string_view AbortSignalLine =
    "abort() ou SIGABRT : arrêt du programme (std::terminate et un appel virtuel pur y mènent "
    "aussi)\n";
constexpr std::string_view InvalidParameterLine =
    "paramètre invalide passé à une fonction de la CRT : arrêt du programme (celle de Release ne "
    "dit pas laquelle)\n";

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

// Ce que fait abort() de la CRT une fois son gestionnaire de SIGABRT passé (ucrt/startup/abort.cpp)
// : un __fastfail, que WER reçoit (0xC0000409), si _CALL_REPORTFAULT est posé, sinon _exit(3). Le
// drapeau est posé par défaut en Release, absent en Debug, et doctest le retire pendant un cas : le
// gestionnaire le lit au lieu de le supposer. `(0, 0)` lit sans rien changer.
[[noreturn]] void endLikeAbort()
{
    if ((_set_abort_behavior(0, 0) & _CALL_REPORTFAULT) != 0)
    {
        __fastfail(FAST_FAIL_FATAL_APP_EXIT);
    }
    stopProcess(CrtReportExitCode);
}

// Le filtre que posait la vcruntime avant le nôtre (.CRT$XCAA, exe_common.inl) : pour une exception
// C++, il appelle std::terminate ([except.handle]/9), donc le gestionnaire de std::set_terminate ;
// pour le reste, il rend EXCEPTION_CONTINUE_SEARCH. Le remplacer sans le garder casserait le C++
// standard, et une DLL qui en aurait posé un avant nous perdrait la main.
LPTOP_LEVEL_EXCEPTION_FILTER previousFilter = nullptr;

LONG chainToPreviousFilter(EXCEPTION_POINTERS* pointers)
{
    return previousFilter != nullptr ? previousFilter(pointers) : PassToWindowsErrorReporting;
}

// Sans ce filtre, un plantage ouvre la fenêtre de Windows Error Reporting en Debug, et ne dit rien
// sur stderr en Release. Il écrit la ligne, puis : en Debug, termine avec le code de l'exception,
// comme Windows avec SEM_NOGPFAULTERRORBOX mais sans en dépendre (un pilote ou une DLL qui
// remettrait l'error mode à zéro rouvrirait la fenêtre) ; en Release, rend la main à WER, qui garde
// son rapport et ses dumps, et le code de l'exception reste celui de Windows.
LONG WINAPI onUnhandledException(EXCEPTION_POINTERS* pointers)
{
    const EXCEPTION_RECORD& record = *pointers->ExceptionRecord;
    const FailureEnding ending = failureEndingFor(IsDebugBuild);
    if (record.ExceptionCode == CppExceptionCode)
    {
        // std::terminate, puis abort(), dont le gestionnaire de SIGABRT finit : le filtre ne rend
        // la main que pour une exception qui n'est pas celle de ce runtime. La ligne de l'exception
        // serait fausse ici, c'est un abort().
        const LONG verdict = chainToPreviousFilter(pointers);
        if (ending == FailureEnding::HandToWindowsErrorReporting)
        {
            return verdict;
        }
    }
    std::array<char, 512> buffer{};
    writeReportLine(describeException(record.ExceptionCode, record.ExceptionAddress, buffer));
    if (ending == FailureEnding::StopProcess)
    {
        stopProcess(record.ExceptionCode);
    }
    return chainToPreviousFilter(pointers);
}

// abort() lève SIGABRT, et std::terminate et un appel virtuel pur passent par abort(). En Release,
// la CRT finit alors par __fastfail, qu'aucun filtre ne voit : sans ce gestionnaire, aucune ligne.
// Il ne rend jamais la main : un `raise(SIGABRT)` direct continuerait sinon. Il ne sert qu'une
// fois, la CRT le remettant à SIG_DFL avant de l'appeler (ucrt/misc/signal.cpp) : un second abort()
// simultané, d'un autre thread, finit sans ligne.
void onAbortSignal(int /*signal*/)
{
    writeReportLine(AbortSignalLine);
    if (failureEndingFor(IsDebugBuild) == FailureEnding::StopProcess)
    {
        stopProcess(CrtReportExitCode);
    }
    endLikeAbort();
}

// Une CRT de Release donne des arguments nuls ici ; celle de Debug a déjà arrêté le programme par
// son crochet (l'assertion précède). Sans gestionnaire, la CRT de Release fait
// __fastfail(FAST_FAIL_INVALID_ARG), sans regarder _CALL_REPORTFAULT (_invoke_watson,
// ucrt/misc/invalid_parameter.cpp) : le même, après la ligne.
void onInvalidParameter(const wchar_t* /*expression*/, const wchar_t* /*function*/,
                        const wchar_t* /*file*/, unsigned int /*line*/, std::uintptr_t /*reserved*/)
{
    writeReportLine(InvalidParameterLine);
    if (failureEndingFor(IsDebugBuild) == FailureEnding::StopProcess)
    {
        stopProcess(CrtReportExitCode);
    }
    __fastfail(FAST_FAIL_INVALID_ARG);
}

#ifdef _DEBUG

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

// Les rapports de la CRT de Debug n'existent pas en Release : `_CrtSetReportHook2` n'y fait rien.
void routeDebugCrtReports()
{
    // La fenêtre de Windows Error Reporting : le Debug ne la veut pas (la Release laisse WER
    // faire).
    SetErrorMode(GetErrorMode() | SEM_NOGPFAULTERRORBOX);
    // Le message d'un assert() du C et de la CRT (R6xxx) va sur stderr, même pour un programme sans
    // console, où il ouvrirait une boîte (ucrt/startup/assert.cpp).
    _set_error_mode(_OUT_TO_STDERR);
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
}

#endif // _DEBUG

// Les deux configurations : la ligne de chaque échec, puis la fin que `failureEndingFor` donne.
void routeFailuresToStderr()
{
    // Microsoft : « all applications call SetErrorMode(SEM_FAILCRITICALERRORS) at startup »
    // (SetErrorMode), pour qu'une erreur matérielle (« disque absent ») ne bloque pas le programme
    // sur une fenêtre. SEM_NOGPFAULTERRORBOX, lui, couperait WER : le Debug seul le pose.
    SetErrorMode(GetErrorMode() | SEM_FAILCRITICALERRORS);
    previousFilter = SetUnhandledExceptionFilter(&onUnhandledException);
    // SIG_ERR : arrêt avant `main`, en le disant. Sans gestionnaire, abort() finirait sans ligne,
    // en silence (règle n°7).
    if (std::signal(SIGABRT, &onAbortSignal) == SIG_ERR)
    {
        writeReportLine(
            "routage de SIGABRT impossible : signal a échoué (engine/core/src/crt_report.cpp)\n");
        stopProcess(CrtReportExitCode);
    }
    _set_invalid_parameter_handler(&onInvalidParameter);
}

} // namespace

CrtReportAction crtReportActionFor(int reportType, bool debuggerPresent)
{
    if (reportType == _CRT_WARN)
    {
        return CrtReportAction::Continue;
    }
    return debuggerPresent ? CrtReportAction::BreakIntoDebugger : CrtReportAction::Stop;
}

FailureEnding failureEndingFor(bool debugBuild)
{
    return debugBuild ? FailureEnding::StopProcess : FailureEnding::HandToWindowsErrorReporting;
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
    static bool routed = false;
    if (std::exchange(routed, true))
    {
        return;
    }
    routeFailuresToStderr();
#ifdef _DEBUG
    routeDebugCrtReports();
#endif
}

} // namespace levain::core

// L'ancre. Une bibliothèque statique ne livre que les fichiers objets dont l'éditeur de liens a
// besoin : rien ne référence celui-ci, qui serait écarté avec le routage, sans un mot. `/INCLUDE:`
// (CMakeLists racine et engine/core/CMakeLists.txt) force la référence à ce symbole dans tout
// exécutable, donc l'initialisation dynamique, avant `main`. init_seg(lib) : avant celles des
// autres fichiers, pour qu'une assertion d'un global soit déjà routée. Elle reste en Release, où
// elle installe la ligne et la main rendue à WER.
#pragma init_seg(lib)
extern "C" const bool LevainCrtReportRouted = (levain::core::routeCrtReportsToStderr(), true);
