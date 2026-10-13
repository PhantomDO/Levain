// Les actions de l'éditeur (ADR-0036, décisions 12 et 13, morceau 12) : leur disponibilité, ce
// qu'une action grisée dit à la console, le temps réel qui ne se règle qu'en Édition, et les
// touches d'outil, lues par position dans les événements bruts de l'image (`keyPressesOf`) : dans
// la Vue seulement, jamais en vol.

#include <bitset>
#include <cstdint>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "levain/core/log_ring.hpp"
#include "levain/editor/actions.hpp"
#include "levain/platform/window.hpp"

using namespace levain::editor;
using levain::platform::KeyModifiers;
using levain::platform::UiEvent;
using levain::platform::UiEventType;

namespace
{

constexpr std::uint16_t ScanW = 26, ScanR = 21, ScanSpace = 44;

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

} // namespace

TEST_CASE("une action grisée dit son milestone, les autres sont là")
{
    CHECK_FALSE(availabilityOf(Action::Save).available);
    CHECK(std::string{availabilityOf(Action::Save).milestone} == "M7.3");
    CHECK(std::string{availabilityOf(Action::Undo).milestone} == "M7.3");
    CHECK(std::string{availabilityOf(Action::Redo).milestone} == "M7.3");
    for (const Action tool :
         {Action::ToolMove, Action::ToolRotate, Action::ToolScale, Action::NextTool})
    {
        CHECK_FALSE(availabilityOf(tool).available);
        CHECK(std::string{availabilityOf(tool).milestone} == "M7.4");
        CHECK(isToolAction(tool));
    }
    CHECK(std::string{availabilityOf(Action::Simulate).milestone} == "M7.5");
    for (const Action here : {Action::Play, Action::Stop, Action::RealTime, Action::MoveForward})
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
    CHECK(runAction(Action::Play, counters, realTime, Mode::Edit) == Mode::PlayWithoutReturn);
    CHECK_FALSE(runAction(Action::Play, counters, realTime, Mode::PlayWithoutReturn).has_value());
    CHECK(runAction(Action::Stop, counters, realTime, Mode::PlayWithoutReturn) == Mode::Edit);
    CHECK(levain::core::snapshotOf(*capture.ring).entries.empty()); // rien de grisé, rien de dit
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
    switchRealTime(Mode::Edit, Mode::PlayWithoutReturn, realTime, editChoice);
    CHECK(realTime); // on joue : le monde bouge
    CHECK_FALSE(editChoice);
    // Ctrl+R ou la case en jeu : rien ne bascule, la console le dit.
    CHECK_FALSE(
        runAction(Action::RealTime, counters, realTime, Mode::PlayWithoutReturn).has_value());
    CHECK(realTime);
    CHECK(counters.declined.at(indexOf(Action::RealTime)) == 1);
    CHECK(counters.performed.at(indexOf(Action::RealTime)) == 0);
    const auto snapshot = levain::core::snapshotOf(*capture.ring);
    REQUIRE(snapshot.entries.size() == 1);
    CHECK(snapshot.entries.front().text == "Temps réel : seulement en Édition");
    switchRealTime(Mode::PlayWithoutReturn, Mode::Edit, realTime, editChoice);
    CHECK_FALSE(realTime); // le choix de l'Édition est revenu
    // Un choix coché reste coché au retour, et rester du même côté ne touche à rien.
    realTime = true;
    switchRealTime(Mode::Edit, Mode::Edit, realTime, editChoice);
    CHECK(realTime);
    CHECK_FALSE(editChoice);
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
