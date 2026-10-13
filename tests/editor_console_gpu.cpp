// La console de l'éditeur dans la vraie boucle (ADR-0036, morceau 14) : `withEditor` autour d'un
// programme minimal qui journalise, un script d'input rejoué hors écran (le banc d'essai de l'App).
// Les tests de fenêtre de `levain_tests` appellent `drawConsole` sur un anneau qu'ils remplissent
// eux-mêmes ; celui-ci prouve le câblage : la capture posée avant `runApp` (ce qui est écrit
// avant la boucle est dans la console), la fenêtre dessinée par `withEditor`, la hiérarchie qui
// garde son onglet, et la case « Erreurs » cochée à la souris scriptée. Il faut un device, comme
// `levain_app_script`.
//   levain_editor_console [vulkan|d3d12|d3d12-warp]

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <format>
#include <print>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>
// Les fenêtres retrouvées par leur nom : l'API interne d'ImGui.
#include <imgui_internal.h>

#include "gpu_test_backend.hpp"

#include "levain/app/app.hpp"
#include "levain/core/log.hpp"
#include "levain/core/log_ring.hpp"
#include "levain/editor/editor.hpp"
#include "levain/platform/input_script.hpp"
#include "levain/scene/components.hpp"
#include "levain/ui/tr.hpp"

namespace
{

using levain::app::App;

constexpr const char* Category = "test.console-e2e";
constexpr int WindowWidth = 1280;
constexpr int WindowHeight = 720;

/// Ce que la console a montré à une image, relevé à la fin de `ui` par `EditorOptions::afterUi`.
struct Seen
{
    bool consoleVisible = false;  ///< l'onglet de la console est celui qu'on voit
    bool errorBox = true;         ///< la case « Erreurs » est cochée
    std::size_t entries = 0;      ///< les messages que la console a copiés de l'anneau
    std::size_t rows = 0;         ///< les lignes qu'elle montre
    std::size_t errorEntries = 0; ///< parmi les messages, ceux qu'un filtre d'erreurs cache
    std::size_t ours = 0;         ///< ceux de ce test (sa catégorie)
    std::size_t ourErrors = 0;
    bool earlySeen = false; ///< le message écrit entre `withEditor` et `runApp`
    std::uint64_t errors = 0;
    std::array<std::string, 3> labels;
    ImVec2 errorBoxAt{};
};

bool tabVisible(const std::string& title)
{
    const ImGuiWindow* window = ImGui::FindWindowByName(title.c_str());
    return window != nullptr && window->DockTabIsVisible;
}

Seen seenOf(const levain::editor::Editor& editor)
{
    const levain::editor::Console& console = editor.console;
    Seen seen{.consoleVisible = tabVisible(levain::ui::labelOf("Console")),
              .errorBox = console.error,
              .entries = console.snapshot.entries.size(),
              .rows = console.rows.size(),
              .errors = console.snapshot.counts.errors,
              .labels = console.labels,
              .errorBoxAt = console.layout.error};
    for (const levain::core::LogEntry& entry : console.snapshot.entries)
    {
        const bool isError =
            levain::core::severityOf(entry.level) == levain::core::LogSeverity::Error;
        seen.errorEntries += isError ? 1 : 0;
        seen.earlySeen = seen.earlySeen || entry.text == "avant runApp";
        if (entry.category == Category)
        {
            ++seen.ours;
            seen.ourErrors += isError ? 1 : 0;
        }
    }
    return seen;
}

/// Le programme : une caméra, quatre messages (un par niveau, l'erreur deux fois) écrits au
/// démarrage, et la console mise en avant à l'image `focusFrame`, comme un clic sur son onglet (au
/// départ, c'est l'onglet de l'image qu'on voit dans le nœud : voir `withEditor`).
levain::app::StartFunction programOf(int focusFrame)
{
    return [focusFrame](App& app) -> levain::core::Result<levain::app::FrameHooks>
    {
        app.world.entity("camera").set(levain::scene::Transform{}).set(levain::app::CameraLens{});
        // Épinglée : une vraie fenêtre lirait la disposition de la machine (AZERTY chez Donnovan).
        app.keyAtPosition = levain::platform::qwertyKeyAtPosition;
        levain::core::log(Category, levain::core::LogLevel::Info, "chargé");
        levain::core::log(Category, levain::core::LogLevel::Warning, "lent");
        levain::core::log(Category, levain::core::LogLevel::Error, "panne");
        levain::core::log(Category, levain::core::LogLevel::Error, "panne");
        levain::app::FrameHooks hooks;
        hooks.ui = [frame = 0, focusFrame](App&) mutable
        {
            if (frame++ == focusFrame)
            {
                ImGui::SetWindowFocus(levain::ui::labelOf("Console").c_str());
            }
        };
        return hooks;
    };
}

struct Run
{
    int exitCode = 0;
    std::vector<Seen> seen;
};

/// Le programme sous `withEditor`, panneaux ouverts, dans la vraie boucle hors écran, une image par
/// pas de temps. Un message est écrit entre `withEditor` et `runApp` : ce que la fenêtre et le
/// device écrivent en s'ouvrant, avant la première image, doit aussi être dans la console.
Run play(const levain::tests::TestBackend& backend, std::string_view script, int frames,
         int focusFrame)
{
    auto parsed = levain::platform::parseInputScript(script);
    if (!parsed)
    {
        throw std::runtime_error{parsed.error().message};
    }
    levain::app::AppSettings settings;
    settings.inputScript = std::move(*parsed);
    settings.steps = frames;
    settings.showUiPanels = true;
    settings.width = WindowWidth;
    settings.height = WindowHeight;
    settings.assetRoots = {LEVAIN_DATA_DIR};
    settings.bindingsFile = LEVAIN_DATA_DIR "/input.cfg";
    settings.api = backend.api;
    settings.adapter = backend.adapter;
    Run run;
    levain::editor::EditorOptions options;
    options.afterUi = [&run](const levain::editor::Editor& editor)
    { run.seen.push_back(seenOf(editor)); };
    auto start = levain::editor::withEditor(programOf(focusFrame), std::move(options));
    levain::core::log(Category, levain::core::LogLevel::Info, "avant runApp");
    run.exitCode = levain::app::runApp(settings, start);
    return run;
}

int expect(bool holds, std::string_view what)
{
    if (!holds)
    {
        std::println(stderr, "échec : {}", what);
    }
    return holds ? 0 : 1;
}

bool insideWindow(ImVec2 at)
{
    return at.x > 0.0f && at.x < static_cast<float>(WindowWidth) && at.y > 0.0f &&
           at.y < static_cast<float>(WindowHeight);
}

constexpr int Frames = 14;
constexpr int FocusFrame = 4;

/// La console s'ouvre sans prendre l'onglet du nœud, se met en avant à la demande, et montre ce que
/// le processus a écrit : avant la boucle, au démarrage du programme, avec ses comptes sur les
/// cases.
int playOpening(const levain::tests::TestBackend& backend, ImVec2& errorBoxAt)
{
    // (un script vide est refusé, bruyamment : un mouvement de souris loin de tout, pour rien.)
    const Run run = play(backend, "0 mouse move 1 1\n", Frames, FocusFrame);
    if (run.exitCode != 0 || run.seen.size() != Frames)
    {
        return expect(false, "l'ouverture : code 0 et une image relevée par image");
    }
    const Seen& before = run.seen.at(FocusFrame - 1);
    const Seen& last = run.seen.back();
    errorBoxAt = last.errorBoxAt;
    // Dans le nœud du bas, avec Image, Passes et Scène, ImGui montre le dernier onglet venu : une
    // console dessinée après les fenêtres du moteur prendrait la place de ce qu'on voyait à
    // l'ouverture (`withEditor` la dessine avant).
    return expect(!before.consoleVisible,
                  "à l'ouverture, la console ne prend pas l'onglet qu'on voyait") +
           expect(last.consoleVisible, "la console mise en avant est dessinée") +
           expect(last.earlySeen,
                  "ce qui est écrit entre withEditor et runApp est dans la console") +
           expect(last.ours == 5 && last.ourErrors == 2,
                  "les messages du programme y sont : 5 messages dont 2 erreurs") +
           expect(last.errors >= 2 && last.labels[2] == std::format("Erreurs : {}", last.errors),
                  "la case « Erreurs » porte le compte des erreurs reçues") +
           expect(last.errorBox && last.rows == last.entries,
                  "sans filtre, une ligne par message") +
           expect(insideWindow(last.errorBoxAt), "la case « Erreurs » est dans la fenêtre");
}

/// Un clic de la souris scriptée sur la case « Erreurs » la décoche : les erreurs quittent la
/// liste, et le compte de la case ne bouge pas (il compte ce qui est reçu, pas ce qui est montré).
int playClick(const levain::tests::TestBackend& backend, ImVec2 errorBoxAt)
{
    const std::string script = std::format("6 mouse move {} {}\n8 button down left\n"
                                           "9 button up left\n",
                                           errorBoxAt.x, errorBoxAt.y);
    const Run run = play(backend, script, Frames, FocusFrame);
    if (run.exitCode != 0 || run.seen.size() != Frames)
    {
        return expect(false, "le clic : code 0 et une image relevée par image");
    }
    const Seen& before = run.seen.at(7);
    const Seen& after = run.seen.back();
    return expect(before.errorBox && before.rows == before.entries,
                  "avant le clic, la case est cochée et tous les messages sont montrés") +
           expect(!after.errorBox, "le clic décoche la case « Erreurs »") +
           expect(after.errorEntries >= 2 && after.rows == after.entries - after.errorEntries,
                  "les erreurs ont quitté la liste, les autres messages y sont") +
           expect(after.labels[2] == before.labels[2],
                  "le compte de la case ne change pas : il compte les messages reçus");
}

} // namespace

int main(int argc, char** argv)
{
    try
    {
        const std::span arguments{argv, static_cast<std::size_t>(argc)};
        const auto backend =
            levain::tests::testBackendNamed(arguments.size() == 2 ? arguments[1] : "vulkan");
        if (arguments.size() > 2 || !backend)
        {
            std::println(stderr, "usage : levain_editor_console [vulkan|d3d12|d3d12-warp]");
            return 2;
        }
        ImVec2 errorBoxAt{};
        const int failures = playOpening(*backend, errorBoxAt) + playClick(*backend, errorBoxAt);
        return failures == 0 ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        std::fputs(e.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
}
