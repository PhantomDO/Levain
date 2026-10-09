// Le contre-test de engine/core/src/crt_report.cpp (règle n°7) : chaque scénario fait échouer le
// programme de l'une des manières qui ouvraient une fenêtre sous Windows, et le programme juge
// qu'il s'est arrêté vite, avec le code de sortie exact, en disant pourquoi sur stderr. En Release,
// il juge de plus que Windows Error Reporting (WER) a reçu l'échec, ce que le code de sortie et
// stderr ne montrent pas : un filtre qui terminerait lui-même le processus avec le même code les
// laisserait verts. WER écrit alors l'événement 1000 du journal Application, que le pilote cherche
// (werEventExists).
//
//   levain_crt_report_probe <scénario>             le pilote : lance l'enfant ci-dessous et juge
//   levain_crt_report_probe --debugger <scénario>  le pilote, débogueur de l'enfant
//   levain_crt_report_probe --child <scénario>     l'enfant : déclenche l'échec
//
// Le pilote est ce programme, et non ctest : PASS_REGULAR_EXPRESSION fait ignorer le code de
// sortie, WILL_FAIL inverse le verdict de l'expression, et sur un dépassement de délai ctest ne tue
// pas la fenêtre de l'enfant. Ici l'enfant est tué à sa limite. Le probe ne lie que
// core et n'appelle rien du routage : s'il passe, tout programme qui lie core l'a, sans l'avoir
// demandé.
#include <algorithm>
#include <array>
#include <cassert>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <format>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <crtdbg.h>
#include <intrin.h>
#include <io.h>
#include <windows.h>
#include <winevt.h>

#include "levain/core/assert.hpp"

namespace
{

// Un enfant que WER ne traite pas finit en moins d'une demi-seconde (Debug : 297 ms pour
// `fastfail`) : à 10 s, une fenêtre ou un blocage l'attend sans doute.
constexpr DWORD ChildTimeoutMilliseconds = 10'000;
// Le piège : WER écrit son événement au début (0,12 à 1,07 s après la création de l'enfant) puis
// garde l'enfant en vie le temps de son minidump : 3 s au repos, 34 s au plus vu (build/GOTCHA.md).
// Une limite fixe accuse une fenêtre absente ou laisse passer un vrai blocage ; l'événement, lui,
// dit à 10 s si l'échec est entre les mains de WER, qu'on attend alors jusqu'à ce plafond.
constexpr DWORD WerHandlingCapMilliseconds = 120'000;
// Après la fin de l'enfant, le temps laissé à l'événement de WER, relu toutes les 100 ms.
constexpr DWORD WerEventTimeoutMilliseconds = 10'000;
constexpr DWORD WerEventPollMilliseconds = 100;
constexpr int UsageError = 64;

#ifdef _DEBUG
constexpr bool IsDebugBuild = true;
#else
constexpr bool IsDebugBuild = false;
#endif

// Le code d'abort() et du SIGABRT par défaut, que reprend l'arrêt par un rapport de la CRT : écrit
// ici plutôt que lu dans core, pour que le probe ne juge pas core avec sa propre constante.
constexpr DWORD AbortExitCode = 3;

// Le code de STATUS_STACK_BUFFER_OVERRUN, celui d'un __fastfail : la fin de abort() en Release,
// comme celle d'un paramètre invalide (la CRT les termine ainsi, WER reçoit le même rapport avec ou
// sans core).
constexpr DWORD FastFailExitCode = 0xC0000409;

// Où finit un échec que la CRT de Debug arrête et que celle de Release termine par __fastfail :
// abort(), SIGABRT, un paramètre invalide, std::terminate, un appel virtuel pur.
constexpr DWORD CrtFailureExitCode = IsDebugBuild ? AbortExitCode : FastFailExitCode;

// Ce que core écrit pour SIGABRT : le début de sa ligne, commune au Debug (raise) et à la Release.
constexpr std::string_view AbortSignalLine = "abort() ou SIGABRT";

struct Scenario
{
    std::string_view name;
    void (*trigger)();
    std::string_view expectedOnStderr;      ///< Vide : seul le code de sortie juge.
    DWORD expectedExitCode = AbortExitCode; ///< Exact ; 0 : le scénario revient de `trigger`.
    bool werReports = false; ///< Release : WER reçoit l'échec, l'événement 1000 le dit.
};

// Faut-il renoncer à un enfant encore en vie `elapsed` ms après sa création, sachant si WER a déjà
// l'échec ? Le piège de WerHandlingCapMilliseconds : 10 s sans l'événement, le plafond avec.
constexpr bool giveUpOnChild(DWORD elapsed, bool werHasFailure)
{
    return elapsed >= (werHasFailure ? WerHandlingCapMilliseconds : ChildTimeoutMilliseconds);
}

static_assert(!giveUpOnChild(ChildTimeoutMilliseconds - 1, false));
static_assert(giveUpOnChild(ChildTimeoutMilliseconds, false));
static_assert(!giveUpOnChild(WerHandlingCapMilliseconds - 1, true));
static_assert(giveUpOnChild(WerHandlingCapMilliseconds, true));

// Ce que l'enfant reçoit du routage, lancé sans le mode d'erreur du pilote (startChild).
void printErrorMode()
{
    const bool failCriticalErrors = (GetErrorMode() & SEM_FAILCRITICALERRORS) != 0;
    const bool noFaultBox = (GetErrorMode() & SEM_NOGPFAULTERRORBOX) != 0;
    const bool toStderr = _set_error_mode(_REPORT_ERRMODE) == _OUT_TO_STDERR;
    std::fprintf(stderr, "SEM_FAILCRITICALERRORS %s, SEM_NOGPFAULTERRORBOX %s, _OUT_TO_STDERR %s\n",
                 failCriticalErrors ? "posé" : "absent", noFaultBox ? "posé" : "absent",
                 toStderr ? "posé" : "absent");
}

// Un appel virtuel pur : le constructeur de la base appelle, par un pointeur volatile, un virtuel
// pur que la dérivée n'a pas encore. Le volatile empêche l'optimiseur de résoudre l'appel à la
// compilation (un virtuel sans corps) ; mesuré, clang-cl 23 en Release passe par _purecall même
// sans lui (build/GOTCHA.md).
struct PureCallBase
{
    PureCallBase()
    {
        PureCallBase* volatile self = this;
        self->pure();
    }

    PureCallBase(const PureCallBase&) = delete;
    PureCallBase& operator=(const PureCallBase&) = delete;
    virtual ~PureCallBase() = default;
    virtual void pure() = 0;
};

struct PureCallDerived : PureCallBase
{
    void pure() override {}
};

// Ce que laisse abort() : en Debug le rapport de la CRT, que le crochet de core préfixe (sans lui,
// le mode FILE écrit le même message et abort() finit tout de même par _exit(3) ; seul le préfixe
// dit qui a arrêté le programme), en Release la ligne de SIGABRT.
constexpr std::string_view AbortedOnStderr =
    IsDebugBuild ? "rapport de la CRT (erreur) : abort() has been called" : AbortSignalLine;

// Un scénario par façon connue d'échouer. Le Debug en a trois de plus (la STL et les assertions de
// la CRT, qui n'existent pas en Release) et s'arrête lui-même, sans WER. La Release rend la main à
// WER (engine/core/README.md) : `werReports` le fait vérifier par l'événement 1000.
constexpr Scenario Scenarios[] = {
#ifdef _DEBUG
    // Le rapport qu'a vu Donnovan, depuis levain_sandbox.exe.
    {"stl-subscript",
     []
     {
         std::vector<int> values(2);
         const volatile int ignored = values[5];
         (void)ignored;
     },
     "vector subscript out of range"},
    // Le piège : en mode FILE, `_CrtDbgReport` rend la main et `_ASSERT` continue.
    {"crt-assert", [] { _ASSERT(false); }, "Assertion failed"},
    // L'assert() du C : celui d'IM_ASSERT, de glm et des ports en Debug.
    {"c-assert", [] { assert(false); }, "Assertion failed: false"},
#endif
    {"abort", [] { std::abort(); }, AbortedOnStderr, CrtFailureExitCode, !IsDebugBuild},
    // [except.handle]/9 : std::terminate, donc le gestionnaire de std::set_terminate, puis abort().
    // Les deux lignes se suivent (stderr n'a pas de tampon) : la seconde est celle de core, qui
    // manque si l'ancre est retirée ou le crochet rendu à la CRT, alors que le code de sortie reste
    // celui d'abort(). Le premier retour à la ligne est « \r\n » : le mode texte de la CRT traduit
    // celui du `fputs` ; core écrit le sien par WriteFile, sans traduction.
    {"uncaught-throw",
     []
     {
         std::set_terminate(
             []
             {
                 std::fputs("std::terminate appelé\n", stderr);
                 std::abort();
             });
         throw std::runtime_error("personne ne l'attrape");
     },
     IsDebugBuild ? "std::terminate appelé\r\nrapport de la CRT (erreur)"
                  : "std::terminate appelé\r\nabort() ou SIGABRT",
     CrtFailureExitCode, !IsDebugBuild},
    // Sans abort() : un gestionnaire de SIGABRT qui rendrait la main laisserait le programme
    // continuer, et finir en 0.
    {"raise-sigabrt", [] { std::raise(SIGABRT); }, AbortSignalLine, CrtFailureExitCode,
     !IsDebugBuild},
    // Le paramètre invalide de la CRT. En Debug, son assertion précède et arrête le programme (code
    // 3) ; en Release, ses arguments sont nuls et sans gestionnaire il finit en __fastfail, muet.
    {"invalid-parameter", [] { static_cast<void>(_close(-1)); },
     IsDebugBuild ? "rapport de la CRT (assertion)" : "paramètre invalide", CrtFailureExitCode,
     !IsDebugBuild},
    // _purecall appelle abort() (vcruntime/purevirt.cpp).
    {"pure-call", [] { PureCallDerived derived; }, AbortedOnStderr, CrtFailureExitCode,
     !IsDebugBuild},
    {"null-write", [] { *static_cast<volatile int*>(nullptr) = 1; }, "0xc0000005",
     EXCEPTION_ACCESS_VIOLATION, !IsDebugBuild},
    // Le point d'arrêt de LEVAIN_ASSERT, sans débogueur.
    {"breakpoint", [] { LEVAIN_DEBUG_BREAK(); }, "0x80000003", EXCEPTION_BREAKPOINT, !IsDebugBuild},
    // Un constat, pas un contre-test : rien n'intercepte un __fastfail, qui finit par
    // STATUS_STACK_BUFFER_OVERRUN. En Release c'est le témoin du contrôle de WER : un événement
    // 1000 qu'aucun code de Levain ne peut avoir empêché.
    {"fastfail", [] { __fastfail(7); }, "", FastFailExitCode, !IsDebugBuild},
#ifndef _DEBUG
    // abort() sans _CALL_REPORTFAULT, comme sous doctest : la CRT finit par _exit(3), pas par
    // __fastfail, et le gestionnaire de core fait de même. WER n'est pas appelé.
    {"abort-without-reportfault",
     []
     {
         _set_abort_behavior(0, _CALL_REPORTFAULT);
         std::abort();
     },
     AbortSignalLine, AbortExitCode},
#endif
    {"error-mode", &printErrorMode,
     IsDebugBuild
         ? "SEM_FAILCRITICALERRORS posé, SEM_NOGPFAULTERRORBOX posé, _OUT_TO_STDERR posé"
         : "SEM_FAILCRITICALERRORS posé, SEM_NOGPFAULTERRORBOX absent, _OUT_TO_STDERR absent",
     0},
};

struct Child
{
    PROCESS_INFORMATION process{};
    HANDLE errorRead = nullptr;
    DWORD start = 0;
};

struct ChildResult
{
    // Le PID et la date de création (FILETIME) identifient le processus dans l'événement de WER.
    DWORD processId = 0;
    ULONGLONG creationTime = 0;
    DWORD exitCode = 0;
    DWORD milliseconds = 0;
    bool timedOut = false;
    bool werTooSlow = false; ///< Avec timedOut : WER avait l'échec, le plafond est atteint.
    DWORD exceptionCode = 0; ///< Sous débogueur : la première exception après celle du chargeur.
    std::string standardError;
};

// L'enfant n'a pas de console (CREATE_NO_WINDOW), sa sortie standard va dans le vide et son erreur
// standard dans un tube que ses quelques lignes ne remplissent pas : un programme lancé par un
// outil, comme la CI. CREATE_DEFAULT_ERROR_MODE : il ne reçoit pas le mode d'erreur du pilote, qui
// lie core lui aussi ; seul son propre routage peut le poser.
Child startChild(std::wstring commandLine, DWORD extraFlags)
{
    SECURITY_ATTRIBUTES inheritable{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    Child child;
    HANDLE errorWrite = nullptr;
    CreatePipe(&child.errorRead, &errorWrite, &inheritable, 1U << 16U);
    SetHandleInformation(child.errorRead, HANDLE_FLAG_INHERIT, 0);
    HANDLE const nothing =
        CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    &inheritable, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = startup.hStdOutput = nothing;
    startup.hStdError = errorWrite;
    child.start = GetTickCount();
    if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW | CREATE_DEFAULT_ERROR_MODE | extraFlags, nullptr, nullptr,
                        &startup, &child.process))
    {
        std::fprintf(stderr, "CreateProcess a échoué : %lu\n", GetLastError());
        std::exit(1);
    }
    CloseHandle(errorWrite);
    CloseHandle(nothing);
    return child;
}

// Lu dès le départ de l'enfant, pas à sa fin : l'événement de WER se cherche à 10 s, enfant en vie.
void recordIdentity(const Child& child, ChildResult& result)
{
    result.processId = child.process.dwProcessId;
    FILETIME creation{};
    FILETIME unused{};
    GetProcessTimes(child.process.hProcess, &creation, &unused, &unused, &unused);
    result.creationTime =
        (static_cast<ULONGLONG>(creation.dwHighDateTime) << 32U) | creation.dwLowDateTime;
}

void finishChild(const Child& child, ChildResult& result)
{
    result.milliseconds = GetTickCount() - child.start;
    GetExitCodeProcess(child.process.hProcess, &result.exitCode);
    std::array<char, 4096> text;
    DWORD available = 0;
    DWORD read = 0;
    // Peek d'abord : un tube vide dont un autre processus tiendrait l'extrémité d'écriture
    // bloquerait ReadFile.
    if (PeekNamedPipe(child.errorRead, nullptr, 0, nullptr, &available, nullptr) && available > 0 &&
        ReadFile(child.errorRead, text.data(), static_cast<DWORD>(text.size()), &read, nullptr))
    {
        result.standardError.assign(text.data(), read);
    }
    CloseHandle(child.process.hProcess);
    CloseHandle(child.process.hThread);
    CloseHandle(child.errorRead);
}

// Les événements de WER : « Application Error » (1000) est écrit par WerFault.exe quand l'échec lui
// arrive, et pas quand un processus se termine lui-même, avec le même code (TerminateProcess).
using EventHandle = std::unique_ptr<void, decltype(&EvtClose)>;

// Un XPath sur la structure de l'événement, ni sur son texte localisé (le portable est en français)
// ni sur la position d'une donnée : `ProcessId` et `ProcessCreationTime` sont des données nommées,
// écrites en hexadécimal minuscule avec « 0x » (`0x81d4`), le format de `{:#x}`. Le couple
// identifie le processus : un PID réutilisé n'est pas confondu.
bool werEventExists(const ChildResult& child)
{
    const std::wstring query =
        std::format(L"*[System[Provider[@Name='Application Error'] and EventID=1000]] and "
                    L"*[EventData[Data[@Name='ProcessId']='{:#x}' and "
                    L"Data[@Name='ProcessCreationTime']='{:#x}']]",
                    child.processId, child.creationTime);
    // Les drapeaux de winevt.h sont des enums signés : en DWORD, le OU n'a plus d'opérande signé
    // (clang-tidy).
    constexpr DWORD QueryFlags =
        static_cast<DWORD>(EvtQueryChannelPath) | static_cast<DWORD>(EvtQueryReverseDirection);
    const EventHandle results{EvtQuery(nullptr, L"Application", query.c_str(), QueryFlags),
                              &EvtClose};
    if (!results)
    {
        throw std::runtime_error(std::format(
            "le journal Application est illisible (EvtQuery : erreur {})", GetLastError()));
    }
    EVT_HANDLE event = nullptr;
    DWORD returned = 0;
    if (EvtNext(results.get(), 1, &event, 0, 0, &returned) != FALSE)
    {
        EvtClose(event);
        return true;
    }
    if (GetLastError() != ERROR_NO_MORE_ITEMS)
    {
        throw std::runtime_error(std::format(
            "le journal Application est illisible (EvtNext : erreur {})", GetLastError()));
    }
    return false;
}

// Le délai après lequel l'événement est vu, ou rien au bout de WerEventTimeoutMilliseconds : WER
// l'écrit avant que l'enfant ne finisse (mesuré, build/GOTCHA.md), l'attente n'est qu'une marge.
std::optional<DWORD> waitForWerEvent(const ChildResult& child)
{
    const DWORD start = GetTickCount();
    do
    {
        if (werEventExists(child))
        {
            return GetTickCount() - start;
        }
        Sleep(WerEventPollMilliseconds);
    } while (GetTickCount() - start < WerEventTimeoutMilliseconds);
    return std::nullopt;
}

ChildResult runChild(const std::wstring& commandLine, bool werReports)
{
    const Child child = startChild(commandLine, 0);
    ChildResult result;
    recordIdentity(child, result);
    bool werHasFailure = false;
    try
    {
        while (WaitForSingleObject(child.process.hProcess, WerEventPollMilliseconds) ==
               WAIT_TIMEOUT)
        {
            const DWORD elapsed = GetTickCount() - child.start;
            // Le journal ne se lit qu'après la limite ordinaire, et plus une fois l'événement vu.
            werHasFailure = werHasFailure || (werReports && elapsed >= ChildTimeoutMilliseconds &&
                                              werEventExists(result));
            if (giveUpOnChild(elapsed, werHasFailure))
            {
                result.timedOut = true;
                result.werTooSlow = werHasFailure;
                break;
            }
        }
    }
    catch (...)
    {
        TerminateProcess(child.process.hProcess, 1); // un journal illisible ne laisse pas l'enfant
        throw;
    }
    if (result.timedOut)
    {
        TerminateProcess(child.process.hProcess, 1); // ferme aussi la fenêtre qu'il attendait
        WaitForSingleObject(child.process.hProcess, 5000);
    }
    finishChild(child, result);
    return result;
}

// Le pilote en débogueur de l'enfant : IsDebuggerPresent y est vrai, et le routage doit rendre la
// main au débogueur par un point d'arrêt (le « Retry » du crochet), pas arrêter le programme. Le
// premier point d'arrêt est celui du chargeur de Windows ; l'enfant est tué à l'exception suivante.
ChildResult debugChild(const std::wstring& commandLine)
{
    const Child child = startChild(commandLine, DEBUG_ONLY_THIS_PROCESS);
    ChildResult result;
    bool loaderBreakpointSeen = false;
    DEBUG_EVENT event{};
    while (WaitForDebugEvent(&event, ChildTimeoutMilliseconds))
    {
        const DWORD kind = event.dwDebugEventCode;
        if (kind == CREATE_PROCESS_DEBUG_EVENT || kind == LOAD_DLL_DEBUG_EVENT)
        {
            CloseHandle(kind == LOAD_DLL_DEBUG_EVENT ? event.u.LoadDll.hFile
                                                     : event.u.CreateProcessInfo.hFile);
        }
        else if (kind == EXCEPTION_DEBUG_EVENT && std::exchange(loaderBreakpointSeen, true) &&
                 result.exceptionCode == 0)
        {
            result.exceptionCode = event.u.Exception.ExceptionRecord.ExceptionCode;
            TerminateProcess(child.process.hProcess, 0);
        }
        ContinueDebugEvent(event.dwProcessId, event.dwThreadId, DBG_CONTINUE);
        if (kind == EXIT_PROCESS_DEBUG_EVENT)
        {
            finishChild(child, result);
            return result;
        }
    }
    // Aucun événement pendant ChildTimeout : une fenêtre l'attend. Le tuer suffit ; la session de
    // débogage finit avec le pilote.
    result.timedOut = true;
    TerminateProcess(child.process.hProcess, 1);
    finishChild(child, result);
    return result;
}

// Vide quand le scénario est conforme, sinon pourquoi il ne l'est pas.
std::string verdictOf(const Scenario& scenario, const ChildResult& result, bool underDebugger)
{
    if (result.werTooSlow)
    {
        return "WER traite encore l'échec après " + std::to_string(result.milliseconds) +
               " ms : une fenêtre de WER ou du débogueur l'attend sans doute";
    }
    if (result.timedOut)
    {
        return "toujours là après " + std::to_string(result.milliseconds) +
               " ms : une fenêtre (assertion, abort, Windows Error Reporting) l'attend sans doute";
    }
    if (underDebugger)
    {
        return result.exceptionCode == EXCEPTION_BREAKPOINT
                   ? std::string{}
                   : "sorti sans rendre la main au débogueur par un point d'arrêt";
    }
    if (result.exitCode != scenario.expectedExitCode)
    {
        return std::format("code {:#x} au lieu de {:#x}{}", result.exitCode,
                           scenario.expectedExitCode,
                           result.exitCode == 0 ? " : l'échec n'a pas arrêté le programme" : "");
    }
    if (result.standardError.find(scenario.expectedOnStderr) == std::string::npos)
    {
        return "stderr ne contient pas « " + std::string{scenario.expectedOnStderr} + " »";
    }
    return {};
}

int drive(const Scenario& scenario, bool underDebugger)
{
    std::wstring path(MAX_PATH, L'\0');
    path.resize(GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size())));
    const std::string name{scenario.name};
    const std::wstring commandLine =
        L"\"" + path + L"\" --child " + std::wstring{name.begin(), name.end()};
    const ChildResult result =
        underDebugger ? debugChild(commandLine) : runChild(commandLine, scenario.werReports);

    std::string verdict = verdictOf(scenario, result, underDebugger);
    std::string werNote;
    if (verdict.empty() && scenario.werReports && !underDebugger)
    {
        const std::optional<DWORD> seen = waitForWerEvent(result);
        if (seen.has_value())
        {
            werNote = std::format(", événement 1000 de WER vu après {} ms", *seen);
        }
        else
        {
            verdict = std::format("WER n'a pas reçu l'échec : aucun événement 1000 pour le "
                                  "processus {:#x} dans les {} ms après sa fin (le programme "
                                  "s'est terminé lui-même ?)",
                                  result.processId, WerEventTimeoutMilliseconds);
        }
    }
    std::printf("crt.report %s%s : code 0x%lx en %lu ms, exception 0x%lx%s, stderr « %s »\n",
                underDebugger ? "(sous débogueur) " : "", name.c_str(), result.exitCode,
                result.milliseconds, result.exceptionCode, werNote.c_str(),
                result.standardError.c_str());
    if (!verdict.empty())
    {
        std::fprintf(stderr, "crt.report %s : %s\n", name.c_str(), verdict.c_str());
    }
    return verdict.empty() ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
    const std::string_view mode = argc == 3 ? argv[1] : "";
    const std::string_view name = (argc == 2 || argc == 3) ? argv[argc - 1] : "";
    const Scenario* const scenario = std::ranges::find(Scenarios, name, &Scenario::name);
    if (scenario == std::ranges::end(Scenarios) ||
        (argc == 3 && mode != "--child" && mode != "--debugger"))
    {
        std::fputs("usage : levain_crt_report_probe [--child | --debugger] <scénario>\n", stderr);
        return UsageError;
    }
    if (mode == "--child")
    {
        // Hors du try ci-dessous : `uncaught-throw` doit laisser son exception sortir de main.
        scenario->trigger();
        if (scenario->expectedExitCode != 0)
        {
            std::fputs("l'échec n'a pas arrêté le programme\n", stderr); // le pilote jugera le code
        }
        return 0;
    }
    try
    {
        return drive(*scenario, mode == "--debugger");
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "crt.report : %s\n", error.what());
        return 1;
    }
}
