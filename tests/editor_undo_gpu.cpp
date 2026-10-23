// Annuler et Rétablir de l'éditeur dans la vraie boucle (ADR-0038, décisions 1 et 9, morceau A4),
// un script d'input rejoué hors écran (le banc d'essai de l'App). Les valeurs changent par
// `editValue`, la porte directe des commandes (l'inspecteur, qui l'appellera, est le morceau A5) :
//   - en Édition, Ctrl+Z défait, Ctrl+Y et Ctrl+Maj+Z refont, sous une disposition QWERTY et sous
//     un AZERTY (l'accord est une lettre : la touche à la place du W y tape « z ») ; la touche Z
//     d'un AZERTY, qui tape « w », ne défait rien ;
//   - en Jouer, la Vue focalisée donne Ctrl+Z au jeu (route *jeu*, ADR-0036 décision 4) : rien
//     n'est atteint ; un panneau cliqué le reçoit, et il est refusé et dit ; la valeur changée en
//     jeu n'entre pas dans la pile ; après Stop, Ctrl+Z défait la commande d'avant Jouer ; en
//     Simuler il est refusé et dit ;
//   - le menu Édition, ouvert en Jouer, montre Annuler grisé et sa raison au survol ; ouvert en
//     Édition, actif, et un clic sur « Annuler » défait ;
//   - Ctrl+Z pendant qu'un élément d'ImGui est actif (un glissé) est refusé ;
//   - **les gestes du vrai inspecteur** (morceau A5, ADR-0038 décision 2), cliqués et glissés dans
//     la fenêtre dont le banc relève les champs (`Inspector::fieldCenters`) : 30 images de glissé
//     donnent une commande, relâcher puis reprendre en donne deux, une liste déroulante sans choix
//     zéro et un choix une, Tab de x à y deux, Échap zéro, retaper la même valeur zéro ; un geste
//     ouvert quand Jouer commence est fermé d'abord (une commande, ni perdue ni doublée) ; une
//     annulation vide les angles tapés.
// Il faut un device, comme `levain_app_script`.
//   levain_editor_undo [vulkan|d3d12|d3d12-warp]

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <format>
#include <map>
#include <print>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <glm/gtc/quaternion.hpp>
#include <imgui.h>
#include <imgui_internal.h> // la bulle d'ImGui, retrouvée par son nom

#include "gpu_test_backend.hpp"

#include "levain/app/app.hpp"
#include "levain/editor/editor.hpp"
#include "levain/platform/input_script.hpp"
#include "levain/scene/components.hpp"
#include "levain/scene/reflection.hpp"

namespace undo_bench // nommé : flecs ne réfléchit pas une enum d'un espace anonyme
{

/// Une liste déroulante et deux nombres, pour les gestes que l'inspecteur voit : le glissé, la
/// liste (qui ne donne aucun bord), le Tab de x à y.
enum class Mood : std::uint8_t
{
    Calm,
    Wild,
    Furious,
};

struct Tuning
{
    float x = 0.0F;
    float y = 0.0F;
    Mood mood = Mood::Calm;
};

} // namespace undo_bench

namespace
{

using undo_bench::Mood;
using undo_bench::Tuning;

using levain::app::App;
using levain::editor::Action;
using levain::editor::indexOf;

struct Mass
{
    float kilograms = 0.0F;
};

/// Ce que l'inspecteur montre : `Tuning` seul, ou un `Transform` (pour les angles tapés).
enum class Select : std::uint8_t
{
    Nothing,
    Tuning,
    Pose,
};

/// Un geste d'une image : la valeur de la masse change à la fin de cette image (`editValue`).
struct Change
{
    std::size_t frame = 0;
    float kilograms = 0.0F;
};

struct Seen
{
    levain::editor::ActionCounters actions;
    levain::editor::MenuBarLayout bar;
    levain::editor::Mode mode = levain::editor::Mode::Edit;
    float kilograms = 0.0F;
    std::size_t done = 0;   ///< la taille de la pile
    std::size_t undone = 0; ///< les refaisables
    bool modified = false;
    bool itemActive = false; ///< un élément d'ImGui est actif (le glissé)
    bool tooltip = false;    ///< une bulle est montrée (le survol d'une entrée grisée)
    std::string status;      ///< le dernier message de la barre d'état
    Tuning tuning;
    glm::quat rotation{1.0F, 0.0F, 0.0F, 0.0F};
    std::vector<ImVec2> centers; ///< où cliquer pour saisir chaque champ de l'inspecteur
    std::size_t typedAngles = 0; ///< les angles tapés que l'inspecteur garde
    /// La liste déroulante ouverte : le milieu de sa première ligne et le pas entre deux lignes
    /// (rien : x négatif).
    ImVec2 comboRow{-1.0F, -1.0F};
    float rowStep = 0.0F;
};

struct Run
{
    int exitCode = 0;
    std::vector<Seen> seen;
};

/// Une bulle d'ImGui est montrée à cette image (`SetTooltip` : la fenêtre « ##Tooltip_00 »).
///
/// **Dépend d'un détail interne d'ImGui** (`imgui_internal.h`, le nom que `BeginTooltipEx` donne à
/// la première bulle, vrai en 1.92.8) : ImGui n'offre aucune route publique pour demander « une
/// bulle est-elle ouverte ? ». Le piège est borné : si une montée de version renomme la fenêtre,
/// cette fonction rend toujours faux, et l'assertion **positive** du banc (la bulle d'Annuler grisé
/// survolé, `playMenu`) échoue bruyamment, ce n'est pas un test qui passe sans rien vérifier.
bool tooltipShown()
{
    const ImGuiWindow* window = ImGui::FindWindowByName("##Tooltip_00");
    return window != nullptr && window->WasActive;
}

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
    std::vector<Change> changes{};
    bool azerty = false;
    bool dragField = false; ///< une fenêtre avec un champ qu'on peut glisser, en (400, 150)
    Select select = Select::Nothing; ///< l'entité que l'inspecteur montre dès la deuxième image
    /// Du texte tapé : à la fin de cette image, ImGui le lit à la suivante (le script d'input ne
    /// sait pas taper).
    std::map<std::size_t, std::string> typed{};
    /// La valeur de départ de `Tuning::x` : une que « %.3f » n'écrit pas exactement, pour qu'Échap
    /// (qu'ImGui rejoue avec le texte formaté) ne rende pas l'avant par hasard.
    float tuningX = 0.0F;
    /// La rotation de départ de la pose : un angle lu d'un quaternion est presque jamais exact à
    /// trois décimales.
    glm::quat poseRotation{1.0F, 0.0F, 0.0F, 0.0F};
};

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
    flecs::entity target;
    flecs::entity tuning;
    flecs::entity pose;
    float dragged = 0.0F;
    const auto program = [&](App& app) -> levain::core::Result<levain::app::FrameHooks>
    {
        appPointer = &app;
        app.world.entity("camera").set(levain::scene::Transform{}).set(levain::app::CameraLens{});
        levain::scene::describeAuthored<Mass>(app.world);
        target = app.world.entity("cible").set(Mass{});
        levain::scene::describeAuthored<Tuning>(app.world);
        tuning = app.world.entity("reglage").set(Tuning{.x = wanted.tuningX});
        levain::scene::Transform start;
        start.rotation = wanted.poseRotation;
        pose = app.world.entity("pose").set(start);
        // Épinglée : une vraie fenêtre lirait la disposition de la machine.
        app.keyAtPosition =
            wanted.azerty ? levain::platform::KeyAtPosition{azertyAt}
                          : levain::platform::KeyAtPosition{levain::platform::qwertyKeyAtPosition};
        levain::app::FrameHooks hooks;
        hooks.ui = [&wanted, &dragged](App&)
        {
            if (!wanted.dragField)
            {
                return;
            }
            ImGui::SetNextWindowPos({400.0f, 150.0f});
            ImGui::SetNextWindowSize({200.0f, 80.0f});
            constexpr auto Flags = static_cast<ImGuiWindowFlags>(
                static_cast<unsigned>(ImGuiWindowFlags_NoDecoration) |
                static_cast<unsigned>(ImGuiWindowFlags_NoMove) |
                static_cast<unsigned>(ImGuiWindowFlags_NoSavedSettings));
            ImGui::Begin("###glisse", nullptr, Flags);
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::DragFloat("##champ", &dragged);
            ImGui::End();
        };
        return hooks;
    };
    levain::editor::EditorOptions options;
    options.fontRoots = {{LEVAIN_TEST_ASSETS_DIR}};
    options.afterUi = [&](const levain::editor::Editor& editor)
    {
        // La liste déroulante ouverte est une fenêtre d'ImGui, « ##Combo_00 » : sa première ligne
        // est sous le rembourrage, et chaque ligne est une hauteur de texte plus l'espacement.
        ImVec2 comboRow{-1.0F, -1.0F};
        const ImGuiWindow* combo = ImGui::FindWindowByName("##Combo_00");
        if (combo != nullptr && combo->WasActive)
        {
            comboRow = {combo->Pos.x + (combo->Size.x * 0.5F),
                        combo->Pos.y + ImGui::GetStyle().WindowPadding.y +
                            (ImGui::GetTextLineHeight() * 0.5F)};
        }
        run.seen.push_back({.actions = editor.actions,
                            .bar = editor.bar,
                            .mode = editor.mode.current,
                            .kilograms = target.get<Mass>().kilograms,
                            .done = editor.undo.done.size(),
                            .undone = editor.undo.undone.size(),
                            .modified = levain::editor::isModified(editor.undo),
                            .itemActive = ImGui::IsAnyItemActive(),
                            .tooltip = tooltipShown(),
                            .status = editor.status.text,
                            .tuning = tuning.get<Tuning>(),
                            .rotation = pose.get<levain::scene::Transform>().rotation,
                            .centers = editor.inspector.fieldCenters,
                            .typedAngles = editor.inspector.typedAngles.size(),
                            .comboRow = comboRow,
                            .rowStep = ImGui::GetTextLineHeightWithSpacing()});
    };
    options.afterUiMutable = [&](levain::editor::Editor& editor)
    {
        if (run.seen.size() == 1 && wanted.select != Select::Nothing)
        {
            editor.selected = wanted.select == Select::Tuning ? tuning : pose;
        }
        for (const auto& [frame, text] : wanted.typed)
        {
            if (frame + 1 == run.seen.size())
            {
                ImGui::GetIO().AddInputCharactersUTF8(text.c_str());
            }
        }
        for (const Change& change : wanted.changes)
        {
            if (change.frame + 1 == run.seen.size())
            {
                const Mass value{.kilograms = change.kilograms};
                const auto recorded =
                    levain::editor::editValue(editor.undo, appPointer->world, editor.mode.current,
                                              target, appPointer->world.id<Mass>(), &value);
                if (!recorded)
                {
                    throw std::runtime_error{recorded.error().message};
                }
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
/// image après (SDL pose les modificateurs tenus sur la touche qui suit).
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

std::string clickScript(int frame, ImVec2 at)
{
    return std::format("{} mouse move {} {}\n{} button down left\n{} button up left\n", frame, at.x,
                       at.y, frame + 2, frame + 3);
}

constexpr const char* NoInput = "0 mouse move 1 1\n"; // un script vide est refusé (règle n°7)

/// Alt+P : le jeu dès l'image 5. Une fonction : une `std::string` globale peut lever à
/// l'initialisation.
std::string playChord()
{
    return chord(4, {"Left Alt"}, "P");
}

/// Ctrl+Z défait, Ctrl+Y et Ctrl+Maj+Z refont ; sous un AZERTY la lettre est celle de la touche.
int playEdit(const levain::tests::TestBackend& backend, bool azerty)
{
    const std::string_view z = azerty ? "W as z" : "Z"; // la touche qui tape « z »
    std::string script = azerty ? "0 keymap changed\n" : "";
    if (azerty)
    {
        script += chord(1, {"Left Ctrl"}, "Z as w"); // Ctrl+W : ne défait rien
    }
    script += chord(8, {"Left Ctrl"}, z) + chord(13, {"Left Ctrl"}, "Y") +
              chord(17, {"Left Ctrl"}, z) + chord(21, {"Left Ctrl", "Left Shift"}, z);
    const Run run =
        play(backend, script, 27, {.changes = {{3, 5.0F}, {5, 9.0F}}, .azerty = azerty});
    const std::string layout = azerty ? "AZERTY" : "QWERTY";
    if (run.exitCode != 0 || run.seen.size() != 27)
    {
        return expect(false, layout + " : code 0 et une image relevée par image");
    }
    const auto at = [&run](std::size_t frame) -> const Seen& { return run.seen.at(frame); };
    const auto count = [&at](std::size_t frame, Action action)
    { return at(frame).actions.performed.at(indexOf(action)); };
    return expect(at(2).kilograms == 0.0F && !at(2).modified,
                  layout + " : au départ la pile est vide et la scène propre") +
           expect(at(4).kilograms == 5.0F && at(6).kilograms == 9.0F && at(6).done == 2 &&
                      at(6).modified,
                  layout + " : deux changements, deux commandes, la scène modifiée") +
           expect(count(5, Action::Undo) == 0 &&
                      at(5).actions.declined.at(indexOf(Action::Undo)) == 0,
                  layout + " : aucune annulation avant la touche (Ctrl+W d'un AZERTY comprise)") +
           expect(at(12).kilograms == 5.0F && count(12, Action::Undo) == 1 && at(12).done == 1 &&
                      at(12).undone == 1,
                  layout + " : Ctrl+Z défait, une fois") +
           expect(at(16).kilograms == 9.0F && count(16, Action::Redo) == 1 && at(16).undone == 0,
                  layout + " : Ctrl+Y refait") +
           expect(at(20).kilograms == 5.0F && count(20, Action::Undo) == 2,
                  layout + " : Ctrl+Z défait de nouveau") +
           expect(at(26).kilograms == 9.0F && count(26, Action::Redo) == 2,
                  layout + " : Ctrl+Maj+Z refait aussi");
}

/// En Jouer, Ctrl+Z ne fait rien et rien n'entre dans la pile ; Stop, puis Ctrl+Z défait la
/// commande d'avant Jouer.
int playPlayThenStop(const levain::tests::TestBackend& backend)
{
    std::string script = playChord() + chord(10, {"Left Ctrl"}, "Z") +
                         "15 key down Escape\n16 key up Escape\n" + chord(20, {"Left Ctrl"}, "Z");
    const Run run = play(backend, script, 28, {.changes = {{3, 5.0F}, {7, 8.0F}}});
    if (run.exitCode != 0 || run.seen.size() != 28)
    {
        return expect(false, "Jouer : code 0 et une image relevée par image");
    }
    const auto at = [&run](std::size_t frame) -> const Seen& { return run.seen.at(frame); };
    const auto undone = [&at](std::size_t frame)
    { return at(frame).actions.performed.at(indexOf(Action::Undo)); };
    const auto declined = [&at](std::size_t frame)
    { return at(frame).actions.declined.at(indexOf(Action::Undo)); };
    return expect(at(4).done == 1 && at(6).mode == levain::editor::Mode::Play,
                  "Jouer : la commande d'avant Jouer est dans la pile") +
           expect(at(8).kilograms == 8.0F && at(8).done == 1,
                  "Jouer : la valeur changée en jeu n'entre pas dans la pile") +
           // Sous la route *jeu* la touche va au jeu : l'éditeur ne la voit pas (Ctrl+Z est une
           // touche du jeu), la pile est gelée.
           expect(at(13).kilograms == 8.0F && undone(13) == 0 && declined(13) == 0 &&
                      at(13).done == 1 && at(13).undone == 0,
                  "Jouer : Ctrl+Z ne fait rien, la pile est gelée") +
           expect(at(18).mode == levain::editor::Mode::Edit && at(18).kilograms == 5.0F &&
                      at(18).done == 1,
                  "Stop rend la scène d'avant Jouer, la pile est intacte") +
           expect(at(24).kilograms == 0.0F && undone(24) == 1 && at(24).done == 0 &&
                      at(24).undone == 1 && !at(24).modified,
                  "après Stop, Ctrl+Z défait la commande d'avant Jouer");
}

/// En Simuler l'éditeur garde le clavier : Ctrl+Z est atteint, grisé et dit, et la pile reste
/// gelée.
int playSimulate(const levain::tests::TestBackend& backend)
{
    const std::string script = chord(4, {"Left Alt"}, "S") + chord(10, {"Left Ctrl"}, "Z");
    const Run run = play(backend, script, 16, {.changes = {{3, 5.0F}, {7, 8.0F}}});
    if (run.exitCode != 0 || run.seen.size() != 16)
    {
        return expect(false, "Simuler : code 0 et une image relevée par image");
    }
    const Seen& last = run.seen.back();
    return expect(last.mode == levain::editor::Mode::Simulate && last.kilograms == 8.0F,
                  "Simuler : la valeur changée en simulation est là") +
           expect(last.actions.declined.at(indexOf(Action::Undo)) == 1 &&
                      last.actions.performed.at(indexOf(Action::Undo)) == 0 && last.done == 1 &&
                      last.undone == 0,
                  "Simuler : Ctrl+Z est grisé et dit, rien n'entre dans la pile, rien ne sort");
}

/// En Jouer, un panneau cliqué reçoit le clavier : Ctrl+Z est refusé et dit, la pile gelée. La
/// fenêtre du champ glissable fait le panneau ; on clique sous le champ, jamais sur lui.
int playPanelInPlay(const levain::tests::TestBackend& backend)
{
    const std::string script =
        playChord() + clickScript(7, {500.0f, 215.0f}) + chord(13, {"Left Ctrl"}, "Z");
    const Run run = play(backend, script, 20, {.changes = {{3, 5.0F}}, .dragField = true});
    if (run.exitCode != 0 || run.seen.size() != 20)
    {
        return expect(false, "Jouer, panneau : code 0 et une image relevée par image");
    }
    const Seen& last = run.seen.back();
    return expect(last.mode == levain::editor::Mode::Play && last.kilograms == 5.0F,
                  "Jouer, panneau : on joue encore") +
           expect(last.actions.declined.at(indexOf(Action::Undo)) == 1 &&
                      last.actions.performed.at(indexOf(Action::Undo)) == 0 && last.done == 1 &&
                      last.undone == 0,
                  "Jouer, un panneau cliqué : Ctrl+Z est refusé, compté, la pile gelée") +
           expect(last.status == "Annuler : seulement en Édition",
                  "Jouer, un panneau cliqué : Ctrl+Z le dit à la console et à la barre d'état");
}

/// Le menu Édition : Annuler grisé en Jouer (la pile gelée), actif en Édition.
int playMenu(const levain::tests::TestBackend& backend)
{
    const Run probe = play(backend, NoInput, 8);
    if (probe.seen.size() != 8 || probe.seen.back().bar.edit.x <= 0.0f)
    {
        return expect(false, "le menu : le passage qui lit la barre a relevé une image par image");
    }
    const ImVec2 edit = probe.seen.back().bar.edit;
    const Run inEdit = play(backend, clickScript(6, edit), 14, {.changes = {{3, 5.0F}}});
    const Run inPlay =
        play(backend, playChord() + clickScript(8, edit), 16, {.changes = {{3, 5.0F}}});
    if (inEdit.seen.size() != 14 || inPlay.seen.size() != 16)
    {
        return expect(false, "le menu : une image relevée par image");
    }
    const Seen& editing = inEdit.seen.back();
    const Seen& playing = inPlay.seen.back();
    // Le clic sur l'entrée « Annuler » du menu ouvert défait, comme le raccourci ; le survol de
    // l'entrée grisée, en Jouer, montre une bulle.
    const Run clicked = play(backend, clickScript(6, edit) + clickScript(12, editing.bar.undo), 20,
                             {.changes = {{3, 5.0F}}});
    const Run hovered =
        play(backend,
             playChord() + clickScript(8, edit) + "13 mouse move 1 1\n" +
                 std::format("14 mouse move {} {}\n", playing.bar.undo.x, playing.bar.undo.y),
             20, {.changes = {{3, 5.0F}}});
    if (clicked.seen.size() != 20 || hovered.seen.size() != 20)
    {
        return expect(false, "le menu : une image relevée par image (clic, survol)");
    }
    const Seen& undone = clicked.seen.back();
    return expect(editing.bar.editOpen && editing.bar.undoEnabled && !editing.bar.redoEnabled,
                  "Édition : le menu est ouvert, Annuler actif, Rétablir grisé (rien à refaire)") +
           expect(editing.bar.modifiedShown, "Édition : le « * » de la scène modifiée est là") +
           expect(playing.mode == levain::editor::Mode::Play && playing.bar.editOpen &&
                      !playing.bar.undoEnabled && !playing.bar.redoEnabled,
                  "Jouer : le menu est ouvert, Annuler grisé malgré la commande") +
           expect(undone.actions.performed.at(indexOf(Action::Undo)) == 1 &&
                      undone.kilograms == 0.0F && undone.done == 0 && undone.undone == 1,
                  "Édition : un clic sur l'entrée « Annuler » défait la commande") +
           expect(
               hovered.seen.at(10).mode == levain::editor::Mode::Play &&
                   !hovered.seen.at(12).tooltip && hovered.seen.back().tooltip &&
                   hovered.seen.back().actions.performed.at(indexOf(Action::Undo)) == 0,
               "Jouer : le survol d'Annuler grisé montre une bulle (la raison), et rien ne part");
}

/// Ctrl+Z pendant un glissé (un élément actif) est refusé ; le champ relâché, il passe.
int playDrag(const levain::tests::TestBackend& backend)
{
    const std::string script = "5 mouse move 480 170\n7 button down left\n" +
                               chord(10, {"Left Ctrl"}, "Z") + "15 button up left\n" +
                               chord(18, {"Left Ctrl"}, "Z");
    const Run run = play(backend, script, 24, {.changes = {{3, 5.0F}}, .dragField = true});
    if (run.exitCode != 0 || run.seen.size() != 24)
    {
        return expect(false, "le glissé : code 0 et une image relevée par image");
    }
    const auto at = [&run](std::size_t frame) -> const Seen& { return run.seen.at(frame); };
    const auto undo = [&at](std::size_t frame, bool performed)
    {
        const auto& counters = at(frame).actions;
        return (performed ? counters.performed : counters.declined).at(indexOf(Action::Undo));
    };
    return expect(at(9).itemActive && at(14).itemActive,
                  "le glissé : le champ est actif tant que le bouton est tenu") +
           expect(undo(14, false) == 1 && undo(14, true) == 0 && at(14).kilograms == 5.0F &&
                      at(14).done == 1,
                  "le glissé : Ctrl+Z est refusé, compté et dit, la pile ne bouge pas") +
           expect(undo(23, true) == 1 && at(23).kilograms == 0.0F,
                  "le champ relâché : Ctrl+Z défait");
}

// ---- Les gestes du vrai inspecteur (morceau A5) -------------------------------------------------

/// Où cliquer pour saisir les champs de l'inspecteur : un passage sans geste les relève.
std::vector<ImVec2> fieldsOf(const levain::tests::TestBackend& backend, Select select)
{
    const Run probe = play(backend, NoInput, 8, {.select = select});
    return probe.seen.size() == 8 ? probe.seen.back().centers : std::vector<ImVec2>{};
}

/// Un glissé : le curseur sur le champ à l'image `frame`, le bouton enfoncé à la suivante, puis
/// `moves` images où il avance de `step` pixels, et le bouton relâché.
std::string dragScript(int frame, ImVec2 at, int moves, float step)
{
    std::string script =
        std::format("{} mouse move {} {}\n{} button down left\n", frame, at.x, at.y, frame + 1);
    for (int i = 1; i <= moves; ++i)
    {
        script += std::format("{} mouse move {} {}\n", frame + 1 + i,
                              at.x + (step * static_cast<float>(i)), at.y);
    }
    return script + std::format("{} button up left\n", frame + 2 + moves);
}

/// Ctrl+clic sur un champ : il passe en saisie (le texte se tape par `Options::typed`).
std::string typeInto(int frame, ImVec2 at)
{
    return std::format("{} key down Left Ctrl\n{} mouse move {} {}\n{} button down left\n"
                       "{} button up left\n{} key up Left Ctrl\n",
                       frame, frame + 1, at.x, at.y, frame + 2, frame + 3, frame + 4);
}

/// Des morceaux de script qui se chevauchent (un glissé tenu pendant qu'un accord est joué), remis
/// dans l'ordre des images : le lecteur les veut croissantes.
std::string merged(std::initializer_list<std::string> parts)
{
    std::vector<std::pair<int, std::string>> lines;
    for (const std::string& part : parts)
    {
        std::size_t begin = 0;
        while (begin < part.size())
        {
            const std::size_t end = part.find('\n', begin);
            const std::string line = part.substr(begin, end - begin);
            lines.emplace_back(std::stoi(line), line);
            begin = end + 1;
        }
    }
    std::ranges::stable_sort(lines, {}, &std::pair<int, std::string>::first);
    std::string script;
    for (const auto& [frame, line] : lines)
    {
        script += line + "\n";
    }
    return script;
}

std::string pressKey(int frame, std::string_view key)
{
    return std::format("{} key down {}\n{} key up {}\n", frame, key, frame + 1, key);
}

int playInspectorDrag(const levain::tests::TestBackend& backend)
{
    const auto fields = fieldsOf(backend, Select::Tuning);
    if (fields.size() != 3)
    {
        return expect(false, "l'inspecteur : les trois champs de Tuning sont relevés");
    }
    // Deux glissés de suite sur le même champ : le premier de 30 images, le second de 10.
    const std::string script = dragScript(6, fields[0], 30, 2.0F) +
                               dragScript(42, fields[0], 10, 2.0F) + chord(60, {"Left Ctrl"}, "Z");
    const Run run = play(backend, script, 68, {.select = Select::Tuning});
    if (run.exitCode != 0 || run.seen.size() != 68)
    {
        return expect(false, "le glissé de l'inspecteur : code 0 et une image par image");
    }
    const auto at = [&run](std::size_t frame) -> const Seen& { return run.seen.at(frame); };
    return expect(at(20).itemActive && at(20).done == 0 && at(20).tuning.x > at(10).tuning.x,
                  "inspecteur : le monde est écrit en direct pendant le glissé, sans commande") +
           expect(at(36).done == 0, "inspecteur : 30 images de glissé, tenu, zéro commande") +
           expect(at(41).done == 1 && at(41).tuning.x > 0.5F && at(41).modified,
                  "inspecteur : 30 images de glissé font une commande") +
           expect(at(58).done == 2, "inspecteur : relâcher puis reprendre le champ en fait deux") +
           expect(at(66).done == 1 && at(66).undone == 1 && at(66).tuning.x == at(41).tuning.x,
                  "inspecteur : Ctrl+Z défait le second glissé en une étape");
}

int playInspectorCombo(const levain::tests::TestBackend& backend)
{
    const auto fields = fieldsOf(backend, Select::Tuning);
    if (fields.size() != 3)
    {
        return expect(false, "l'inspecteur : les trois champs de Tuning sont relevés");
    }
    const ImVec2 combo = fields[2];
    const Run opened = play(backend, clickScript(6, combo), 14, {.select = Select::Tuning});
    const Seen& open = opened.seen.back();
    if (opened.seen.size() != 14 || open.comboRow.x < 0.0F)
    {
        return expect(false, "la liste déroulante s'ouvre au clic et le banc relève ses lignes");
    }
    const ImVec2 first = open.comboRow;
    const ImVec2 second{first.x, first.y + open.rowStep};
    // Ouvrir puis refermer par Échap, rechoisir la constante déjà choisie : zéro commande ; choisir
    // une autre : une commande, que Ctrl+Z défait.
    const Run closed = play(backend, clickScript(6, combo) + pressKey(12, "Escape"), 18,
                            {.select = Select::Tuning});
    const Run same = play(backend, clickScript(6, combo) + clickScript(11, first), 20,
                          {.select = Select::Tuning});
    const Run chosen = play(
        backend, clickScript(6, combo) + clickScript(11, second) + chord(20, {"Left Ctrl"}, "Z"),
        28, {.select = Select::Tuning});
    if (closed.seen.size() != 18 || same.seen.size() != 20 || chosen.seen.size() != 28)
    {
        return expect(false, "la liste déroulante : une image relevée par image");
    }
    return expect(closed.seen.back().done == 0 && closed.seen.back().tuning.mood == Mood::Calm,
                  "inspecteur : une liste ouverte puis fermée sans choix, zéro commande") +
           expect(same.seen.back().done == 0 && same.seen.back().tuning.mood == Mood::Calm,
                  "inspecteur : la constante déjà choisie, rechoisie, zéro commande") +
           expect(chosen.seen.at(18).done == 1 && chosen.seen.at(18).tuning.mood == Mood::Wild,
                  "inspecteur : un choix dans la liste est une commande, sans aucun bord d'ImGui") +
           expect(chosen.seen.back().done == 0 && chosen.seen.back().tuning.mood == Mood::Calm,
                  "inspecteur : Ctrl+Z rend la constante d'avant");
}

/// Une valeur que `%.3f` écrit « 0.123 » : le piège d'Échap (la relecture d'A5).
constexpr float InexactValue = 0.12345678F;

int playInspectorTyping(const levain::tests::TestBackend& backend)
{
    const auto fields = fieldsOf(backend, Select::Tuning);
    if (fields.size() != 3)
    {
        return expect(false, "l'inspecteur : les trois champs de Tuning sont relevés");
    }
    // Tab de x à y : « 5 », Tab, « 7 », Entrée.
    const Run tab =
        play(backend, typeInto(6, fields[0]) + pressKey(18, "Tab") + pressKey(26, "Return"), 32,
             {.select = Select::Tuning, .typed = {{14, "5"}, {22, "7"}}});
    // Échap : « 9 » tapé, puis Échap rend la valeur, zéro commande.
    const Run escape = play(backend, typeInto(6, fields[0]) + pressKey(18, "Escape"), 24,
                            {.select = Select::Tuning, .typed = {{14, "9"}}});
    // Échap sur une valeur que « %.3f » n'écrit pas : ImGui rejoue « 0.123 », l'avant exact doit
    // revenir (0.12345678), pas l'arrondi.
    const Run escapeInexact =
        play(backend, typeInto(6, fields[0]) + pressKey(18, "Escape"), 24,
             {.select = Select::Tuning, .typed = {{14, "9"}}, .tuningX = InexactValue});
    // Retaper la même valeur : « 0 » sur 0, Entrée.
    const Run retyped = play(backend, typeInto(6, fields[0]) + pressKey(18, "Return"), 24,
                             {.select = Select::Tuning, .typed = {{14, "0"}}});
    if (tab.seen.size() != 32 || escape.seen.size() != 24 || escapeInexact.seen.size() != 24 ||
        retyped.seen.size() != 24)
    {
        return expect(false, "la saisie : une image relevée par image");
    }
    return expect(tab.seen.at(21).done == 1 && tab.seen.at(21).tuning.x == 5.0F,
                  "inspecteur : Tab de x à y ferme x, une commande") +
           expect(tab.seen.back().done == 2 && tab.seen.back().tuning.y == 7.0F &&
                      tab.seen.back().tuning.x == 5.0F,
                  "inspecteur : Tab de x à y, puis la fin de y : deux commandes") +
           expect(escape.seen.back().done == 0 && escape.seen.back().tuning.x == 0.0F,
                  "inspecteur : Échap rend la valeur, zéro commande") +
           expect(escapeInexact.seen.back().done == 0 &&
                      escapeInexact.seen.back().tuning.x == InexactValue &&
                      !escapeInexact.seen.back().modified,
                  "inspecteur : Échap sur 0.12345678 rend l'avant exact, zéro commande, scène "
                  "propre") +
           expect(retyped.seen.back().done == 0 && retyped.seen.back().tuning.x == 0.0F,
                  "inspecteur : retaper la même valeur, zéro commande");
}

/// Un glissé en cours quand Jouer commence : le geste est fermé d'abord (une commande),
/// l'instantané prend la valeur écrite, Stop la rend, Ctrl+Z défait le glissé.
int playInspectorBeforePlay(const levain::tests::TestBackend& backend, bool azerty)
{
    const std::string layout = azerty ? "AZERTY" : "QWERTY";
    const auto fields = fieldsOf(backend, Select::Tuning);
    if (fields.size() != 3)
    {
        return expect(false, "l'inspecteur : les trois champs de Tuning sont relevés");
    }
    // Le bouton tenu de l'image 7 à 38, Alt+P à l'image 16 (Jouer dès la suivante), Échap à 44.
    const std::string z = azerty ? "W as z" : "Z"; // la touche qui tape « z »
    const std::string script =
        merged({azerty ? "0 keymap changed\n" : "", dragScript(6, fields[0], 30, 2.0F),
                chord(16, {"Left Alt"}, "P"), pressKey(44, "Escape"), chord(52, {"Left Ctrl"}, z)});
    const Run run = play(backend, script, 60, {.azerty = azerty, .select = Select::Tuning});
    if (run.exitCode != 0 || run.seen.size() != 60)
    {
        return expect(false, layout + " Jouer pendant un glissé : code 0 et une image par image");
    }
    const auto at = [&run](std::size_t frame) -> const Seen& { return run.seen.at(frame); };
    std::size_t playing = 0;
    while (playing < run.seen.size() && at(playing).mode != levain::editor::Mode::Play)
    {
        ++playing;
    }
    if (playing >= 40)
    {
        return expect(false, layout + " Jouer pendant un glissé : le jeu commence");
    }
    const float written = at(playing).tuning.x;
    return expect(at(playing - 1).done == 0 && at(playing).done == 1 && written > 0.0F,
                  layout +
                      " Jouer pendant un glissé : le geste est fermé avant Jouer, une commande") +
           expect(at(42).done == 1,
                  layout + " Jouer pendant un glissé : la pile reste à une commande") +
           expect(
               at(50).mode == levain::editor::Mode::Edit && at(50).tuning.x == written &&
                   at(50).done == 1,
               layout +
                   " Jouer pendant un glissé : Stop rend la valeur écrite avant Jouer, la commande "
                   "reste") +
           expect(at(58).done == 0 && at(58).tuning.x == 0.0F,
                  layout + " Jouer pendant un glissé : Ctrl+Z défait le glissé tout entier");
}

/// Les angles tapés d'une rotation sont vidés par une annulation.
int playInspectorAngles(const levain::tests::TestBackend& backend)
{
    const auto fields = fieldsOf(backend, Select::Pose);
    if (fields.size() < 2)
    {
        return expect(false, "l'inspecteur : les champs d'un Transform sont relevés");
    }
    const std::string script = dragScript(6, fields[1], 20, 2.0F) + chord(36, {"Left Ctrl"}, "Z");
    const Run run = play(backend, script, 44, {.select = Select::Pose});
    if (run.exitCode != 0 || run.seen.size() != 44)
    {
        return expect(false, "les angles tapés : code 0 et une image par image");
    }
    const auto at = [&run](std::size_t frame) -> const Seen& { return run.seen.at(frame); };
    // Échap sur une rotation de départ que « %.3f » n'écrit pas (un quaternion tourné de angles
    // quelconques) : ImGui rejoue les angles arrondis, le quaternion d'avant doit revenir à
    // l'identique, les angles tapés vidés.
    const glm::quat rotated =
        levain::editor::rotationFromEuler({10.123456F, 20.654321F, 30.111111F});
    const Run escape =
        play(backend, typeInto(6, fields[1]) + pressKey(18, "Escape"), 24,
             {.select = Select::Pose, .typed = {{14, "9"}}, .poseRotation = rotated});
    if (escape.seen.size() != 24)
    {
        return expect(false, "les angles tapés : Échap, une image relevée par image");
    }
    const Seen& cancelled = escape.seen.back();
    return expect(cancelled.done == 0 && cancelled.rotation == rotated &&
                      cancelled.typedAngles == 0 && !cancelled.modified,
                  "angles : Échap sur une rotation rend le quaternion exact, zéro commande") +
           expect(at(32).done == 1 && at(32).typedAngles == 1 && at(32).rotation.w < 1.0F,
                  "angles : le glissé d'une rotation est une commande, l'angle est gardé") +
           expect(at(42).done == 0 && at(42).typedAngles == 0 && at(42).rotation.w == 1.0F,
                  "angles : Ctrl+Z rend la rotation et vide les angles tapés");
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
            std::println(stderr, "usage : levain_editor_undo [vulkan|d3d12|d3d12-warp]");
            return 2;
        }
        const int failures =
            playEdit(*backend, false) + playEdit(*backend, true) + playPlayThenStop(*backend) +
            playSimulate(*backend) + playPanelInPlay(*backend) + playMenu(*backend) +
            playDrag(*backend) + playInspectorDrag(*backend) + playInspectorCombo(*backend) +
            playInspectorTyping(*backend) + playInspectorBeforePlay(*backend, false) +
            playInspectorBeforePlay(*backend, true) + playInspectorAngles(*backend);
        return failures == 0 ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        std::fputs(e.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
}
