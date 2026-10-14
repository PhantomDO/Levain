#include "levain/editor/shortcut_window.hpp"

#include <utility>

namespace levain::editor
{

namespace
{

// Les refus de la saisie, des clés du catalogue.
constexpr const char* ReservedMessage = "F1 est réservée : elle ouvre et ferme les panneaux";
constexpr const char* UnbindableMessage = "Cette touche ne peut pas être liée à un raccourci";
constexpr const char* KeymapMessage =
    "Le clavier a changé : l'accord en attente est abandonné, refaites la saisie";

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
        const CaptureResult captured = captureChord(press, defaultKindOf(target.action));
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
        window.message = nullptr;
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

} // namespace levain::editor
