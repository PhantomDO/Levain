#include "levain/editor/shortcut_window.hpp"

#include <string>
#include <utility>

#include "levain/core/log.hpp"
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

constexpr float ChordButtonWidth = 190.0f;

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

/// Un bouton de largeur fixe, dont le texte change sans changer l'identifiant (`##`).
bool cellButton(const std::string& text, const char* id)
{
    return ImGui::Button((text + id).c_str(), {ChordButtonWidth, 0.0f});
}

void drawConflictBlock(ShortcutWindow& window, ShortcutTable& table,
                       const platform::KeyAtPosition& keyAtPosition, bool& changed)
{
    window.conflictShown.clear();
    if (!window.pending)
    {
        return;
    }
    for (const ChordConflict& conflict : window.pending->conflicts)
    {
        window.conflictShown +=
            (window.conflictShown.empty() ? "" : ", ") + conflictName(conflict, keyAtPosition);
    }
    ui::textf("Accord déjà pris, rien ne change : {}", window.conflictShown);
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
            resetAll(table);
            window.capturing.reset();
            window.pending.reset();
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
    if (pending.resets)
    {
        return resetAction(table, pending.target.action, keyAtPosition);
    }
    return replaceChord(table, pending.target.action, pending.target.slot, pending.chord,
                        keyAtPosition);
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
    resetAction(table, action, keyAtPosition);
    return true;
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
    // Assez haute pour les vingt lignes sans défiler à la première ouverture.
    ImGui::SetNextWindowPos({40.0f, 40.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({820.0f, 640.0f}, ImGuiCond_FirstUseEver);
    const bool visible = ImGui::Begin(ui::labelOf(ShortcutWindowTitle).c_str(), &window.open);
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
        const float footer = ImGui::GetFrameHeightWithSpacing() * 3.0f;
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
                ImGui::TextUnformatted(row.name.c_str());
                for (std::size_t slot = 0; slot < 2; ++slot)
                {
                    ImGui::TableNextColumn();
                    const bool capturingHere = window.capturing == ShortcutTarget{row.action, slot};
                    const std::string text =
                        capturingHere
                            ? std::string{ui::tr("Appuyez sur une touche... (Échap annule)")}
                            : chordCellText(binding->chords.at(slot), keyAtPosition);
                    // Hors de l'Édition les touches n'arrivent pas à la saisie : pas de cases à
                    // cliquer.
                    ImGui::BeginDisabled(!editing);
                    if (cellButton(text, slot == 0 ? "##c0" : "##c1"))
                    {
                        beginCapture(window, {row.action, slot});
                    }
                    ImGui::EndDisabled();
                    seen.chordAt.at(slot) = centerOfLastItem();
                }
                ImGui::TableNextColumn();
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
        if (window.message != nullptr)
        {
            ImGui::TextColored({1.0f, 0.75f, 0.2f, 1.0f}, "%s", ui::tr(window.message));
        }
        drawConflictBlock(window, table, keyAtPosition, changed);
        drawResetAll(window, table, changed);
    }
    ImGui::End();
    return changed;
}

} // namespace levain::editor
