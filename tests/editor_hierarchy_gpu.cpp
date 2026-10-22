// Créer, supprimer et renommer dans la Hiérarchie, dans la vraie boucle (ADR-0038, décisions 3, 4,
// 6, 8, 9 et 10, morceau A6), un script d'input rejoué hors écran (le banc d'essai de l'App) :
//   - Entité > Créer une entité : une racine « Entité », choisie et ouverte en renommage ; le texte
//     tapé la renomme ; Ctrl+Z, Ctrl+Z défont le nom puis la création, Ctrl+Y la refait **au même
//     identifiant** (QWERTY et AZERTY : Ctrl+Z est une lettre).
// Il faut un device, comme `levain_app_script`.
//   levain_editor_hierarchy [vulkan|d3d12|d3d12-warp]

#include <cstddef>
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

#include "gpu_test_backend.hpp"

#include "levain/app/app.hpp"
#include "levain/core/log_ring.hpp"
#include "levain/editor/editor.hpp"
#include "levain/platform/input_script.hpp"
#include "levain/scene/components.hpp"
#include "levain/scene/reflection.hpp"

namespace
{

using levain::app::App;
using levain::editor::Action;
using levain::editor::indexOf;
using levain::editor::Mode;

struct Mass
{
    float kilograms = 1.0F;
};

struct RowSeen
{
    flecs::entity_t entity = 0;
    int depth = 0;
    std::string name;
    levain::editor::RowRect rect;
};

/// Ce qu'une image a montré de la Hiérarchie et de la pile.
struct Seen
{
    levain::editor::ActionCounters actions;
    levain::editor::MenuBarLayout bar;
    Mode mode = Mode::Edit;
    std::size_t done = 0;
    std::size_t undone = 0;
    bool modified = false;
    flecs::entity_t selected = 0;
    std::vector<RowSeen> rows;
    flecs::entity_t renaming = 0;
};

struct Ids
{
    flecs::entity_t a = 0, cube = 0, b = 0, one = 0, two = 0;
};

struct Run
{
    int exitCode = 0;
    std::vector<Seen> seen;
    Ids ids;
};

std::uint32_t azertyAt(std::uint16_t scancode)
{
    constexpr std::uint16_t A = 4, Q = 20, W = 26, Z = 29;
    switch (scancode)
    {
    case A:
        return 'q';
    case Q:
        return 'a';
    case W:
        return 'z';
    case Z:
        return 'w';
    default:
        return levain::platform::qwertyKeyAtPosition(scancode);
    }
}

struct Options
{
    bool azerty = false;
};

const RowSeen* rowOf(const Seen& seen, flecs::entity_t entity)
{
    for (const RowSeen& row : seen.rows)
    {
        if (row.entity == entity)
        {
            return &row;
        }
    }
    return nullptr;
}

std::string nameOf(const Seen& seen, flecs::entity_t entity)
{
    const RowSeen* row = rowOf(seen, entity);
    return row != nullptr ? row->name : std::string{"(absente)"};
}

std::vector<std::string> namesOf(const Seen& seen)
{
    std::vector<std::string> names;
    names.reserve(seen.rows.size());
    for (const RowSeen& row : seen.rows)
    {
        names.push_back(std::string(static_cast<std::size_t>(row.depth), '>') + row.name);
    }
    return names;
}

/// Le programme sous `withEditor`, panneaux ouverts, hors écran, une image par pas de temps.
Run play(const levain::tests::TestBackend& backend, std::string_view script, std::size_t frames,
         const Options& wanted = {})
{
    auto parsed = levain::platform::parseInputScript(script);
    if (!parsed)
    {
        throw std::runtime_error{parsed.error().message};
    }
    levain::app::AppSettings settings;
    settings.inputScript = std::move(*parsed);
    settings.steps = static_cast<int>(frames);
    settings.showUiPanels = true;
    settings.width = 960;
    settings.height = 540;
    settings.assetRoots = {LEVAIN_DATA_DIR};
    settings.bindingsFile = LEVAIN_DATA_DIR "/input.cfg";
    settings.api = backend.api;
    settings.adapter = backend.adapter;
    Run run;
    App* appPointer = nullptr;
    const auto program = [&](App& app) -> levain::core::Result<levain::app::FrameHooks>
    {
        appPointer = &app;
        app.world.entity("camera").set(levain::scene::Transform{}).set(levain::app::CameraLens{});
        levain::scene::describeAuthored<Mass>(app.world);
        const auto make = [&](const char* name)
        { return app.world.entity(name).set(levain::scene::Transform{}).set(Mass{}); };
        const auto kid = [&](flecs::entity parent, const char* name)
        {
            return app.world.entity(flecs::Parent{parent}, name)
                .set(levain::scene::Transform{})
                .set(Mass{});
        };
        run.ids.a = make("A").id();
        const flecs::entity cube = make("Cube");
        run.ids.cube = cube.id();
        run.ids.b = make("B").id();
        run.ids.one = kid(cube, "un").id();
        run.ids.two = kid(cube, "deux").id();
        app.keyAtPosition =
            wanted.azerty ? levain::platform::KeyAtPosition{azertyAt}
                          : levain::platform::KeyAtPosition{levain::platform::qwertyKeyAtPosition};
        return levain::app::FrameHooks{};
    };
    levain::editor::EditorOptions options;
    options.fontRoots = {{LEVAIN_TEST_ASSETS_DIR}};
    options.afterUi = [&](const levain::editor::Editor& editor)
    {
        Seen seen{.actions = editor.actions,
                  .bar = editor.bar,
                  .mode = editor.mode.current,
                  .done = editor.undo.done.size(),
                  .undone = editor.undo.undone.size(),
                  .modified = levain::editor::isModified(editor.undo),
                  .selected = editor.selected,
                  .rows = {},
                  .renaming = editor.hierarchy.rename.entity};
        for (const levain::editor::HierarchyRow& row : editor.hierarchy.rows)
        {
            // Les lignes sont celles du début de l'image : une annulation de cette image a pu
            // détruire l'entité depuis.
            const char* name = ecs_is_alive(appPointer->world, row.entity)
                                   ? ecs_get_name(appPointer->world, row.entity)
                                   : nullptr;
            const auto rect = editor.hierarchy.rects.find(row.entity);
            seen.rows.push_back({.entity = row.entity,
                                 .depth = row.depth,
                                 .name = name != nullptr ? name : "",
                                 .rect = rect != editor.hierarchy.rects.end()
                                             ? rect->second
                                             : levain::editor::RowRect{}});
        }
        run.seen.push_back(std::move(seen));
    };
    run.exitCode =
        levain::app::runApp(settings, levain::editor::withEditor(program, std::move(options)));
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

/// Un accord à modificateurs : les modificateurs tenus une image avant la touche, relâchés une
/// image après.
std::string chord(int frame, std::initializer_list<std::string_view> modifiers,
                  std::string_view key)
{
    std::string script;
    for (const std::string_view modifier : modifiers)
    {
        script += std::format("{} key down {}\n", frame, modifier);
    }
    script += std::format("{} key down {}\n{} key up {}\n", frame + 1, key, frame + 2, key);
    for (const std::string_view modifier : modifiers)
    {
        script += std::format("{} key up {}\n", frame + 3, modifier);
    }
    return script;
}

/// Une touche seule : appuyée à `frame`, relâchée deux images après.
std::string tap(int frame, std::string_view key)
{
    return std::format("{} key down {}\n{} key up {}\n", frame, key, frame + 2, key);
}

std::string clickScript(int frame, ImVec2 at, std::string_view button = "left")
{
    return std::format("{} mouse move {} {}\n{} button down {}\n{} button up {}\n", frame, at.x,
                       at.y, frame + 2, button, frame + 3, button);
}

std::string typed(int frame, std::string_view word)
{
    return std::format("{} text {}\n", frame, word);
}

constexpr const char* NoInput = "0 mouse move 1 1\n"; // un script vide est refusé (règle n°7)

/// Le banc pose les lignes de départ : la caméra, A, Cube (fermé, deux enfants), B.
bool startRows(const Seen& seen, const Ids& ids)
{
    return namesOf(seen) == std::vector<std::string>{"camera", "A", "Cube", "B"} &&
           rowOf(seen, ids.cube) != nullptr;
}

// --- Entité > Créer une entité
// ---------------------------------------------------------------------

/// Où l'entrée « Créer une entité » du menu Entité est dessinée, une fois le menu ouvert.
ImVec2 createEntryOf(const levain::tests::TestBackend& backend)
{
    const Run probe = play(backend, NoInput, 8);
    const ImVec2 entity = probe.seen.back().bar.entity;
    const Run opened = play(backend, clickScript(6, entity), 14);
    return opened.seen.back().bar.create;
}

int createFromMenu(const levain::tests::TestBackend& backend, bool azerty)
{
    const std::string layout = azerty ? "AZERTY" : "QWERTY";
    const Run probe = play(backend, NoInput, 8, {.azerty = azerty});
    const ImVec2 entityMenu = probe.seen.back().bar.entity;
    const ImVec2 entry = createEntryOf(backend);
    const std::string z = azerty ? "W as z" : "Z";
    // Le menu, l'entrée, le nom tapé, Entrée ; puis Ctrl+Z, Ctrl+Z, Ctrl+Y, Ctrl+Y.
    const std::string script = clickScript(6, entityMenu) + clickScript(12, entry) +
                               typed(24, "Caisse") + tap(27, "Return") +
                               chord(34, {"Left Ctrl"}, z) + chord(40, {"Left Ctrl"}, z) +
                               chord(46, {"Left Ctrl"}, "Y") + chord(52, {"Left Ctrl"}, "Y");
    const Run run = play(backend, script, 60, {.azerty = azerty});
    if (run.exitCode != 0 || run.seen.size() != 60)
    {
        return expect(false, layout + " créer : code 0 et une image relevée par image");
    }
    const auto at = [&run](std::size_t frame) -> const Seen& { return run.seen.at(frame); };
    const flecs::entity_t created = at(22).selected;
    return expect(startRows(at(5), run.ids) && at(5).done == 0,
                  layout + " créer : au départ la caméra, A, Cube, B et une pile vide") +
           expect(created != 0 && at(22).done == 1 && at(22).modified &&
                      nameOf(at(22), created) == "Entité" && at(22).renaming == created,
                  layout + " créer : « Entité » est choisie, ouverte en renommage, la pile a 1") +
           expect(nameOf(at(30), created) == "Caisse" && at(30).renaming == 0 && at(30).done == 2,
                  layout + " créer : le texte tapé puis Entrée renomment, 2 commandes") +
           expect(nameOf(at(38), created) == "Entité" && at(38).done == 1 && at(38).undone == 1,
                  layout + " créer : Ctrl+Z défait le nom") +
           expect(rowOf(at(44), created) == nullptr && at(44).selected == 0 && at(44).done == 0 &&
                      !at(44).modified,
                  layout + " créer : Ctrl+Z défait la création, désélectionnée, la scène propre") +
           expect(nameOf(at(50), created) == "Entité" && at(50).selected == created,
                  layout + " créer : Ctrl+Y la refait au même identifiant, choisie") +
           expect(nameOf(at(58), created) == "Caisse" && at(58).done == 2,
                  layout + " créer : Ctrl+Y refait le nom");
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
            std::println(stderr, "usage : levain_editor_hierarchy [vulkan|d3d12|d3d12-warp]");
            return 2;
        }
        const int failures = createFromMenu(*backend, false) + createFromMenu(*backend, true);
        return failures == 0 ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        std::fputs(e.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
}
