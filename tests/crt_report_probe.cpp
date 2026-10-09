// Le contre-test de engine/core/src/crt_report.cpp (règle n°7) : chaque scénario fait échouer le
// programme de l'une des manières qui ouvraient une fenêtre sous Windows, et le programme juge
// qu'il s'est arrêté vite, avec un code non nul, en disant pourquoi sur stderr.
//
//   levain_crt_report_probe <scénario>             le pilote : lance l'enfant ci-dessous et juge
//   levain_crt_report_probe --debugger <scénario>  le pilote, débogueur de l'enfant
//   levain_crt_report_probe --child <scénario>     l'enfant : déclenche l'échec
//
// Le pilote est ce programme, et non ctest : PASS_REGULAR_EXPRESSION fait ignorer le code de
// sortie, WILL_FAIL inverse le verdict de l'expression, et sur un dépassement de délai ctest ne tue
// pas la fenêtre de l'enfant. Ici l'enfant est tué au bout de ChildTimeout. Le probe ne lie que
// core et n'appelle rien du routage : s'il passe, tout programme qui lie core l'a, sans l'avoir
// demandé.
#include <algorithm>
#include <array>
#include <cassert>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <crtdbg.h>
#include <intrin.h>
#include <windows.h>

#include "levain/core/assert.hpp"

namespace
{

// Largement plus que le démarrage de l'enfant : une fenêtre encore là après ce temps n'ira pas
// mieux.
constexpr DWORD ChildTimeoutMilliseconds = 10'000;
constexpr int UsageError = 64;

// Comment l'enfant doit finir : arrêté par l'échec (code non nul), ou revenu de son scénario
// (code 0) quand celui-ci ne fait que lire l'état du routage.
enum class Ending : std::uint8_t
{
    Stopped,
    Returned,
};

struct Scenario
{
    std::string_view name;
    void (*trigger)();
    std::string_view expectedOnStderr; ///< Vide : seul le code de sortie juge.
    Ending ending = Ending::Stopped;
};

// Ce que l'enfant reçoit du routage, lancé sans le mode d'erreur du pilote (startChild).
void printErrorMode()
{
    const bool noFaultBox = (GetErrorMode() & SEM_NOGPFAULTERRORBOX) != 0;
    const bool toStderr = _set_error_mode(_REPORT_ERRMODE) == _OUT_TO_STDERR;
    std::fprintf(stderr, "SEM_NOGPFAULTERRORBOX %s, _OUT_TO_STDERR %s\n",
                 noFaultBox ? "posé" : "absent", toStderr ? "posé" : "absent");
}

// Un scénario par fenêtre connue (build/GOTCHA.md), en Debug : la Release ne change rien
// (engine/core/README.md), et `error-mode` le vérifie.
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
    {"abort", [] { std::abort(); }, "abort() has been called"},
    // [except.handle]/9 : std::terminate, donc le gestionnaire de std::set_terminate.
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
     "std::terminate appelé"},
    // Sans abort() : un gestionnaire de SIGABRT qui rendrait la main laisserait le programme
    // continuer, et finir en 0.
    {"raise-sigabrt", [] { std::raise(SIGABRT); }, ""},
    {"null-write", [] { *static_cast<volatile int*>(nullptr) = 1; }, "0xc0000005"},
    {"breakpoint", [] { LEVAIN_DEBUG_BREAK(); },
     "0x80000003"}, // celui de LEVAIN_ASSERT, sans débogueur
    // Un constat, pas un contre-test : rien n'intercepte un __fastfail, et sur ce portable Windows
    // Error Reporting n'ouvre pas de fenêtre ; il ne vérifie que l'arrêt.
    {"fastfail", [] { __fastfail(7); }, ""},
    {"error-mode", &printErrorMode, "SEM_NOGPFAULTERRORBOX posé, _OUT_TO_STDERR posé",
     Ending::Returned},
#else
    {"error-mode", &printErrorMode, "SEM_NOGPFAULTERRORBOX absent, _OUT_TO_STDERR absent",
     Ending::Returned},
#endif
};

struct Child
{
    PROCESS_INFORMATION process{};
    HANDLE errorRead = nullptr;
    DWORD start = 0;
};

struct ChildResult
{
    DWORD exitCode = 0;
    DWORD milliseconds = 0;
    bool timedOut = false;
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

ChildResult runChild(const std::wstring& commandLine)
{
    const Child child = startChild(commandLine, 0);
    ChildResult result;
    result.timedOut =
        WaitForSingleObject(child.process.hProcess, ChildTimeoutMilliseconds) == WAIT_TIMEOUT;
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
    if (scenario.ending == Ending::Stopped && result.exitCode == 0)
    {
        return "sorti avec le code 0 : l'échec n'a pas arrêté le programme";
    }
    if (scenario.ending == Ending::Returned && result.exitCode != 0)
    {
        return "arrêté (code non nul) : ce scénario ne fait que lire, il devait finir en 0";
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
    const ChildResult result = underDebugger ? debugChild(commandLine) : runChild(commandLine);

    std::printf("crt.report %s%s : code 0x%lx en %lu ms, exception 0x%lx, stderr « %s »\n",
                underDebugger ? "(sous débogueur) " : "", name.c_str(), result.exitCode,
                result.milliseconds, result.exceptionCode, result.standardError.c_str());
    const std::string verdict = verdictOf(scenario, result, underDebugger);
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
        if (scenario->ending == Ending::Stopped)
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
