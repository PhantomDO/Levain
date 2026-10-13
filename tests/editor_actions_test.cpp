// Les actions de l'éditeur (ADR-0036, décisions 12 et 13, morceau 12) : leur disponibilité, ce
// qu'une action grisée dit à la console, et le temps réel qui ne se règle qu'en Édition.

#include <string>

#include <doctest/doctest.h>

#include "levain/core/log_ring.hpp"
#include "levain/editor/actions.hpp"

using namespace levain::editor;

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
