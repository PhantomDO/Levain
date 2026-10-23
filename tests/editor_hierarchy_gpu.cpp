// Créer, supprimer et renommer dans la Hiérarchie, dans la vraie boucle (ADR-0038, décisions 3, 4,
// 6, 8, 9 et 10, morceau A6), un script d'input rejoué hors écran (le banc d'essai de l'App) :
//   - Entité > Créer une entité : une racine « Entité », choisie et ouverte en renommage ; le texte
//     tapé la renomme ; Ctrl+Z, Ctrl+Z défont le nom puis la création, Ctrl+Y la refait **au même
//     identifiant** (QWERTY et AZERTY : Ctrl+Z est une lettre) ;
//   - le clic droit : sur le vide « Créer une entité », sur une ligne « Créer un enfant »,
//     « Renommer » et « Supprimer », chacun cliqué à la souris scriptée ;
//   - F2 sur la ligne choisie, Entrée ou un clic ailleurs valide, Échap annule ; « a.b », un frère
//     du même nom et le vide sont refusés, dits sous le champ, sans rien changer ni poser ;
//   - Suppr supprime la sélection, Ctrl+Z la rend (même identifiant, enfants, ordre), la sélection
//     suit ; Suppr ne part pas pendant qu'un champ est actif ;
//   - en Jouer, Suppr supprime sans rien poser dans la pile ; Stop rend l'entité avec son
//   identifiant ;
//   - un modèle supprimé puis annulé est dessiné de nouveau, sans avertissement.
// Il faut un device, comme `levain_app_script`.
//   levain_editor_hierarchy [vulkan|d3d12|d3d12-warp]

#include <cstddef>
#include <cstdio>
#include <exception>
#include <filesystem>
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
#include "levain/app/load_model.hpp"
#include "levain/assets/asset_id.hpp"
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
    levain::editor::HierarchyMenuLayout menu;
    flecs::entity_t renaming = 0;
    std::string renameText;
    std::string renameRefusal;
    std::vector<std::string> warnings; ///< les lignes de la console au moins « Avertissement »
    std::uint64_t modelDrawn = 0;
    bool anyItemActive = false;
};

struct Ids
{
    flecs::entity_t a = 0, cube = 0, b = 0, one = 0, two = 0;
    flecs::entity_t model = 0;
};

struct Run
{
    int exitCode = 0;
    std::vector<Seen> seen;
    Ids ids;
};

struct TempDir
{
    std::filesystem::path path;

    TempDir()
        : path{std::filesystem::temp_directory_path() /
               ("levain-editor-hierarchy-" +
                levain::assets::toString(levain::assets::generateAssetId()))}
    {
        std::filesystem::create_directories(path);
        std::filesystem::copy_file(std::filesystem::path{LEVAIN_TEST_DATA_DIR} / "two-joints.gltf",
                                   path / "two-joints.gltf");
    }

    ~TempDir()
    {
        std::error_code ignored; // un destructeur ne lève pas
        std::filesystem::remove_all(path, ignored);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
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
    bool withModel = false;
    /// Une entité créée à l'image 3 par la pile (une commande avant Jouer).
    bool commandBeforePlay = false;
    /// B porte un nom de 300 octets, plus long que le champ de renommage (255).
    bool longName = false;
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

ImVec2 centerOf(const levain::editor::RowRect& rect)
{
    return {(rect.min.x + rect.max.x) * 0.5f, (rect.min.y + rect.max.y) * 0.5f};
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
    const TempDir dir;
    levain::app::AppSettings settings;
    settings.inputScript = std::move(*parsed);
    settings.steps = static_cast<int>(frames);
    settings.showUiPanels = true;
    settings.width = 960;
    settings.height = 540;
    settings.assetRoots = {LEVAIN_DATA_DIR, dir.path};
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
        if (wanted.longName)
        {
            ecs_set_name(app.world, run.ids.b, std::string(300, 'x').c_str());
        }
        if (wanted.withModel)
        {
            const auto loaded = levain::app::loadModel(app, {.path = dir.path / "two-joints.gltf",
                                                             .placement = {},
                                                             .name = "modele",
                                                             .clip = "tourne",
                                                             .locomotion = std::nullopt});
            if (!loaded)
            {
                return std::unexpected(loaded.error());
            }
            run.ids.model = app.world.lookup("modele").id();
        }
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
                  .menu = editor.hierarchy.menu,
                  .renaming = editor.hierarchy.rename.entity,
                  .renameText = editor.hierarchy.rename.text.data(),
                  .renameRefusal = editor.hierarchy.rename.refusal,
                  .warnings = {},
                  .modelDrawn = appPointer->modelsCamera.drawn,
                  .anyItemActive = ImGui::IsAnyItemActive()};
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
        for (const auto& entry : levain::core::snapshotOf(*editor.log->ring).entries)
        {
            if (entry.level >= levain::core::LogLevel::Warning)
            {
                seen.warnings.push_back(entry.text);
            }
        }
        run.seen.push_back(std::move(seen));
    };
    options.afterUiMutable = [&](levain::editor::Editor& editor)
    {
        if (wanted.commandBeforePlay && run.seen.size() == 4)
        {
            const auto created = levain::editor::createEntity(editor.undo, appPointer->world,
                                                              editor.mode.current, 0, "Avant");
            if (!created)
            {
                throw std::runtime_error{created.error().message};
            }
        }
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

std::string playChord()
{
    return chord(4, {"Left Alt"}, "P");
}

/// Où cliquer pour un clic droit dans le vide de la Hiérarchie : sous la dernière ligne.
ImVec2 emptySpotOf(const Seen& seen)
{
    const levain::editor::RowRect last = seen.rows.back().rect;
    return {last.min.x + 30.0f, last.max.y + 80.0f};
}

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

// --- Le clic droit -----------------------------------------------------------------------------

int contextMenus(const levain::tests::TestBackend& backend)
{
    // Deux passages pour lire où le menu dessine ses entrées : sur le vide, sur la ligne « Cube ».
    const Run start = play(backend, NoInput, 8);
    const ImVec2 empty = emptySpotOf(start.seen.back());
    const ImVec2 cube = centerOf(rowOf(start.seen.back(), start.ids.cube)->rect);
    const Run emptyProbe = play(backend, clickScript(6, empty, "right"), 14);
    const Run rowProbe = play(backend, clickScript(6, cube, "right"), 14);
    const Seen& emptyMenu = emptyProbe.seen.back();
    const Seen& rowMenu = rowProbe.seen.back();
    if (!emptyMenu.menu.open || !rowMenu.menu.open)
    {
        return expect(false, "le menu contextuel s'ouvre au clic droit (vide, ligne)");
    }
    const ImVec2 create = emptyMenu.menu.create;
    // Un passage par entrée de la ligne : chacun repart d'un monde neuf.
    const Run created = play(backend, clickScript(6, empty, "right") + clickScript(12, create), 24);
    const Run child =
        play(backend, clickScript(6, cube, "right") + clickScript(12, rowMenu.menu.create), 24);
    const Run renamed = play(backend,
                             clickScript(6, cube, "right") + clickScript(12, rowMenu.menu.rename) +
                                 tap(20, "Escape"),
                             28);
    const Run removed = play(backend,
                             clickScript(6, cube, "right") + clickScript(12, rowMenu.menu.remove) +
                                 chord(20, {"Left Ctrl"}, "Z"),
                             30);
    if (created.seen.size() != 24 || child.seen.size() != 24 || renamed.seen.size() != 28 ||
        removed.seen.size() != 30)
    {
        return expect(false, "le menu contextuel : une image relevée par image");
    }
    const Seen& made = created.seen.back();
    const Seen& under = child.seen.back();
    return expect(rowMenu.selected == start.ids.cube,
                  "le clic droit sur une ligne la choisit avant d'ouvrir son menu") +
           expect(made.done == 1 && made.rows.size() == 5 && made.rows.back().name == "Entité" &&
                      made.rows.back().depth == 0 && made.renaming == made.rows.back().entity,
                  "vide > Créer une entité : une racine, ouverte en renommage") +
           expect(under.done == 1 && under.selected != start.ids.cube && under.rows.size() == 7 &&
                      under.renaming == under.selected &&
                      namesOf(under) == std::vector<std::string>{"camera", "A", "Cube", ">un",
                                                                 ">deux", ">Entité", "B"},
                  "ligne > Créer un enfant : sous Cube (ouvert), choisi, ouvert en renommage") +
           expect(renamed.seen.back().renaming == 0 && renamed.seen.back().done == 0 &&
                      namesOf(renamed.seen.at(18)) == namesOf(start.seen.back()) &&
                      renamed.seen.at(18).renaming == start.ids.cube &&
                      renamed.seen.at(18).renameText == "Cube",
                  "ligne > Renommer ouvre le champ sur Cube, Échap l'annule sans rien changer") +
           expect(removed.seen.at(18).done == 1 &&
                      rowOf(removed.seen.at(18), start.ids.cube) == nullptr,
                  "ligne > Supprimer supprime Cube") +
           expect(removed.seen.back().done == 0 && removed.seen.back().undone == 1 &&
                      removed.seen.back().selected == start.ids.cube &&
                      namesOf(removed.seen.back()) == namesOf(start.seen.back()),
                  "Ctrl+Z rend Cube au même identifiant, choisi, à sa place");
}

// --- F2, Entrée, Échap ---------------------------------------------------------------------------

int renaming(const levain::tests::TestBackend& backend)
{
    const Run start = play(backend, NoInput, 8);
    const ImVec2 cube = centerOf(rowOf(start.seen.back(), start.ids.cube)->rect);
    const ImVec2 elsewhere = emptySpotOf(start.seen.back());
    // Un clic sur Cube (le focus de la Hiérarchie), puis : F2, « Caisse », Entrée ; F2, « a.b »,
    // Entrée (refusé, reste ouvert), Échap ; F2, « A » (un frère), Entrée, Échap ; F2, Entrée (le
    // même nom) ; F2, « Autre », un clic ailleurs.
    const std::string script =
        clickScript(5, cube) + tap(10, "F2") + typed(14, "Caisse") + tap(16, "Return") +
        tap(22, "F2") + typed(26, "a.b") + tap(28, "Return") + tap(34, "Escape") + tap(40, "F2") +
        typed(44, "A") + tap(46, "Return") + tap(52, "Escape") + tap(58, "F2") + tap(63, "Return") +
        tap(68, "F2") + typed(72, "Autre") + clickScript(76, elsewhere);
    const Run run = play(backend, script, 90);
    if (run.exitCode != 0 || run.seen.size() != 90)
    {
        return expect(false, "renommer : code 0 et une image relevée par image");
    }
    const auto at = [&run](std::size_t frame) -> const Seen& { return run.seen.at(frame); };
    const flecs::entity_t cubeId = start.ids.cube;
    return expect(at(9).selected == cubeId && at(9).renaming == 0,
                  "renommer : le clic choisit Cube") +
           expect(at(13).renaming == cubeId && at(13).renameText == "Cube",
                  "renommer : F2 ouvre le champ sur Cube, son nom dedans") +
           expect(nameOf(at(20), cubeId) == "Caisse" && at(20).done == 1 && at(20).renaming == 0,
                  "renommer : « Caisse » puis Entrée, une commande") +
           expect(at(30).renaming == cubeId && !at(30).renameRefusal.empty() &&
                      at(30).renameText == "a.b" && nameOf(at(30), cubeId) == "Caisse" &&
                      at(30).done == 1,
                  "renommer : « a.b » est refusé sous le champ, ouvert, rien ne change") +
           expect(at(38).renaming == 0 && nameOf(at(38), cubeId) == "Caisse" && at(38).done == 1,
                  "renommer : Échap annule") +
           expect(at(48).renaming == cubeId && !at(48).renameRefusal.empty() &&
                      nameOf(at(48), cubeId) == "Caisse" && at(48).done == 1,
                  "renommer : le nom d'un frère est refusé, rien ne change") +
           expect(at(56).renaming == 0 && at(56).done == 1, "renommer : Échap annule encore") +
           expect(at(66).renaming == 0 && at(66).done == 1 && nameOf(at(66), cubeId) == "Caisse",
                  "renommer : Entrée sans changer le nom ne pose aucune commande") +
           expect(at(86).renaming == 0 && nameOf(at(86), cubeId) == "Autre" && at(86).done == 2,
                  "renommer : un clic ailleurs valide le nom tapé") +
           expect(run.seen.back().warnings.empty(),
                  "renommer : aucun avertissement, un refus se dit sous le champ");
}

/// N5 : un nom plus long que le champ (255 octets), F2 puis Entrée sans rien taper : le nom reste
/// entier, aucune commande. Sans le drapeau « édité », le champ renommait en le coupant.
int renamingLongName(const levain::tests::TestBackend& backend)
{
    const Run start = play(backend, NoInput, 8, {.longName = true});
    const ImVec2 row = centerOf(rowOf(start.seen.back(), start.ids.b)->rect);
    const std::string script = clickScript(5, row) + tap(10, "F2") + tap(16, "Return");
    const Run run = play(backend, script, 24, {.longName = true});
    if (run.exitCode != 0 || run.seen.size() != 24)
    {
        return expect(false, "nom long : code 0 et une image relevée par image");
    }
    return expect(run.seen.at(13).renaming == start.ids.b, "nom long : F2 ouvre le champ sur B") +
           expect(run.seen.back().done == 0 && nameOf(run.seen.back(), start.ids.b).size() == 300,
                  "nom long : Entrée sans rien taper ne coupe pas le nom et ne pose rien");
}

// --- Suppr
// -----------------------------------------------------------------------------------------

int deleting(const levain::tests::TestBackend& backend, bool azerty)
{
    const std::string layout = azerty ? "AZERTY" : "QWERTY";
    const Run start = play(backend, NoInput, 8, {.azerty = azerty});
    const flecs::entity_t cubeId = start.ids.cube;
    const ImVec2 cube = centerOf(rowOf(start.seen.back(), cubeId)->rect);
    const std::string z = azerty ? "W as z" : "Z";
    // Un clic, Suppr, Ctrl+Z, Ctrl+Y, Ctrl+Z ; puis F2 (le champ ouvert) et Suppr : le champ garde
    // sa touche.
    const std::string script = clickScript(5, cube) + tap(10, "Delete") +
                               chord(16, {"Left Ctrl"}, z) + chord(22, {"Left Ctrl"}, "Y") +
                               chord(28, {"Left Ctrl"}, z) + tap(36, "F2") + tap(42, "Delete");
    const Run run = play(backend, script, 50, {.azerty = azerty});
    if (run.exitCode != 0 || run.seen.size() != 50)
    {
        return expect(false, layout + " supprimer : code 0 et une image relevée par image");
    }
    const auto at = [&run](std::size_t frame) -> const Seen& { return run.seen.at(frame); };
    return expect(at(9).selected == cubeId,
                  layout + " supprimer : Cube est choisi, la Hiérarchie a le focus") +
           expect(rowOf(at(14), cubeId) == nullptr && at(14).done == 1 && at(14).selected == 0 &&
                      at(14).modified,
                  layout + " supprimer : Suppr supprime Cube, désélectionné") +
           expect(rowOf(at(20), cubeId) != nullptr && at(20).selected == cubeId &&
                      namesOf(at(20)) == namesOf(start.seen.back()),
                  layout +
                      " supprimer : Ctrl+Z rend Cube au même identifiant, choisi, à sa place") +
           expect(rowOf(at(26), cubeId) == nullptr && at(26).selected == 0,
                  layout + " supprimer : Ctrl+Y le supprime de nouveau, désélectionné") +
           expect(rowOf(at(32), cubeId) != nullptr && at(32).done == 0,
                  layout + " supprimer : Ctrl+Z le rend encore, la scène est propre") +
           expect(at(40).renaming == cubeId && at(48).renaming == cubeId &&
                      rowOf(at(48), cubeId) != nullptr && at(48).done == 0,
                  layout + " supprimer : Suppr dans le champ de renommage ne supprime rien");
}

// --- En Jouer ----------------------------------------------------------------------------------

int deletingInPlay(const levain::tests::TestBackend& backend)
{
    const Run start = play(backend, NoInput, 8);
    const flecs::entity_t cubeId = start.ids.cube;
    const ImVec2 cube = centerOf(rowOf(start.seen.back(), cubeId)->rect);
    // Jouer, un clic sur Cube (la Hiérarchie a le focus : le clavier va à l'interface), Suppr, puis
    // Échap : Stop.
    const std::string script = playChord() + clickScript(8, cube) + tap(13, "Delete") +
                               tap(20, "Escape") + chord(28, {"Left Ctrl"}, "Z");
    const Run run = play(backend, script, 36, {.commandBeforePlay = true});
    if (run.exitCode != 0 || run.seen.size() != 36)
    {
        return expect(false, "Jouer : code 0 et une image relevée par image");
    }
    const auto at = [&run](std::size_t frame) -> const Seen& { return run.seen.at(frame); };
    return expect(at(6).done == 1 && at(10).mode == Mode::Play && at(12).selected == cubeId,
                  "Jouer : la commande d'avant Jouer est dans la pile, Cube est choisi") +
           expect(rowOf(at(16), cubeId) == nullptr && at(16).done == 1 && at(16).undone == 0,
                  "Jouer : Suppr supprime Cube, la pile ne bouge pas") +
           expect(at(24).mode == Mode::Edit && rowOf(at(24), cubeId) != nullptr &&
                      at(24).done == 1 && at(24).undone == 0,
                  "Stop rend Cube avec son identifiant, la pile est intacte") +
           expect(at(34).done == 0 && at(34).undone == 1,
                  "après Stop, Ctrl+Z défait la commande d'avant Jouer") +
           expect(run.seen.back().warnings.empty(), "Jouer : aucun avertissement");
}

// --- Le modèle se dessine ------------------------------------------------------------------------

int modelStaysDrawn(const levain::tests::TestBackend& backend)
{
    const Run start = play(backend, NoInput, 8, {.withModel = true});
    const flecs::entity_t modelId = start.ids.model;
    const RowSeen* row = rowOf(start.seen.back(), modelId);
    if (row == nullptr)
    {
        return expect(false, "le modèle : sa racine est dans la Hiérarchie");
    }
    const ImVec2 at = centerOf(row->rect);
    // Un clic, Suppr, des images (la fin d'image décharge ce qui est tombé à zéro), Ctrl+Z.
    const std::string script =
        clickScript(5, at) + tap(10, "Delete") + chord(24, {"Left Ctrl"}, "Z");
    const Run run = play(backend, script, 40, {.withModel = true});
    if (run.exitCode != 0 || run.seen.size() != 40)
    {
        return expect(false, "le modèle : code 0 et une image relevée par image");
    }
    const auto drawnIn = [&run](std::size_t frame)
    { return run.seen.at(frame).modelDrawn - run.seen.at(frame - 1).modelDrawn; };
    return expect(drawnIn(8) > 0, "le modèle : dessiné avant la suppression") +
           expect(rowOf(run.seen.at(18), modelId) == nullptr && drawnIn(18) == 0,
                  "le modèle : supprimé, il n'est plus dessiné") +
           expect(rowOf(run.seen.at(36), modelId) != nullptr && drawnIn(36) > 0,
                  "le modèle : Ctrl+Z le rend, dessiné de nouveau (l'asset est resté chargé)") +
           // Le seul avertissement est celui du chargement (le modèle n'est pas cuit), d'avant.
           expect(run.seen.back().warnings.size() == run.seen.at(8).warnings.size(),
                  "le modèle : supprimé puis annulé sans le moindre avertissement de plus");
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
        const int failures = createFromMenu(*backend, false) + createFromMenu(*backend, true) +
                             contextMenus(*backend) + renaming(*backend) +
                             renamingLongName(*backend) + deleting(*backend, false) +
                             deleting(*backend, true) + deletingInPlay(*backend) +
                             modelStaysDrawn(*backend);
        return failures == 0 ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        std::fputs(e.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
}
