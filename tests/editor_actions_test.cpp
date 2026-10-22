// Les actions de l'éditeur (ADR-0036, décisions 12 et 13, morceau 12) : leur disponibilité, ce
// qu'une action grisée dit à la console, et les raccourcis qui les atteignent. Les raccourcis de
// lettre passent par `ImGui::Shortcut`, que ce banc fait tourner dans un ImGui sans fenêtre :
// Ctrl+S atteint son action, et un champ de texte actif garde son Ctrl+Z. Les touches d'outil
// viennent des événements bruts de l'image (`keyPressesOf`), comme dans la boucle.

#include <algorithm>
#include <array>
#include <bitset>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <doctest/doctest.h>
#include <imgui.h>

#include "levain/core/log_ring.hpp"
#include "levain/editor/actions.hpp"
#include "levain/platform/input_script.hpp"
#include "levain/platform/window.hpp"
#include "levain/ui/context.hpp"

using namespace levain::editor;
using levain::platform::KeyModifiers;
using levain::platform::UiEvent;
using levain::platform::UiEventType;

namespace
{

constexpr std::uint16_t ScanW = 26, ScanZ = 29, ScanR = 21, ScanSpace = 44;

/// Un événement de touche brut, tel que la fenêtre le rend.
UiEvent keyEvent(std::uint16_t scancode, std::uint32_t keycode, bool down,
                 KeyModifiers modifiers = {})
{
    return {.type = UiEventType::Key,
            .keycode = keycode,
            .scancode = scancode,
            .down = down,
            .modifiers = modifiers};
}

bool contains(const std::vector<Action>& actions, Action action)
{
    return std::ranges::find(actions, action) != actions.end();
}

} // namespace

TEST_CASE("une action grisée dit son milestone, les autres sont là")
{
    CHECK_FALSE(availabilityOf(Action::Save).available);
    CHECK(std::string{availabilityOf(Action::Save).milestone} == "M7.3");
    CHECK(availabilityOf(Action::Undo).available); // M7.3, A4 : la pile est là
    CHECK(availabilityOf(Action::Redo).available);
    for (const Action tool :
         {Action::ToolMove, Action::ToolRotate, Action::ToolScale, Action::NextTool})
    {
        CHECK_FALSE(availabilityOf(tool).available);
        CHECK(std::string{availabilityOf(tool).milestone} == "M7.4");
        CHECK(isToolAction(tool));
    }
    for (const Action here :
         {Action::Play, Action::Simulate, Action::Stop, Action::RealTime, Action::MoveForward})
    {
        CHECK(availabilityOf(here).available);
        CHECK(std::string{availabilityOf(here).milestone}.empty());
    }
    // Toute action grisée dit un milestone : un grisé sans date ne dirait pas quand il arrive.
    for (std::size_t i = 0; i < ActionCount; ++i)
    {
        const Availability availability = availabilityOf(static_cast<Action>(i));
        CHECK(availability.available == std::string{availability.milestone}.empty());
    }
}

TEST_CASE("atteindre une action grisée la compte et le dit à la console, rien d'autre ne change")
{
    levain::core::LogCapture capture = levain::core::captureLog(16);
    ActionCounters counters;
    bool realTime = true;
    const auto mode = runAction(Action::Save, counters, realTime, Mode::Edit);
    CHECK_FALSE(mode.has_value());
    CHECK(counters.declined.at(indexOf(Action::Save)) == 1);
    CHECK(counters.performed.at(indexOf(Action::Save)) == 0);
    CHECK(realTime);
    const levain::core::LogSnapshot snapshot = levain::core::snapshotOf(*capture.ring);
    REQUIRE(snapshot.entries.size() == 1);
    CHECK(snapshot.entries.front().text == "Enregistrer : arrive en M7.3");
}

TEST_CASE("Annuler et Rétablir : comptées faites en Édition, grisées et dites en jeu")
{
    levain::core::LogCapture capture = levain::core::captureLog(16);
    ActionCounters counters;
    bool realTime = true;
    for (const Action action : {Action::Undo, Action::Redo})
    {
        CHECK_FALSE(runAction(action, counters, realTime, Mode::Edit).has_value());
        CHECK(counters.performed.at(indexOf(action)) == 1);
        for (const Mode playing : {Mode::Play, Mode::Simulate})
        {
            CHECK_FALSE(runAction(action, counters, realTime, playing).has_value());
        }
        CHECK(counters.performed.at(indexOf(action)) == 1); // rien de plus en jeu
        CHECK(counters.declined.at(indexOf(action)) == 2);
    }
    const levain::core::LogSnapshot snapshot = levain::core::snapshotOf(*capture.ring);
    REQUIRE(snapshot.entries.size() == 4);
    CHECK(snapshot.entries.front().text == "Annuler : seulement en Édition");
    CHECK(snapshot.entries.back().text == "Rétablir : seulement en Édition");
}

TEST_CASE("Ctrl+Z pendant qu'un élément est actif est refusé et dit, Ctrl+S passe")
{
    levain::core::LogCapture capture = levain::core::captureLog(16);
    ActionCounters counters;
    const std::vector<Action> pressed{Action::Undo, Action::Save, Action::Redo};

    CHECK(withoutHistoryWhileActive(pressed, false, counters) == pressed);
    CHECK(counters.declined.at(indexOf(Action::Undo)) == 0);
    CHECK(withoutHistoryWhileActive(pressed, true, counters) == std::vector<Action>{Action::Save});
    CHECK(counters.declined.at(indexOf(Action::Undo)) == 1);
    CHECK(counters.declined.at(indexOf(Action::Redo)) == 1);
    CHECK(counters.declined.at(indexOf(Action::Save)) == 0);
    const levain::core::LogSnapshot snapshot = levain::core::snapshotOf(*capture.ring);
    REQUIRE(snapshot.entries.size() == 2);
    CHECK(snapshot.entries.front().text == "Annuler : refusé pendant qu'un champ est actif");
}

TEST_CASE("les actions qui sont là : le temps réel bascule, jouer et arrêter rendent leur mode")
{
    levain::core::LogCapture capture = levain::core::captureLog(16);
    ActionCounters counters;
    bool realTime = true;
    CHECK_FALSE(runAction(Action::RealTime, counters, realTime, Mode::Edit).has_value());
    CHECK_FALSE(realTime);
    CHECK_FALSE(runAction(Action::RealTime, counters, realTime, Mode::Edit).has_value());
    CHECK(realTime);
    CHECK(counters.performed.at(indexOf(Action::RealTime)) == 2);
    CHECK(runAction(Action::Play, counters, realTime, Mode::Edit) == Mode::Play);
    CHECK(runAction(Action::Stop, counters, realTime, Mode::Play) == Mode::Edit);
    // Simuler depuis l'Édition, et Échap en revient comme de Jouer.
    CHECK(runAction(Action::Simulate, counters, realTime, Mode::Edit) == Mode::Simulate);
    CHECK(runAction(Action::Stop, counters, realTime, Mode::Simulate) == Mode::Edit);
    CHECK(counters.performed.at(indexOf(Action::Simulate)) == 1);
    CHECK(levain::core::snapshotOf(*capture.ring).entries.empty()); // rien de grisé, rien de dit
}

TEST_CASE(
    "un changement de mode refusé par startPlay n'est pas une action faite : le compte passe à "
    "refusé")
{
    ActionCounters counters;
    bool realTime = true;
    REQUIRE(runAction(Action::Simulate, counters, realTime, Mode::Edit) == Mode::Simulate);
    CHECK(counters.performed.at(indexOf(Action::Simulate)) == 1);

    countRefusedTransition(counters, Action::Simulate);

    CHECK(counters.performed.at(indexOf(Action::Simulate)) == 0);
    CHECK(counters.declined.at(indexOf(Action::Simulate)) == 1);
    // Une autre action n'est pas touchée.
    CHECK(counters.performed.at(indexOf(Action::Stop)) == 0);
    CHECK(counters.declined.at(indexOf(Action::Stop)) == 0);
}

TEST_CASE("Jouer et Simuler ne partent que de l'Édition : en jeu elles sont refusées, comptées et "
          "dites")
{
    levain::core::LogCapture capture = levain::core::captureLog(16);
    ActionCounters counters;
    bool realTime = true;
    for (const Mode mode : {Mode::Play, Mode::Simulate})
    {
        CHECK_FALSE(runAction(Action::Simulate, counters, realTime, mode).has_value());
        // Alt+P en Simuler était muette quand Alt+S en Jouer était refusée : les deux se disent.
        CHECK_FALSE(runAction(Action::Play, counters, realTime, mode).has_value());
    }
    CHECK(counters.declined.at(indexOf(Action::Simulate)) == 2);
    CHECK(counters.performed.at(indexOf(Action::Simulate)) == 0);
    CHECK(counters.declined.at(indexOf(Action::Play)) == 2);
    CHECK(counters.performed.at(indexOf(Action::Play)) == 0);
    const levain::core::LogSnapshot snapshot = levain::core::snapshotOf(*capture.ring);
    REQUIRE(snapshot.entries.size() == 4);
    CHECK(snapshot.entries.at(0).text == "Simuler : seulement en Édition");
    CHECK(snapshot.entries.at(1).text == "Jouer : seulement en Édition");
}

TEST_CASE("une touche d'outil dans la Vue prend l'outil : en vol aucune (W vole, R et Espace se "
          "taisent), avec Ctrl ou dans un champ de texte non plus")
{
    const ShortcutTable table = defaultShortcuts();
    const KeyPress w{.scancode = ScanW, .keycode = 'w', .modifiers = {}};
    const ActiveContexts inView{.flight = false, .view = true, .game = false, .textInput = false};
    CHECK(toolActionOf(table, w, inView) == Action::ToolMove);
    // Sur un AZERTY la touche à la place du W tape « z » : c'est la position qui compte.
    CHECK(toolActionOf(table, {.scancode = ScanW, .keycode = 'z', .modifiers = {}}, inView) ==
          Action::ToolMove);

    ActiveContexts flying = inView;
    flying.flight = true;
    CHECK(actionFor(table, w, flying) == Action::MoveForward); // elle vole...
    CHECK_FALSE(toolActionOf(table, w, flying).has_value());   // ...et ne prend pas l'outil
    // R et Espace ne volent pas, mais en vol la Vue se tait : aucun changement d'outil.
    const KeyPress r{.scancode = ScanR, .keycode = 'r', .modifiers = {}};
    const KeyPress space{.scancode = ScanSpace, .keycode = ' ', .modifiers = {}};
    CHECK(toolActionOf(table, r, inView) == Action::ToolScale);
    CHECK(toolActionOf(table, space, inView) == Action::NextTool);
    CHECK_FALSE(toolActionOf(table, r, flying).has_value());
    CHECK_FALSE(toolActionOf(table, space, flying).has_value());

    const KeyPress ctrlW{.scancode = ScanW, .keycode = 'w', .modifiers = {.ctrl = true}};
    CHECK_FALSE(toolActionOf(table, ctrlW, inView).has_value());
    ActiveContexts typing = inView;
    typing.textInput = true;
    CHECK_FALSE(toolActionOf(table, w, typing).has_value());
    ActiveContexts elsewhere = inView;
    elsewhere.view = false;
    CHECK_FALSE(toolActionOf(table, w, elsewhere).has_value());
}

namespace
{

/// Un ImGui sans fenêtre où `shortcutsPressed` tourne chaque image, avec ou sans champ de texte.
/// Les accords de lettre passent par ImGui (`chord`), les touches d'outil par les événements bruts
/// de l'image (`tap`, lus par `keyPressesOf`), comme dans la boucle.
struct ShortcutBench
{
    const levain::ui::UiContext context = levain::ui::createUiContext(1.0f);
    ShortcutTable table = defaultShortcuts();
    levain::platform::Events raw; ///< Les événements bruts de la prochaine image.
    std::bitset<levain::platform::KeyCodeCount> held;
    std::vector<Action> reached;
    bool withField = false;
    bool textInput = false;
    bool flight = false;
    bool allowed = true;
    bool viewActive = true;   ///< `viewActiveIn` : faux quand une fenêtre couvre la Vue
    bool sceneActive = false; ///< `sceneKeysActiveIn` : la Hiérarchie ou la Vue a le focus
    bool editing = true;      ///< faux en Jouer : les outils ne valent plus
    std::array<char, 64> buffer{};

    void frame()
    {
        levain::ui::prepareUiFrame(ImGui::GetIO(), {.width = 640, .height = 480}, 1.0 / 60.0);
        ImGui::NewFrame();
        if (withField)
        {
            ImGui::SetNextWindowPos({0.0f, 0.0f});
            ImGui::SetNextWindowSize({300.0f, 100.0f});
            ImGui::Begin("champ");
            if (!ImGui::IsAnyItemActive())
            {
                ImGui::SetKeyboardFocusHere();
            }
            ImGui::InputText("##texte", buffer.data(), buffer.size());
            ImGui::End();
        }
        textInput = ImGui::GetIO().WantTextInput;
        const std::vector<KeyPress> presses = keyPressesOf(raw, held);
        for (const UiEvent& event : raw.ui)
        {
            if (event.type == UiEventType::Key)
            {
                held.set(event.scancode, event.down);
            }
        }
        raw = {};
        const auto now = shortcutsPressed({.table = table,
                                           .presses = presses,
                                           .editing = editing,
                                           .globalAllowed = allowed,
                                           .viewActive = viewActive,
                                           .flight = flight,
                                           .textInput = textInput,
                                           .sceneActive = sceneActive});
        reached.insert(reached.end(), now.begin(), now.end());
        ImGui::EndFrame();
    }

    void settle(int frames = 4)
    {
        for (int i = 0; i < frames; ++i)
        {
            frame();
        }
    }

    /// Appuie sur une touche d'ImGui (la lettre), modificateurs tenus avant elle, puis la relâche.
    void chord(ImGuiKey key, bool ctrl, bool shift = false)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiMod_Ctrl, ctrl);
        io.AddKeyEvent(ImGuiMod_Shift, shift);
        settle(2);
        io.AddKeyEvent(key, true);
        settle(3);
        io.AddKeyEvent(key, false);
        settle(2);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        io.AddKeyEvent(ImGuiMod_Shift, false);
        settle(2);
    }

    /// Appuie sur une touche par les événements bruts (la position et la lettre de la
    /// disposition), une image, puis la relâche.
    void tap(std::uint16_t scancode, std::uint32_t keycode, KeyModifiers modifiers = {})
    {
        raw.ui.push_back(keyEvent(scancode, keycode, true, modifiers));
        frame();
        raw.ui.push_back(keyEvent(scancode, keycode, false, modifiers));
        settle(2);
    }
};

} // namespace

TEST_CASE("Ctrl+S atteint son action, une seule fois : Ctrl+Maj+Z est Rétablir : sous une modale "
          "rien ne part")
{
    ShortcutBench bench;
    bench.settle();
    bench.chord(ImGuiKey_S, true);
    CHECK(std::ranges::count(bench.reached, Action::Save) == 1);
    CHECK(std::ranges::count(bench.reached, Action::Undo) == 0);

    bench.reached.clear();
    bench.chord(ImGuiKey_Z, true);
    CHECK(bench.reached == std::vector<Action>{Action::Undo});

    bench.reached.clear();
    bench.chord(ImGuiKey_Z, true, true);
    CHECK(bench.reached == std::vector<Action>{Action::Redo});

    bench.reached.clear();
    bench.allowed = false; // la modale « Quitter ? » est ouverte
    bench.chord(ImGuiKey_S, true);
    CHECK(bench.reached.empty());
}

TEST_CASE("Ctrl+R, dans la Vue, bascule le temps réel")
{
    ShortcutBench bench;
    bench.settle();
    bench.chord(ImGuiKey_R, true);
    CHECK(bench.reached == std::vector<Action>{Action::RealTime});
}

TEST_CASE(
    "Ctrl+R et les touches d'outil ne valent que dans une Vue active : une fenêtre par-dessus "
    "les prive de la souris")
{
    ShortcutBench bench;
    bench.viewActive = false; // la Vue est couverte (l'Aide, un menu) ou ni survolée ni focalisée
    bench.settle();
    bench.chord(ImGuiKey_R, true);
    bench.tap(ScanW, 'w');
    CHECK(bench.reached.empty());  // ni Temps réel ni outil
    bench.chord(ImGuiKey_S, true); // le global vaut partout
    CHECK(contains(bench.reached, Action::Save));

    bench.reached.clear();
    bench.viewActive = true;
    bench.chord(ImGuiKey_R, true);
    bench.tap(ScanW, 'w');
    CHECK(bench.reached == (std::vector<Action>{Action::RealTime, Action::ToolMove}));
}

TEST_CASE("viewActiveIn : panneaux fermés toujours, ouverts le focus ou le survol d'ImGui")
{
    CHECK(viewActiveIn(false, false, false));
    CHECK_FALSE(viewActiveIn(true, false, false));
    CHECK(viewActiveIn(true, true, false));
    CHECK(viewActiveIn(true, false, true));
}

TEST_CASE("le temps réel ne se règle qu'en Édition : jouer le force, revenir rend le choix")
{
    levain::core::LogCapture capture = levain::core::captureLog(16);
    ActionCounters counters;
    bool realTime = false; // décoché en Édition
    bool editChoice = true;
    switchRealTime(Mode::Edit, Mode::Play, realTime, editChoice);
    CHECK(realTime); // on joue : le monde bouge
    CHECK_FALSE(editChoice);
    // Ctrl+R ou la case en jeu : rien ne bascule, la console le dit.
    CHECK_FALSE(runAction(Action::RealTime, counters, realTime, Mode::Play).has_value());
    CHECK(realTime);
    CHECK(counters.declined.at(indexOf(Action::RealTime)) == 1);
    CHECK(counters.performed.at(indexOf(Action::RealTime)) == 0);
    const auto snapshot = levain::core::snapshotOf(*capture.ring);
    REQUIRE(snapshot.entries.size() == 1);
    CHECK(snapshot.entries.front().text == "Temps réel : seulement en Édition");
    switchRealTime(Mode::Play, Mode::Edit, realTime, editChoice);
    CHECK_FALSE(realTime); // le choix de l'Édition est revenu
    // Un choix coché reste coché au retour, et rester du même côté ne touche à rien.
    realTime = true;
    switchRealTime(Mode::Edit, Mode::Edit, realTime, editChoice);
    CHECK(realTime);
    CHECK_FALSE(editChoice);
}

TEST_CASE("un champ de texte actif garde son Ctrl+Z : l'éditeur n'annule rien")
{
    {
        ShortcutBench bench;
        bench.withField = true;
        bench.settle();
        REQUIRE(bench.textInput); // le champ est actif
        ImGui::GetIO().AddInputCharactersUTF8("abc");
        bench.settle();
        CHECK(std::string{bench.buffer.data()} == "abc");

        bench.chord(ImGuiKey_Z, true);
        CHECK_FALSE(contains(bench.reached, Action::Undo));
        // Le Ctrl+Z est allé au champ : son historique a défait la frappe.
        CHECK(std::string{bench.buffer.data()}.size() < 3);
        // Ctrl+Maj+Z aussi est le Rétablir du champ, non celui de l'éditeur.
        bench.chord(ImGuiKey_Z, true, true);
        CHECK_FALSE(contains(bench.reached, Action::Redo));
    }
    // Contre-épreuve : la même touche, sans champ, est bien l'Annuler de l'éditeur.
    ShortcutBench without;
    without.settle();
    without.chord(ImGuiKey_Z, true);
    CHECK(contains(without.reached, Action::Undo));
}

TEST_CASE("les touches d'outil lues dans les événements bruts : par position, jamais en vol ni "
          "avec Ctrl")
{
    // Un AZERTY : la touche à la place du W tape « z », celle à la place du Z tape « w ».
    ShortcutBench bench;
    bench.settle();
    bench.tap(ScanW, 'z');
    CHECK(bench.reached == std::vector<Action>{Action::ToolMove});

    bench.reached.clear();
    bench.flight = true; // le vol est en cours : la touche vole
    bench.tap(ScanW, 'z');
    CHECK(bench.reached.empty());

    bench.flight = false;
    bench.tap(ScanZ, 'w'); // la touche marquée W n'est plus à la place du W
    CHECK(bench.reached.empty());
    // Ctrl+Z : la lettre va à ImGui (Annuler), la position arrive avec Ctrl et ne prend pas
    // d'outil.
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    bench.settle(2);
    io.AddKeyEvent(ImGuiKey_Z, true);
    bench.tap(ScanW, 'z', {.ctrl = true});
    io.AddKeyEvent(ImGuiKey_Z, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    bench.settle(2);
    CHECK(bench.reached == std::vector<Action>{Action::Undo});
}

TEST_CASE("heldKeysAfter range les événements de l'image : un appui et un relâchement ne laissent "
          "rien, la fenêtre qui perd le focus oublie tout")
{
    std::bitset<levain::platform::KeyCodeCount> held;
    held.set(ScanR);
    levain::platform::Events events;
    events.ui = {keyEvent(ScanW, 'w', true),
                 keyEvent(ScanSpace, ' ', true),
                 keyEvent(ScanSpace, ' ', false),
                 keyEvent(ScanR, 'r', false),
                 {.type = UiEventType::MouseMoved, .x = 3.0f, .y = 4.0f}};
    const auto after = heldKeysAfter(events, held);
    CHECK(after.test(ScanW));
    CHECK_FALSE(after.test(ScanSpace)); // appuyée et relâchée dans l'image
    CHECK_FALSE(after.test(ScanR));     // relâchée
    CHECK(after.count() == 1);
    events.ui.insert(events.ui.begin() + 1, UiEvent{.type = UiEventType::FocusLost});
    const auto lost = heldKeysAfter(events, held); // tout oublié avant l'appui de l'espace
    CHECK_FALSE(lost.test(ScanR));
    CHECK(lost.none()); // le W d'avant le focus perdu aussi, l'espace relâché
    CHECK(heldKeysAfter({}, held) == held);
}

TEST_CASE("Pause, Image suivante et Rendre la souris : le contrôle que chaque action est")
{
    CHECK(playControlOf(Action::Pause) == PlayControl::TogglePause);
    CHECK(playControlOf(Action::NextFrame) == PlayControl::StepFrame);
    CHECK(playControlOf(Action::GiveMouseBack) == PlayControl::FreeMouse);
    for (const Action other : {Action::Play, Action::Stop, Action::RealTime, Action::Simulate})
    {
        CHECK_FALSE(playControlOf(other).has_value());
    }
    // Hors du jeu, aucun ne vaut ; en jeu, Image suivante veut la pause et les autres non.
    for (const PlayControl control :
         {PlayControl::TogglePause, PlayControl::StepFrame, PlayControl::FreeMouse})
    {
        CHECK(playControlRefusal(control, Mode::Edit, false) == PlayRefusal::NotPlaying);
        CHECK(playControlRefusal(control, Mode::Edit, true) == PlayRefusal::NotPlaying);
    }
    CHECK(playControlRefusal(PlayControl::StepFrame, Mode::Play, false) == PlayRefusal::NotPaused);
    CHECK(playControlRefusal(PlayControl::StepFrame, Mode::Play, true) == PlayRefusal::None);
    CHECK(playControlRefusal(PlayControl::TogglePause, Mode::Play, false) == PlayRefusal::None);
    CHECK(playControlRefusal(PlayControl::TogglePause, Mode::Play, true) == PlayRefusal::None);
    CHECK(playControlRefusal(PlayControl::FreeMouse, Mode::Play, false) == PlayRefusal::None);
    // Simuler : la pause et le pas comme en Jouer ; Maj+F1 n'a rien à rendre, la souris étant à
    // l'éditeur.
    CHECK(playControlRefusal(PlayControl::StepFrame, Mode::Simulate, false) ==
          PlayRefusal::NotPaused);
    CHECK(playControlRefusal(PlayControl::StepFrame, Mode::Simulate, true) == PlayRefusal::None);
    CHECK(playControlRefusal(PlayControl::TogglePause, Mode::Simulate, false) == PlayRefusal::None);
    CHECK(playControlRefusal(PlayControl::FreeMouse, Mode::Simulate, false) ==
          PlayRefusal::NotInPlay);
    CHECK(playControlRefusal(PlayControl::FreeMouse, Mode::Simulate, true) ==
          PlayRefusal::NotInPlay);
}

TEST_CASE(
    "atteindre un contrôle de jeu le compte s'il est fait, le dit à la console s'il est refusé")
{
    levain::core::LogCapture capture = levain::core::captureLog(16);
    ActionCounters counters;
    CHECK(runPlayControl(Action::Pause, counters, Mode::Play, false) == PlayControl::TogglePause);
    CHECK(runPlayControl(Action::Pause, counters, Mode::Play, true) == PlayControl::TogglePause);
    CHECK(runPlayControl(Action::NextFrame, counters, Mode::Play, true) == PlayControl::StepFrame);
    CHECK(runPlayControl(Action::GiveMouseBack, counters, Mode::Play, false) ==
          PlayControl::FreeMouse);
    CHECK(counters.performed.at(indexOf(Action::Pause)) == 2);
    CHECK(counters.performed.at(indexOf(Action::NextFrame)) == 1);
    CHECK(levain::core::snapshotOf(*capture.ring).entries.empty()); // rien de refusé, rien de dit

    // Image suivante en marche : refusée, comptée, dite.
    CHECK_FALSE(runPlayControl(Action::NextFrame, counters, Mode::Play, false).has_value());
    CHECK(counters.declined.at(indexOf(Action::NextFrame)) == 1);
    CHECK(counters.performed.at(indexOf(Action::NextFrame)) == 1);
    // Pause en Édition : refusée aussi.
    CHECK_FALSE(runPlayControl(Action::Pause, counters, Mode::Edit, false).has_value());
    CHECK(counters.declined.at(indexOf(Action::Pause)) == 1);
    // Maj+F1 en Simuler : refusée (la souris est déjà à l'éditeur), comptée, dite.
    CHECK_FALSE(runPlayControl(Action::GiveMouseBack, counters, Mode::Simulate, false).has_value());
    CHECK(counters.declined.at(indexOf(Action::GiveMouseBack)) == 1);
    CHECK(counters.performed.at(indexOf(Action::GiveMouseBack)) == 1);
    const levain::core::LogSnapshot snapshot = levain::core::snapshotOf(*capture.ring);
    REQUIRE(snapshot.entries.size() == 3);
    CHECK(snapshot.entries.at(0).text == "Image suivante : seulement en pause");
    CHECK(snapshot.entries.at(1).text == "Pause : seulement en jeu");
    CHECK(snapshot.entries.at(2).text == "Rendre la souris : seulement en Jouer");
    // Une action qui n'est pas un contrôle de jeu n'est ni comptée ni dite.
    CHECK_FALSE(runPlayControl(Action::Play, counters, Mode::Edit, false).has_value());
    CHECK(counters.performed.at(indexOf(Action::Play)) == 0);
    CHECK(counters.declined.at(indexOf(Action::Play)) == 0);
}

TEST_CASE("keyPressesOf rend la position, la lettre de la disposition et les modificateurs, et une "
          "répétition n'est pas un appui")
{
    const KeyModifiers shift{.shift = true};
    levain::platform::Events events;
    events.ui = {keyEvent(ScanW, 'z', true, shift),
                 keyEvent(ScanW, 'z', true, shift), // la répétition de SDL, touche tenue
                 {.type = UiEventType::MouseMoved, .x = 3.0f, .y = 4.0f},
                 keyEvent(ScanW, 'z', false, shift),
                 keyEvent(ScanW, 'z', true)}; // un nouvel appui, sans Maj
    const std::vector<KeyPress> presses = keyPressesOf(events, {});
    REQUIRE(presses.size() == 2);
    CHECK(presses.front().scancode == ScanW);
    CHECK(presses.front().keycode == 'z');
    CHECK(presses.front().modifiers == shift);
    CHECK(presses.back().modifiers == KeyModifiers{});

    // Tenue depuis l'image d'avant, la touche qui se répète n'est pas appuyée de nouveau.
    std::bitset<levain::platform::KeyCodeCount> held;
    held.set(ScanW);
    levain::platform::Events repeat;
    repeat.ui = {keyEvent(ScanW, 'z', true)};
    CHECK(keyPressesOf(repeat, held).empty());
    // La fenêtre qui perd le focus oublie les touches tenues : le prochain appui compte.
    repeat.ui.insert(repeat.ui.begin(), UiEvent{.type = UiEventType::FocusLost});
    CHECK(keyPressesOf(repeat, held).size() == 1);
    // Rien d'appuyé : aucune touche rendue.
    CHECK(keyPressesOf({}, {}).empty());
}

TEST_CASE(
    "Suppr et F2 valent où la sélection a le focus : ni en vol, ni dans un champ de texte, ni "
    "ailleurs")
{
    constexpr std::uint16_t ScanDelete = 76, ScanF2 = 59;
    constexpr std::uint32_t DeleteKey = 0x7F, F2Key = (1U << 30U) | 59U;
    ShortcutBench bench;
    bench.settle();
    bench.tap(ScanDelete, DeleteKey);
    bench.tap(ScanF2, F2Key);
    CHECK(bench.reached.empty()); // ni la Hiérarchie ni la Vue n'a le focus

    bench.sceneActive = true;
    bench.tap(ScanDelete, DeleteKey);
    bench.tap(ScanF2, F2Key);
    CHECK(bench.reached == (std::vector<Action>{Action::DeleteEntity, Action::RenameEntity}));

    bench.reached.clear();
    bench.flight = true; // le clic droit est tenu : la Vue se tait
    bench.tap(ScanDelete, DeleteKey);
    CHECK(bench.reached.empty());
    bench.flight = false;

    bench.withField = true; // un champ de texte actif garde ses touches
    bench.settle();
    bench.tap(ScanDelete, DeleteKey);
    bench.tap(ScanF2, F2Key);
    CHECK(bench.reached.empty());
    bench.withField = false;
    bench.settle();

    // Jouer : les outils ne valent plus, Suppr et F2 d'un panneau focalisé oui.
    bench.editing = false;
    bench.tap(ScanW, 'w');
    bench.tap(ScanDelete, DeleteKey);
    CHECK(bench.reached == std::vector<Action>{Action::DeleteEntity});
}

TEST_CASE("sceneKeysActiveIn : la Hiérarchie en tout mode, la Vue là où l'éditeur la possède")
{
    CHECK(sceneKeysActiveIn(true, false, Mode::Edit));
    CHECK(sceneKeysActiveIn(true, false, Mode::Play));
    CHECK(sceneKeysActiveIn(false, true, Mode::Edit));
    CHECK(sceneKeysActiveIn(false, true, Mode::Simulate));
    CHECK_FALSE(sceneKeysActiveIn(false, true, Mode::Play)); // le clavier de la Vue est au jeu
    CHECK_FALSE(sceneKeysActiveIn(false, false, Mode::Edit));
}

TEST_CASE("Créer, Supprimer et Renommer sont atteintes comme les autres actions, hors Jouer aussi")
{
    ActionCounters counters;
    bool realTime = true;
    for (const Mode mode : {Mode::Edit, Mode::Play, Mode::Simulate})
    {
        for (const Action action :
             {Action::CreateEntity, Action::DeleteEntity, Action::RenameEntity})
        {
            CHECK(availabilityOf(action).available);
            CHECK_FALSE(runAction(action, counters, realTime, mode).has_value());
        }
    }
    CHECK(counters.performed.at(indexOf(Action::DeleteEntity)) == 3);
    CHECK(counters.declined.at(indexOf(Action::DeleteEntity)) == 0);
    CHECK(isSceneAction(Action::DeleteEntity));
    CHECK(isSceneAction(Action::RenameEntity));
    CHECK_FALSE(isSceneAction(Action::CreateEntity));
}
