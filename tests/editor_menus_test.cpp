// Les barres et la fenêtre Aide de l'éditeur (ADR-0036, décisions 12 et 13, morceau 12), dans un
// ImGui sans fenêtre : la liste des raccourcis de l'Aide, la barre d'état, et les clics d'une
// souris simulée sur la barre d'outils (la vitesse de la caméra bornée, la case « Temps réel »).

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include <doctest/doctest.h>
#include <imgui.h>

#include "levain/core/log_ring.hpp"
#include "levain/editor/actions.hpp"
#include "levain/editor/bars.hpp"
#include "levain/editor/camera.hpp"
#include "levain/editor/menus.hpp"
#include "levain/platform/input_script.hpp"
#include "levain/ui/context.hpp"

using namespace levain::editor;

namespace
{

/// Un AZERTY : A et Q, W et Z échangés (comme tests/editor_shortcuts_test.cpp).
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

const HelpRow* rowOf(const std::vector<HelpRow>& rows, Action action)
{
    const auto found = std::ranges::find(rows, action, &HelpRow::action);
    return found == rows.end() ? nullptr : &*found;
}

} // namespace

TEST_CASE("l'Aide liste chaque entrée de la table, avec sa touche selon la disposition")
{
    const ShortcutTable table = defaultShortcuts();
    const auto qwerty = helpRowsOf(table, levain::platform::qwertyKeyAtPosition);
    const auto azerty = helpRowsOf(table, azertyAt);
    REQUIRE(qwerty.size() == table.bindings.size());
    CHECK(azerty.size() == table.bindings.size());
    // Aucune action n'est oubliée.
    for (std::size_t i = 0; i < ActionCount; ++i)
    {
        CHECK(rowOf(qwerty, static_cast<Action>(i)) != nullptr);
    }
    // Avancer : W en QWERTY, Z en AZERTY (la touche à la place du W).
    CHECK(rowOf(qwerty, Action::MoveForward)->chords == "W");
    CHECK(rowOf(azerty, Action::MoveForward)->chords == "Z");
    CHECK(rowOf(azerty, Action::MoveLeft)->chords == "Q");
    // Un accord de lettre reste la lettre : Ctrl+S ne change pas avec la disposition.
    CHECK(rowOf(azerty, Action::Save)->chords == "Ctrl+S");
    // Rétablir a deux accords.
    CHECK(rowOf(qwerty, Action::Redo)->chords == "Ctrl+Y / Ctrl+Maj+Z");
    CHECK(rowOf(qwerty, Action::MoveForward)->context == "Vol (clic droit tenu)");
    CHECK(rowOf(qwerty, Action::Save)->context == "Partout");
    // Les touches d'Unreal et d'Unity, de la table des gestes.
    CHECK(rowOf(qwerty, Action::Play)->unreal == "Alt+P");
    CHECK(rowOf(qwerty, Action::Play)->unity == "Ctrl+P");
}

TEST_CASE("la recherche de l'Aide trouve par le nom, la touche, le contexte ou l'autre moteur")
{
    const auto rows = helpRowsOf(defaultShortcuts(), levain::platform::qwertyKeyAtPosition);
    const auto count = [&rows](std::string_view search)
    {
        return std::ranges::count_if(rows, [&](const HelpRow& row)
                                     { return helpRowMatches(row, search); });
    };
    CHECK(count("") == static_cast<std::ptrdiff_t>(rows.size()));
    CHECK(count("zzzzzz") == 0);
    CHECK(count("enregistrer") == 1);                            // la casse ne compte pas
    CHECK(count("ctrl+s") == 1);                                 // la touche
    CHECK(count("partout") >= 3);                                // le contexte
    CHECK(helpRowMatches(*rowOf(rows, Action::Play), "ctrl+p")); // la colonne Unity
}

TEST_CASE("la barre d'état dit le dernier message et les compteurs")
{
    levain::core::LogRing ring{8};
    CHECK(statusOf(ring).text.empty());
    using levain::core::LogLevel;
    levain::core::pushLogEntry(
        ring, {.level = LogLevel::Info, .category = "t", .text = "un\ndeux", .seconds = 0.0});
    levain::core::pushLogEntry(
        ring, {.level = LogLevel::Warning, .category = "t", .text = "lent", .seconds = 0.0});
    levain::core::pushLogEntry(
        ring, {.level = LogLevel::Error, .category = "t", .text = "panne", .seconds = 0.0});
    levain::core::pushLogEntry(
        ring, {.level = LogLevel::Error, .category = "t", .text = "encore", .seconds = 0.0});
    const StatusLine line = statusOf(ring);
    CHECK(line.text == "encore");
    CHECK(line.severity == levain::core::LogSeverity::Error);
    CHECK(line.counts.warnings == 1);
    CHECK(line.counts.errors == 2);
    levain::core::pushLogEntry(
        ring, {.level = LogLevel::Info, .category = "t", .text = "a\nb", .seconds = 0.0});
    CHECK(statusOf(ring).text == "a | b"); // une seule ligne
    levain::core::clearLogRing(ring);
    CHECK(statusOf(ring).text.empty());
    CHECK(statusOf(ring).counts.errors == 0);
}

namespace
{

/// Les barres dans un ImGui sans fenêtre, comme l'éditeur les dessine : outils et état.
struct BarBench
{
    const levain::ui::UiContext context = levain::ui::createUiContext(1.0f);
    ToolbarLayout toolbar;
    levain::core::LogRing ring{8};
    // Ce que les images ont rendu, accumulé.
    std::vector<Action> actions;
    std::vector<Mode> modes;
    std::vector<float> speeds;
    // Ce que les barres reçoivent.
    Mode mode = Mode::Edit;
    float flySpeed = DefaultFlySpeed;
    bool realTime = true;

    void frame()
    {
        levain::ui::prepareUiFrame(ImGui::GetIO(), {.width = 960, .height = 540}, 1.0 / 60.0);
        ImGui::NewFrame();
        const ToolbarResult tools = drawToolbar(mode, flySpeed, realTime, toolbar);
        actions.insert(actions.end(), tools.actions.begin(), tools.actions.end());
        if (tools.mode)
        {
            modes.push_back(*tools.mode);
        }
        if (tools.flySpeed)
        {
            speeds.push_back(*tools.flySpeed);
            flySpeed = *tools.flySpeed;
        }
        drawStatusBar(statusOf(ring));
        ImGui::EndFrame();
    }

    void settle(int frames = 4)
    {
        for (int i = 0; i < frames; ++i)
        {
            frame();
        }
    }

    void click(ImVec2 at)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(at.x, at.y);
        settle(4);
        io.AddMouseButtonEvent(0, true);
        settle(4);
        io.AddMouseButtonEvent(0, false);
        settle(4);
    }
};

} // namespace

TEST_CASE("la barre d'outils : le mode, la vitesse bornée, Temps réel, et des outils grisés")
{
    BarBench bench;
    bench.settle();
    // Les outils et Simuler sont grisés : le clic ne rend rien.
    bench.click(bench.toolbar.tool);
    bench.click(bench.toolbar.simulate);
    CHECK(bench.actions.empty());
    CHECK(bench.modes.empty());
    CHECK(bench.speeds.empty());

    // Le bouton de mode propose Jouer depuis l'Édition.
    bench.click(bench.toolbar.mode);
    REQUIRE(bench.modes.size() == 1);
    CHECK(bench.modes.front() == Mode::PlayWithoutReturn);

    // Temps réel : la case rend l'action, comme Ctrl+R.
    bench.click(bench.toolbar.realTime);
    CHECK(bench.actions == std::vector<Action>{Action::RealTime});

    // La vitesse : un clic vers le bout droit du curseur donne une vitesse bornée, plus grande.
    const ImVec2 farRight{bench.toolbar.speed.x + bench.toolbar.speedWidth * 0.45f,
                          bench.toolbar.speed.y};
    bench.click(farRight);
    REQUIRE_FALSE(bench.speeds.empty());
    CHECK(bench.speeds.back() > DefaultFlySpeed);
    CHECK(bench.speeds.back() <= MaxFlySpeed);
    CHECK(bench.flySpeed == bench.speeds.back());
}
