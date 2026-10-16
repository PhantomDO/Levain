#include "levain/editor/shortcut_window.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "levain/core/log.hpp"
#include "levain/editor/actions.hpp"
#include "levain/editor/console.hpp"
#include "levain/editor/window_menu.hpp"
#include "levain/ui/tr.hpp"

namespace levain::editor
{

namespace
{

// Les refus de la saisie, des clés du catalogue.
constexpr const char* ReservedMessage = "F1 est réservée : elle ouvre et ferme les panneaux";
constexpr const char* UnbindableMessage = "Cette touche ne peut pas être liée à un raccourci";
// Une action tenue (le vol, Arrêter) ne lit aucun modificateur : l'accord est la touche nue, et la
// fenêtre le dit à côté du conflit qu'il fait peut-être (Maj+E pour « Avancer » est E, que « Monter
// » tient).
constexpr const char* ModifiersIgnoredMessage =
    "Cette action se tient comme une touche : ses accords n'ont pas de modificateur";
constexpr const char* KeymapMessage =
    "Le clavier a changé : l'accord en attente est abandonné, refaites la saisie";

// Les tailles, en multiples de la police (`ImGui::GetFontSize()`) : à 125 % de l'écran la police et
// le style grandissent, un nombre de pixels non. Les largeurs ont été prises sur la police de
// l'éditeur (Inter) : « Outil mettre à l'échelle » fait 9 polices, « Vol (clic droit tenu) » 7,5, «
// Réinitialiser » 6,5 plus ses marges ; le bouton d'accord montre « Appuyez sur une touche... » en
// entier.
constexpr float ChordButtonFonts = 13.0f;
constexpr float ActionColumnFonts = 10.0f;
constexpr float ContextColumnFonts = 9.0f;
constexpr float ResetColumnFonts = 7.5f;
// Le pied sous le tableau : un message, un conflit (sa phrase, ses deux boutons), « Tout
// réinitialiser ».
constexpr float FooterLines = 4.0f;
constexpr float Columns = 5.0f;
// Le marge de la fenêtre au bord de l'espace libre, en polices.
constexpr float WindowMarginFonts = 1.0f;

/// La console dit ce que « Remplacer » ou « Réinitialiser » a retiré à d'autres actions : rien ne
/// se déplace en silence.
void logRemoved(const std::vector<ChordConflict>& removed,
                const platform::KeyAtPosition& keyAtPosition)
{
    for (const ChordConflict& conflict : removed)
    {
        core::log("editor", core::LogLevel::Info, "raccourcis : {}",
                  ui::trf("{} retiré (conflit)", conflictName(conflict, keyAtPosition)));
    }
}

std::string chordCellText(const Chord& chord, const platform::KeyAtPosition& keyAtPosition)
{
    return isBound(chord) ? displayNameOf(chord, keyAtPosition) : std::string{ui::tr("(aucun)")};
}

/// Note l'action écrite, et celles qu'elle a dépouillées de leur accord (`removed`) : leur table
/// ne dit plus ce que leur ligne de fichier disait non plus.
void noteWritten(ShortcutWindow& window, Action action, const std::vector<ChordConflict>& removed)
{
    window.written.push_back(action);
    for (const ChordConflict& conflict : removed)
    {
        window.written.push_back(conflict.action);
    }
}

/// Un bouton de largeur fixe, dont le texte change sans changer l'identifiant (`##`).
bool cellButton(const std::string& text, const char* id, float width)
{
    return ImGui::Button((text + id).c_str(), {width, 0.0f});
}

void drawConflictBlock(ShortcutWindow& window, ShortcutTable& table,
                       const platform::KeyAtPosition& keyAtPosition, bool& changed)
{
    window.conflictShown.clear();
    window.conflictSentence.clear();
    if (!window.pending)
    {
        return;
    }
    for (const ChordConflict& conflict : window.pending->conflicts)
    {
        window.conflictShown +=
            (window.conflictShown.empty() ? "" : ", ") + conflictName(conflict, keyAtPosition);
    }
    // La phrase nomme l'action qui voudrait l'accord : « Annuler » à côté de « Ctrl+Z : Annuler »
    // (le bouton, puis l'action qui tient Ctrl+Z) se lisait de deux façons.
    const PendingChord& pending = *window.pending;
    const std::string target = ui::tr(nameOf(pending.target.action));
    const std::string sentence =
        pending.resets
            ? ui::trf("{} voudrait ses accords par défaut, déjà pris, rien ne change : {}", target,
                      window.conflictShown)
            : ui::trf("{} voudrait {}, déjà pris, rien ne change : {}", target,
                      displayNameOf(pending.chord, keyAtPosition), window.conflictShown);
    window.conflictSentence = sentence;
    ImGui::TextWrapped("%s", sentence.c_str());
    if (ImGui::Button(ui::labelOf("Remplacer").c_str()))
    {
        logRemoved(acceptPending(window, table, keyAtPosition), keyAtPosition);
        changed = true;
        return;
    }
    window.replaceAt = centerOfLastItem();
    ImGui::SameLine();
    if (ImGui::Button(ui::labelOf("Annuler", "conflit").c_str()))
    {
        window.pending.reset();
        window.message = nullptr; // la candidate est abandonnée, ce qu'on disait d'elle aussi
    }
    window.cancelAt = centerOfLastItem();
}

void drawResetAll(ShortcutWindow& window, ShortcutTable& table, bool& changed)
{
    if (ImGui::Button(ui::labelOf("Tout réinitialiser").c_str()))
    {
        window.resetAllAsked = true;
        // La confirmation est modale : une saisie qui continuerait dessous lirait ce qu'on y tape
        // (Ctrl+J devenait l'accord d'« Enregistrer », Entrée aurait lié une action de position).
        abandonCapture(window);
    }
    window.resetAllAt = centerOfLastItem();
    if (std::exchange(window.resetAllAsked, false))
    {
        ImGui::OpenPopup(ui::labelOf("Tout réinitialiser ?").c_str());
    }
    window.confirmShown = false;
    if (ImGui::BeginPopupModal(ui::labelOf("Tout réinitialiser ?").c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        window.confirmShown = true;
        ImGui::TextUnformatted(ui::tr("Tous les raccourcis reprennent leurs accords par défaut."));
        if (ImGui::Button(ui::labelOf("Réinitialiser").c_str()))
        {
            resetEverything(window, table);
            changed = true;
            ImGui::CloseCurrentPopup();
        }
        window.confirmAllAt = centerOfLastItem();
        ImGui::SameLine();
        if (ImGui::Button(ui::labelOf("Annuler", "tout").c_str()))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace

ShortcutWindowFit shortcutWindowFit(const ImGuiStyle& style, float fontSize, std::size_t rows,
                                    ImVec2 workSize)
{
    const float frameHeight = fontSize + (style.FramePadding.y * 2.0f);
    const float rowHeight = frameHeight + (style.CellPadding.y * 2.0f);
    const float headerHeight = fontSize + (style.CellPadding.y * 2.0f);
    const float line = frameHeight + style.ItemSpacing.y;
    const float chordWidth = fontSize * ChordButtonFonts;
    const float width = (fontSize * (ActionColumnFonts + ContextColumnFonts + ResetColumnFonts)) +
                        (2.0f * chordWidth) + (Columns * 2.0f * style.CellPadding.x) +
                        (2.0f * style.WindowPadding.x) + style.ScrollbarSize;
    // Le titre, les marges, le filtre, l'en-tête, les lignes, le pied ; une marge pour le contour
    // du tableau (qu'une ligne de moins ferait défiler d'une ligne : vérifié à 100 % et 125 %).
    const float height = frameHeight + (2.0f * style.WindowPadding.y) + line + headerHeight +
                         (static_cast<float>(rows) * rowHeight) + (FooterLines * line) +
                         style.ItemSpacing.y;
    return {.chordWidth = chordWidth,
            .size = {std::min(width, workSize.x), std::min(height, workSize.y)}};
}

void beginCapture(ShortcutWindow& window, ShortcutTarget target)
{
    window.capturing = target;
    window.pending.reset();
    window.message = nullptr;
}

void abandonCapture(ShortcutWindow& window)
{
    window.capturing.reset();
    window.pending.reset();
    window.message = nullptr;
}

bool stepCapture(ShortcutWindow& window, ShortcutTable& table,
                 const platform::KeyAtPosition& keyAtPosition, std::span<const KeyPress> presses)
{
    for (const KeyPress& press : presses)
    {
        if (!window.capturing)
        {
            return false;
        }
        const ShortcutTarget target = *window.capturing;
        const CaptureResult captured =
            captureChord(press, defaultKindOf(target.action), isHeldAction(target.action));
        switch (captured.outcome)
        {
        case CaptureOutcome::Cancel:
            window.capturing.reset();
            window.message = nullptr;
            return false;
        case CaptureOutcome::ModifierOnly:
            continue; // la touche qui suit
        case CaptureOutcome::Reserved:
            window.message = ReservedMessage;
            continue;
        case CaptureOutcome::Unbindable:
            window.message = UnbindableMessage;
            continue;
        case CaptureOutcome::Chord:
            break;
        }
        window.capturing.reset();
        window.message = captured.modifiersDropped ? ModifiersIgnoredMessage : nullptr;
        auto conflicts = conflictsOf(table, target.action, captured.chord, keyAtPosition);
        if (!conflicts.empty())
        {
            window.pending = PendingChord{
                .target = target, .chord = captured.chord, .conflicts = std::move(conflicts)};
            return false;
        }
        replaceChord(table, target.action, target.slot, captured.chord, keyAtPosition);
        noteWritten(window, target.action, {});
        return true;
    }
    return false;
}

std::vector<ChordConflict> acceptPending(ShortcutWindow& window, ShortcutTable& table,
                                         const platform::KeyAtPosition& keyAtPosition)
{
    const std::optional<PendingChord> taken = std::exchange(window.pending, std::nullopt);
    if (!taken)
    {
        return {};
    }
    const PendingChord& pending = *taken;
    const std::vector<ChordConflict> removed =
        pending.resets ? resetAction(table, pending.target.action, keyAtPosition)
                       : replaceChord(table, pending.target.action, pending.target.slot,
                                      pending.chord, keyAtPosition);
    noteWritten(window, pending.target.action, removed);
    return removed;
}

bool requestReset(ShortcutWindow& window, ShortcutTable& table, Action action,
                  const platform::KeyAtPosition& keyAtPosition)
{
    if (window.capturing && window.capturing->action == action)
    {
        window.capturing.reset();
    }
    auto taken = resetConflictsOf(table, action, keyAtPosition);
    if (!taken.empty())
    {
        window.pending = PendingChord{
            .target = {action, 0}, .chord = {}, .conflicts = std::move(taken), .resets = true};
        return false;
    }
    window.pending.reset();
    noteWritten(window, action, resetAction(table, action, keyAtPosition));
    return true;
}

void resetEverything(ShortcutWindow& window, ShortcutTable& table)
{
    resetAll(table);
    for (std::size_t action = 0; action < static_cast<std::size_t>(Action::Count); ++action)
    {
        window.written.push_back(static_cast<Action>(action));
    }
    window.capturing.reset();
    window.pending.reset();
}

void forgetWrittenLines(ShortcutWindow& window, std::vector<ShortcutLine>& kept)
{
    kept = withoutLinesOf(std::move(kept), std::exchange(window.written, {}));
}

void dropPendingAfterKeymapChange(ShortcutWindow& window)
{
    if (window.pending)
    {
        window.pending.reset();
        window.message = KeymapMessage;
    }
}

std::size_t logShortcutConflicts(const ShortcutTable& table,
                                 const platform::KeyAtPosition& keyAtPosition, const char* when)
{
    const std::vector<ConflictPair> pairs = conflictsIn(table, keyAtPosition);
    for (const ConflictPair& pair : pairs)
    {
        core::log("editor", core::LogLevel::Error, "{}",
                  ui::trf("raccourcis : conflit ({}) : {} et {}", ui::tr(when),
                          conflictName(pair.first, keyAtPosition),
                          ui::tr(nameOf(pair.second.action))));
    }
    return pairs.size();
}

bool shortcutRowMatches(const HelpRow& row, std::string_view search)
{
    return search.empty() || containsIgnoringAsciiCase(row.name, search) ||
           containsIgnoringAsciiCase(row.chords, search);
}

bool drawShortcutWindow(ShortcutWindow& window, ShortcutTable& table,
                        const platform::KeyAtPosition& keyAtPosition,
                        std::span<const KeyPress> presses, bool editing)
{
    window.rows.clear();
    window.conflictShown.clear();
    window.conflictSentence.clear();
    window.messageLine.clear();
    window.confirmShown = false;
    if (!window.open)
    {
        // Fermer la fenêtre abandonne la saisie : elle ne bloquerait plus les raccourcis pour rien.
        abandonCapture(window);
        window.focused = false;
        return false;
    }
    // La saisie ne lit le clavier qu'à la fenêtre qui l'a (le focus de la dernière image : celui de
    // celle-ci n'est connu qu'après `Begin`) : un champ de texte actif, une modale, un onglet
    // caché, une autre application l'arrêtent avant qu'une touche n'y devienne l'accord.
    const ImGuiIO& io = ImGui::GetIO();
    if (window.capturing && !captureAllowed({.editing = editing,
                                             .windowFocused = window.focused,
                                             .textInput = io.WantTextInput,
                                             .appFocused = !io.AppFocusLost}))
    {
        abandonCapture(window);
    }
    bool changed = stepCapture(window, table, keyAtPosition, presses);
    // Une première ouverture sous la barre d'outils (l'espace libre de la fenêtre principale, que
    // la barre de menus et les bandes ne couvrent pas), assez grande pour toutes les lignes sans
    // défiler, le tout selon la police : à 125 % de l'écran tout grandit avec elle.
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float margin = ImGui::GetFontSize() * WindowMarginFonts;
    const ShortcutWindowFit fit = shortcutWindowFit(
        ImGui::GetStyle(), ImGui::GetFontSize(), table.bindings.size(),
        {viewport->WorkSize.x - (2.0f * margin), viewport->WorkSize.y - (2.0f * margin)});
    ImGui::SetNextWindowPos({viewport->WorkPos.x + margin, viewport->WorkPos.y + margin},
                            ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(fit.size, ImGuiCond_FirstUseEver);
    const bool visible = ImGui::Begin(ui::labelOf(ShortcutWindowTitle).c_str(), &window.open);
    window.windowMin = ImGui::GetWindowPos();
    window.windowMax = {window.windowMin.x + ImGui::GetWindowSize().x,
                        window.windowMin.y + ImGui::GetWindowSize().y};
    window.focused = visible && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (!visible)
    {
        abandonCapture(window); // repliée ou derrière un onglet : plus rien ne la montre
    }
    if (visible)
    {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##filtre", ui::tr("Chercher une action ou un accord"),
                                 window.filter.data(), window.filter.size());
        window.filterAt = centerOfLastItem();
        constexpr auto Flags =
            static_cast<ImGuiTableFlags>(static_cast<unsigned>(ImGuiTableFlags_RowBg) |
                                         static_cast<unsigned>(ImGuiTableFlags_ScrollY) |
                                         static_cast<unsigned>(ImGuiTableFlags_SizingFixedFit));
        const float footer = ImGui::GetFrameHeightWithSpacing() * FooterLines;
        if (ImGui::BeginTable("##accords", 5, Flags, {0.0f, -footer}))
        {
            for (const char* column : {"Action", "Accord", "Accord 2", "Contexte", ""})
            {
                ImGui::TableSetupColumn(ui::labelOf(column).c_str());
            }
            ImGui::TableHeadersRow();
            for (const HelpRow& row : helpRowsOf(table, keyAtPosition))
            {
                if (!shortcutRowMatches(row, window.filter.data()))
                {
                    continue;
                }
                const Binding* binding = bindingOf(table, row.action);
                ShortcutRowSeen seen{
                    .action = row.action, .chordAt = {}, .resetAt = {}, .chords = row.chords};
                ImGui::PushID(static_cast<int>(row.action));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                // Le texte à la hauteur du texte des boutons de la ligne, pas au-dessus d'eux.
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(row.name.c_str());
                seen.nameAt = centerOfLastItem();
                for (std::size_t slot = 0; slot < 2; ++slot)
                {
                    ImGui::TableNextColumn();
                    const bool capturingHere = window.capturing == ShortcutTarget{row.action, slot};
                    const std::string text =
                        capturingHere ? std::string{ui::tr("Appuyez sur une touche...")}
                                      : chordCellText(binding->chords.at(slot), keyAtPosition);
                    // La saisie n'est que de l'Édition : Échap l'annule, et en Simuler (où les
                    // touches arrivent) elle arrêterait aussi la simulation, `gameKeysPressed` ne
                    // se taisant que sous un champ de texte ou la modale. Pas de cases à cliquer.
                    ImGui::BeginDisabled(!editing);
                    if (cellButton(text, slot == 0 ? "##c0" : "##c1", fit.chordWidth))
                    {
                        beginCapture(window, {row.action, slot});
                    }
                    ImGui::EndDisabled();
                    seen.chordAt.at(slot) = centerOfLastItem();
                }
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(row.context.c_str());
                ImGui::TableNextColumn();
                ImGui::BeginDisabled(isDefault(table, row.action));
                if (ImGui::Button(ui::labelOf("Réinitialiser").c_str()))
                {
                    changed = requestReset(window, table, row.action, keyAtPosition) || changed;
                }
                seen.resetAt = centerOfLastItem();
                ImGui::EndDisabled();
                ImGui::PopID();
                window.rows.push_back(std::move(seen));
            }
            ImGui::EndTable();
        }
        // Un refus ou une remarque en orange ; pendant la saisie, ce que fait Échap (dans le bouton
        // il ne tenait pas à 125 %).
        window.messageLine.clear();
        if (window.message != nullptr)
        {
            window.messageLine = ui::tr(window.message);
            ImGui::TextColored({1.0f, 0.75f, 0.2f, 1.0f}, "%s", window.messageLine.c_str());
        }
        else if (window.capturing)
        {
            window.messageLine = ui::tr("Échap annule la saisie");
            ImGui::TextDisabled("%s", window.messageLine.c_str());
        }
        else
        {
            // La ligne reste réservée : le bouton du dessous ne saute pas quand un message paraît.
            ImGui::Dummy({0.0f, ImGui::GetTextLineHeight()});
        }
        drawConflictBlock(window, table, keyAtPosition, changed);
        drawResetAll(window, table, changed);
    }
    ImGui::End();
    return changed;
}

} // namespace levain::editor
