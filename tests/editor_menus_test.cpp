// Les menus, les barres et la fenêtre Aide de l'éditeur (ADR-0036, décisions 12 et 13, morceau 12),
// dans un ImGui sans fenêtre : la liste des raccourcis de l'Aide, la barre d'état, et les clics
// d'une souris simulée sur la barre de menus (une entrée grisée ne rend rien au clic) et sur la
// barre d'outils (la vitesse de la caméra bornée, la case « Temps réel »).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include <doctest/doctest.h>
#include <imgui.h>

#include "levain/core/log_ring.hpp"
#include "levain/editor/actions.hpp"
#include "levain/editor/bars.hpp"
#include "levain/editor/camera.hpp"
#include "levain/editor/menus.hpp"
#include "levain/editor/show.hpp"
#include "levain/editor/window_menu.hpp"
#include "levain/platform/input_script.hpp"
#include "levain/ui/context.hpp"
#include "levain/ui/style.hpp"
#include "levain/ui/tr.hpp"

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

/// L'état des modes, chaque champ nommé (les initialiseurs désignés les veulent tous).
ModeState modeIn(Mode mode, bool paused = false, bool mouseFreed = false)
{
    ModeState state;
    state.current = mode;
    state.paused = paused;
    state.mouseFreed = mouseFreed;
    return state;
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

/// Les barres dans un ImGui sans fenêtre, comme l'éditeur les dessine : menus, outils, état, Aide.
struct BarBench
{
    const levain::ui::UiContext context = levain::ui::createUiContext(1.0f);
    ShortcutTable table = defaultShortcuts();
    bool hierarchyOpen = true;
    std::array<WindowEntry, 1> windows{{{"Hiérarchie", &hierarchyOpen}}};
    WindowMenuLayout windowLayout;
    MenuBarLayout bar;
    HelpState help;
    ToolbarLayout toolbar;
    levain::core::LogRing ring{8};
    // Ce que les images ont rendu, accumulé.
    std::vector<Action> actions;
    bool quit = false;
    std::vector<Mode> modes;
    std::vector<float> speeds;
    // Ce que les barres reçoivent.
    Mode mode = Mode::Edit;
    bool paused = false;
    float flySpeed = DefaultFlySpeed;
    bool realTime = true;
    std::function<std::uint32_t(std::uint16_t)> layout = levain::platform::qwertyKeyAtPosition;

    /// `render` : `ImGui::Render` au lieu de `EndFrame`, pour lire ce que l'image a dessiné.
    void frame(bool render = false)
    {
        levain::ui::prepareUiFrame(ImGui::GetIO(), {.width = 960, .height = 540}, 1.0 / 60.0);
        ImGui::NewFrame();
        const MenuBarInputs inputs{.table = table, .keyAtPosition = layout, .windows = windows};
        const MenuRequests requests = drawMenuBar(inputs, windowLayout, bar, help);
        actions.insert(actions.end(), requests.actions.begin(), requests.actions.end());
        quit = quit || requests.quit;
        const ToolbarResult tools = drawToolbar(mode, paused, flySpeed, realTime, toolbar);
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
        drawHelp(help, inputs);
        if (render)
        {
            ImGui::Render();
        }
        else
        {
            ImGui::EndFrame();
        }
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

TEST_CASE("le menu Fichier : Enregistrer est grisé et ne fait rien au clic, Quitter demande")
{
    BarBench bench;
    bench.settle();
    REQUIRE(bench.bar.file.x > 0.0f);
    bench.click(bench.bar.file);
    REQUIRE(bench.bar.fileOpen);
    // Grisée : l'entrée montre son raccourci mais le clic ne l'atteint pas, et le menu reste
    // ouvert.
    bench.click(bench.bar.save);
    CHECK(bench.actions.empty());
    // Quitter l'éditeur est là.
    bench.click(bench.bar.quit);
    CHECK(bench.quit);
    CHECK(bench.actions.empty());
}

TEST_CASE("Édition et Entité s'ouvrent, leurs entrées grisées ne rendent rien au clic")
{
    BarBench bench;
    bench.settle();
    bench.click(bench.bar.edit);
    REQUIRE(bench.bar.editOpen);
    bench.click(bench.bar.undo); // Annuler arrive en M7.3
    CHECK(bench.actions.empty());
    CHECK_FALSE(bench.quit);
}

TEST_CASE("Entité > Créer… arrive en M7.3 : grisé, il ne rend rien au clic")
{
    BarBench bench;
    bench.settle();
    bench.click(bench.bar.entity);
    REQUIRE(bench.bar.entityOpen);
    bench.click(bench.bar.create);
    CHECK(bench.actions.empty());
}

TEST_CASE("le menu Fenêtre, maintenant dans la barre complète, ferme et rouvre sa fenêtre")
{
    BarBench bench;
    bench.settle();
    bench.click(bench.windowLayout.title);
    REQUIRE(bench.windowLayout.itemsShown);
    bench.click(bench.windowLayout.items.at(0));
    CHECK_FALSE(bench.hierarchyOpen);
}

TEST_CASE("Aide > Raccourcis ouvre la fenêtre, qui liste tout et filtre à la frappe")
{
    BarBench bench;
    bench.layout = azertyAt;
    bench.settle();
    CHECK(bench.help.total == 0); // fermée : rien dessiné
    bench.click(bench.bar.help);
    REQUIRE(bench.bar.helpOpen);
    bench.click(bench.bar.helpShortcuts);
    bench.settle();
    REQUIRE(bench.help.open);
    CHECK(bench.help.total == bench.table.bindings.size());
    CHECK(bench.help.shown.size() == bench.help.total);
    // Avancer s'y lit « Z » : la disposition est celle d'un AZERTY.
    const HelpRow* forward = rowOf(bench.help.shown, Action::MoveForward);
    REQUIRE(forward != nullptr);
    CHECK(forward->chords == "Z");

    // Chercher « enregistrer » dans le champ : une seule ligne reste.
    bench.click(bench.help.searchAt);
    ImGui::GetIO().AddInputCharactersUTF8("enregistrer");
    bench.settle();
    REQUIRE(bench.help.shown.size() == 1);
    CHECK(bench.help.shown.front().action == Action::Save);
    CHECK(bench.help.total == bench.table.bindings.size());
}

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
    CHECK(bench.modes.front() == Mode::Play);

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

TEST_CASE("la barre d'outils en jeu : Pause, Reprendre et Image suivante, le mode à sa place")
{
    BarBench bench;
    bench.settle();
    // Édition : ni Pause ni Image suivante, Jouer et Simuler (grisé) comme avant.
    CHECK(bench.toolbar.pause.x == 0.0f);
    CHECK(bench.toolbar.step.x == 0.0f);

    // Jeu : le bouton de mode (Arrêter) reste en tête, Pause vient après lui, puis Image suivante.
    bench.mode = Mode::Play;
    bench.settle();
    CHECK(bench.toolbar.pause.x > bench.toolbar.mode.x);
    CHECK(bench.toolbar.step.x > bench.toolbar.pause.x);
    bench.click(bench.toolbar.mode);
    REQUIRE(bench.modes.size() == 1);
    CHECK(bench.modes.back() == Mode::Edit); // Arrêter propose l'Édition

    // Le jeu tourne : Pause rend l'action Pause, Image suivante est éteinte et ne rend rien.
    bench.click(bench.toolbar.step);
    CHECK(bench.actions.empty());
    bench.click(bench.toolbar.pause);
    CHECK(bench.actions == std::vector<Action>{Action::Pause});

    // En pause : le même bouton dit Reprendre et rend la même action, Image suivante répond.
    bench.actions.clear();
    bench.paused = true;
    bench.settle();
    bench.click(bench.toolbar.step);
    CHECK(bench.actions == std::vector<Action>{Action::NextFrame});
    bench.click(bench.toolbar.pause);
    CHECK(bench.actions == std::vector<Action>{Action::NextFrame, Action::Pause});
}

namespace
{

/// Les sommets de la dernière image rendue, tous.
std::vector<ImDrawVert> drawnVertices()
{
    std::vector<ImDrawVert> all;
    for (const ImDrawList* list : ImGui::GetDrawData()->CmdLists)
    {
        all.insert(all.end(), list->VtxBuffer.begin(), list->VtxBuffer.end());
    }
    return all;
}

ImU32 packed(const ImVec4& color)
{
    return ImGui::ColorConvertFloat4ToU32(color);
}

bool sameRgb(ImU32 a, ImU32 b)
{
    return (a & ~static_cast<ImU32>(IM_COL32_A_MASK)) == (b & ~static_cast<ImU32>(IM_COL32_A_MASK));
}

/// Un bouton de la barre d'outils vu dans ce qu'elle a dessiné : les sommets de sa couleur de
/// contour (`Border`) et de son texte, dans sa largeur (le centre, de `ToolbarLayout`, et la place
/// du libellé plus le rembourrage de chaque côté).
struct ButtonDrawing
{
    int borderVertices = 0;
    int textVertices = 0;
    int translucentTextVertices = 0; ///< du texte grisé dessiné à moins de 100 %
};

ButtonDrawing drawingOf(const std::vector<ImDrawVert>& vertices, const ImVec2& center,
                        const std::string& label, ImU32 textColor)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const float half = (ImGui::CalcTextSize(label.c_str()).x / 2.0f) + style.FramePadding.x + 1.5f;
    const ImU32 border = packed(style.Colors[ImGuiCol_Border]);
    ButtonDrawing drawing;
    for (const ImDrawVert& vertex : vertices)
    {
        if (std::abs(vertex.pos.x - center.x) > half)
        {
            continue;
        }
        drawing.borderVertices += sameRgb(vertex.col, border) ? 1 : 0;
        if (sameRgb(vertex.col, textColor))
        {
            ++drawing.textVertices;
            drawing.translucentTextVertices += (vertex.col >> IM_COL32_A_SHIFT) != 255U ? 1 : 0;
        }
    }
    return drawing;
}

} // namespace

TEST_CASE("la barre d'outils prend la teinte du jeu quand il tourne, une autre en pause, aucune "
          "en Édition")
{
    BarBench bench;
    levain::ui::applyEditorStyle(ImGui::GetStyle(), 1.0f);
    const ImU32 playing = packed(levain::ui::tintedBarBackground(levain::ui::PlayTint));
    const ImU32 paused = packed(levain::ui::tintedBarBackground(levain::ui::PausedTint));
    const auto drawn = [&](ImU32 color)
    {
        return std::ranges::any_of(drawnVertices(), [color](const ImDrawVert& vertex)
                                   { return sameRgb(vertex.col, color); });
    };
    const auto settleAndRender = [&bench]
    {
        bench.settle();
        bench.frame(true);
    };
    settleAndRender();
    CHECK_FALSE(drawn(playing));
    CHECK_FALSE(drawn(paused));
    bench.mode = Mode::Play;
    settleAndRender();
    CHECK(drawn(playing));
    CHECK_FALSE(drawn(paused));
    bench.paused = true;
    settleAndRender();
    CHECK(drawn(paused));
    CHECK_FALSE(drawn(playing));
    // Retour à l'Édition : la teinte part avec le jeu.
    bench.mode = Mode::Edit;
    bench.paused = false;
    settleAndRender();
    CHECK_FALSE(drawn(playing));
    CHECK_FALSE(drawn(paused));
}

TEST_CASE("un bouton grisé de la barre d'outils : texte éteint plein, ni contour ni alpha de plus")
{
    BarBench bench;
    levain::ui::applyEditorStyle(ImGui::GetStyle(), 1.0f);
    bench.settle();
    bench.frame(true);
    const ImGuiStyle& style = ImGui::GetStyle();
    const std::vector<ImDrawVert> vertices = drawnVertices();
    const ImU32 dim = packed(style.Colors[ImGuiCol_TextDisabled]);

    // Le bouton « Simuler » : son texte est `TextDisabled` à 100 %, tel que le test des contrastes
    // le mesure (`BeginDisabled` y ajouterait `DisabledAlpha`, soit 2,6 de contraste à 0,6), et il
    // n'a pas de contour : il redessinerait la forme qu'on a retirée.
    const ButtonDrawing simulate =
        drawingOf(vertices, bench.toolbar.simulate, levain::ui::tr("Simuler"), dim);
    REQUIRE(simulate.textVertices > 0);
    CHECK(simulate.translucentTextVertices == 0);
    CHECK(simulate.borderVertices == 0);

    // Le contrôle du contrôle : le bouton qui répond, lui, a son contour, et le même relevé le
    // voit.
    const ButtonDrawing mode =
        drawingOf(vertices, bench.toolbar.mode, modeButtonLabel(otherMode(bench.mode)),
                  packed(style.Colors[ImGuiCol_Text]));
    CHECK(mode.borderVertices > 0);
}

TEST_CASE("une entrée de menu grisée est dessinée comme un bouton grisé : TextDisabled plein, "
          "libellé et raccourci, jamais le Text à DisabledAlpha")
{
    BarBench bench;
    levain::ui::applyEditorStyle(ImGui::GetStyle(), 1.0f);
    bench.settle();
    bench.click(bench.bar.edit);
    REQUIRE(bench.bar.editOpen);
    bench.frame(true);
    const ImGuiStyle& style = ImGui::GetStyle();
    // `MenuItem` grisé ouvre un `BeginDisabled` : le libellé (`Text`) sortait à `DisabledAlpha`,
    // 196 contre 215 sur la 4070, et l'entrée ne semblait pas grisée. Maintenant libellé et
    // raccourci sont du `TextDisabled` à 100 %.
    REQUIRE(style.DisabledAlpha < 1.0f); // le style garde son grisé pour les widgets d'ImGui
    int dimOpaque = 0;
    // L'alpha 0 est la coche invisible d'une entrée, que `MenuItem` dessine toujours : ni compté
    // ni fautif.
    int dimFaded = 0;
    int textFaded = 0;
    for (const ImDrawVert& vertex : drawnVertices())
    {
        const ImU32 vertexAlpha = vertex.col >> IM_COL32_A_SHIFT;
        dimOpaque += (sameRgb(vertex.col, packed(style.Colors[ImGuiCol_TextDisabled])) &&
                      vertexAlpha == 255U)
                         ? 1
                         : 0;
        dimFaded += (sameRgb(vertex.col, packed(style.Colors[ImGuiCol_TextDisabled])) &&
                     vertexAlpha > 0U && vertexAlpha < 255U)
                        ? 1
                        : 0;
        textFaded += (sameRgb(vertex.col, packed(style.Colors[ImGuiCol_Text])) &&
                      vertexAlpha > 0U && vertexAlpha < 255U)
                         ? 1
                         : 0;
    }
    CHECK(dimOpaque > 0);
    CHECK(textFaded == 0);
    CHECK(dimFaded == 0); // ni le grisé du style par-dessus
}

TEST_CASE("greyedMenuItem : le libellé et le raccourci en TextDisabled plein, jamais en Text")
{
    const levain::ui::UiContext context = levain::ui::createUiContext(1.0f);
    levain::ui::applyEditorStyle(ImGui::GetStyle(), 1.0f);
    for (int frame = 0; frame < 3; ++frame)
    {
        levain::ui::prepareUiFrame(ImGui::GetIO(), {.width = 640, .height = 480}, 1.0 / 60.0);
        ImGui::NewFrame();
        ImGui::SetNextWindowSize({300.0f, 100.0f});
        ImGui::Begin("Menu", nullptr, ImGuiWindowFlags_NoTitleBar);
        greyedMenuItem("Annuler", "Ctrl+Z");
        ImGui::End();
        ImGui::Render();
    }
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImU32 dim = packed(style.Colors[ImGuiCol_TextDisabled]);
    const ImU32 text = packed(style.Colors[ImGuiCol_Text]);
    int dimOpaque = 0;
    int anyText = 0;
    for (const ImDrawVert& vertex : drawnVertices())
    {
        dimOpaque += (sameRgb(vertex.col, dim) && (vertex.col >> IM_COL32_A_SHIFT) == 255U) ? 1 : 0;
        anyText += (sameRgb(vertex.col, text) && (vertex.col >> IM_COL32_A_SHIFT) != 0U) ? 1 : 0;
    }
    CHECK(dimOpaque > 0);
    CHECK(anyText == 0); // le libellé n'est pas en Text, à aucun alpha
}

TEST_CASE("le bandeau de la barre de mode se dessine sur un gris de la palette, presque opaque")
{
    const levain::ui::UiContext context = levain::ui::createUiContext(1.0f);
    levain::ui::applyEditorStyle(ImGui::GetStyle(), 1.0f);
    // Un enfant à la taille de son contenu n'a de taille qu'à la deuxième image.
    for (int frame = 0; frame < 3; ++frame)
    {
        levain::ui::prepareUiFrame(ImGui::GetIO(), {.width = 640, .height = 480}, 1.0 / 60.0);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize({400.0f, 300.0f});
        ImGui::Begin("Vue");
        std::ignore = drawModeBarOverlay(modeIn(Mode::Edit), {10.0f, 10.0f}, "Alt+P");
        ImGui::End();
        ImGui::Render();
    }
    const ImVec4 overlay = levain::ui::overlayBackground();
    const auto alpha = static_cast<ImU32>(std::lround(levain::ui::OverlayAlpha * 255.0f));
    const bool drawnOnPalette =
        std::ranges::any_of(drawnVertices(),
                            [&](const ImDrawVert& vertex)
                            {
                                return sameRgb(vertex.col, packed(overlay)) &&
                                       (vertex.col >> IM_COL32_A_SHIFT) == alpha;
                            });
    CHECK(drawnOnPalette);
    // Le rappel de touche (« Alt+P ») est en `Text`, non en `TextDisabled` : sur une scène claire
    // le fond du calque reste assez sombre pour le texte, pas pour un gris éteint. Rien d'autre
    // dans la barre n'est grisé.
    const ImU32 dim = packed(ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    CHECK(std::ranges::none_of(drawnVertices(),
                               [&](const ImDrawVert& vertex) { return sameRgb(vertex.col, dim); }));
}

TEST_CASE("le bandeau de la barre de mode dit la pause")
{
    // Le texte choisi : rien en Édition, la pause en pause, sinon ce que Jouer fait à la scène.
    CHECK(std::string_view{modeBannerOf(Mode::Edit, false)}.empty());
    CHECK(std::string_view{modeBannerOf(Mode::Edit, true)}.empty());
    const std::string_view playing = modeBannerOf(Mode::Play, false);
    const std::string_view paused = modeBannerOf(Mode::Play, true);
    CHECK(playing.starts_with("Jouer"));
    CHECK(paused.starts_with("Pause"));
}

TEST_CASE("la barre de mode en calque rend ses rectangles : la barre au coin demandé, son bouton "
          "dedans, à l'échelle 1 comme à 125 %")
{
    // À 125 % (le portable), les marges du style ne sont plus celles de l'échelle 1 : un clic écrit
    // en pixels y manquait le bouton (build/GOTCHA.md). La Vue place « Afficher » après la barre
    // (`ModeBarOverlay::bar`), un test vise le bouton (`ModeBarOverlay::button`).
    for (const float scale : {1.0f, 1.25f})
    {
        for (const Mode mode : {Mode::Edit, Mode::Play})
        {
            CAPTURE(scale);
            CAPTURE(modeTokenOf(mode));
            const levain::ui::UiContext context = levain::ui::createUiContext(scale);
            levain::ui::applyEditorStyle(ImGui::GetStyle(), scale);
            ModeBarOverlay overlay;
            float frameHeight = 0.0f;
            // Un enfant à la taille de son contenu n'a de taille qu'à la deuxième image.
            for (int frame = 0; frame < 3; ++frame)
            {
                levain::ui::prepareUiFrame(ImGui::GetIO(), {.width = 900, .height = 480},
                                           1.0 / 60.0);
                ImGui::NewFrame();
                ImGui::SetNextWindowPos({0.0f, 0.0f});
                ImGui::SetNextWindowSize({880.0f, 300.0f});
                ImGui::Begin("Vue");
                overlay = drawModeBarOverlay(modeIn(mode), {10.0f, 10.0f}, "Alt+P");
                frameHeight = ImGui::GetFrameHeight();
                ImGui::End();
                ImGui::Render();
            }
            const levain::app::ScreenRect& bar = overlay.bar;
            const levain::app::ScreenRect& button = overlay.button;
            // La barre est à 8 px du coin demandé, l'enfant entier, non le dernier item dedans.
            CHECK(bar.x == 18.0f);
            CHECK(bar.y == 18.0f);
            CHECK(bar.width > button.width);
            CHECK(bar.height > button.height);
            // Le bouton, non le rappel de la touche dessiné après lui : un item à cadre, dans la
            // barre.
            CHECK(button.height == doctest::Approx(frameHeight));
            CHECK(button.width > 0.0f);
            CHECK(button.x > bar.x);
            CHECK(button.y > bar.y);
            CHECK(button.x + button.width < bar.x + bar.width);
            CHECK(button.y + button.height < bar.y + bar.height);
            CHECK_FALSE(overlay.requested.has_value());
            CHECK_FALSE(overlay.hovered);
        }
    }
}

TEST_CASE("la barre « Afficher » : sur un gris de la palette, et le menu s'ouvre sous son bouton")
{
    const levain::ui::UiContext context = levain::ui::createUiContext(1.0f);
    levain::ui::applyEditorStyle(ImGui::GetStyle(), 1.0f);
    ShowState show;
    ShowMenuLayout layout;
    const auto frame = [&]
    {
        levain::ui::prepareUiFrame(ImGui::GetIO(), {.width = 640, .height = 480}, 1.0 / 60.0);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize({400.0f, 300.0f});
        ImGui::Begin("Vue");
        std::ignore = drawShowMenu(show, {10.0f, 10.0f}, "Alt+G", layout);
        ImGui::End();
        ImGui::Render();
    };
    for (int i = 0; i < 4; ++i)
    {
        frame();
    }
    const ImVec4 overlay = levain::ui::overlayBackground();
    CHECK(std::ranges::any_of(drawnVertices(),
                              [&](const ImDrawVert& vertex)
                              {
                                  return sameRgb(vertex.col, packed(overlay)) &&
                                         (vertex.col >> IM_COL32_A_SHIFT) ==
                                             static_cast<ImU32>(std::lround(overlay.w * 255.0f));
                              }));
    // Un clic sur « Afficher » : le menu s'ouvre, et sa première entrée est sous le bouton, non à
    // la souris (le défaut d'un popup), qui recouvrirait le bouton.
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 button = layout.button;
    io.AddMousePosEvent(button.x, button.y);
    frame();
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    frame();
    io.AddMouseButtonEvent(0, false);
    for (int i = 0; i < 4; ++i)
    {
        frame();
    }
    REQUIRE(layout.open);
    const float buttonHeight = ImGui::GetFrameHeight();
    CHECK(layout.gridItem.y - (buttonHeight / 2.0f) >= button.y + (buttonHeight / 2.0f) - 1.0f);
}
