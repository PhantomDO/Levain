#include "levain/editor/undo.hpp"

#include <algorithm>
#include <format>
#include <string>
#include <utility>

#include "levain/core/log.hpp"
#include "levain/scene/reflection.hpp"
#include "levain/ui/tr.hpp"

namespace levain::editor
{

namespace
{

void dropRedo(UndoStack& stack)
{
    // Un point d'enregistrement parmi les refaisables ne revient jamais : les numéros ne sont pas
    // réutilisés, `isModified` reste vrai sans rien tenir de plus.
    stack.undone.clear();
}

void pushOutOldest(UndoStack& stack)
{
    Command& oldest = stack.done.front();
    // L'état d'après elle devient le fond de la pile : annuler tout y ramène, et si c'était l'état
    // enregistré la scène y est propre (`topNumberOf`).
    stack.bottomNumber = oldest.number;
    stack.done.erase(stack.done.begin());
}

/// Écrit l'avant (annuler) ou l'après (refaire) d'une commande de valeur.
struct Stepper
{
    flecs::world& world;
    bool redo;

    void operator()(const ValueCommand& command) const
    {
        scene::setComponentValue(world, command.entity, command.component,
                                 (redo ? command.after : command.before).data());
    }
};

/// Annule (`redo` faux) ou refait la commande du sommet de la liste d'où elle vient, et la range
/// dans l'autre. Un visiteur : une sorte de commande de plus ajoute sa surcharge ici.
void stepOnce(UndoStack& stack, flecs::world& world, bool redo)
{
    std::vector<Command>& from = redo ? stack.undone : stack.done;
    std::vector<Command>& to = redo ? stack.done : stack.undone;
    Command command = std::move(from.back());
    from.pop_back();
    std::visit(Stepper{world, redo}, command.kind);
    to.push_back(std::move(command));
}

} // namespace

bool canUndo(const UndoStack& stack)
{
    return !stack.done.empty();
}

bool canRedo(const UndoStack& stack)
{
    return !stack.undone.empty();
}

std::uint64_t topNumberOf(const UndoStack& stack)
{
    return stack.done.empty() ? stack.bottomNumber : stack.done.back().number;
}

bool isModified(const UndoStack& stack)
{
    return topNumberOf(stack) != stack.savedNumber;
}

void markSaved(UndoStack& stack)
{
    stack.savedNumber = topNumberOf(stack);
}

HistoryRefusal historyRefusalOf(const UndoStack& stack, Mode mode, bool redo)
{
    if (mode != Mode::Edit)
    {
        return HistoryRefusal::NotEditing;
    }
    return (redo ? canRedo(stack) : canUndo(stack)) ? HistoryRefusal::None
                                                    : HistoryRefusal::Nothing;
}

bool recordValueChange(UndoStack& stack, flecs::world& world, Mode mode, flecs::entity_t entity,
                       flecs::entity_t component, scene::ValueBox before, scene::ValueBox after)
{
    if (mode != Mode::Edit || scene::sameValue(world, before.type(), before.data(), after.data()))
    {
        return false;
    }
    dropRedo(stack);
    stack.done.push_back({.number = stack.nextNumber++,
                          .kind = ValueCommand{.entity = entity,
                                               .component = component,
                                               .before = std::move(before),
                                               .after = std::move(after)}});
    if (stack.done.size() > UndoDepth)
    {
        pushOutOldest(stack);
    }
    return true;
}

core::Result<bool> editValue(UndoStack& stack, flecs::world& world, Mode mode,
                             flecs::entity_t entity, flecs::entity_t component, const void* value)
{
    const flecs::entity_t type = ecs_get_typeid(world, component);
    // L'avant se copie avant l'écriture : `set` écrit tout de suite un composant déjà là.
    scene::ValueBox before{world, type, ecs_get_id(world, entity, component)};
    scene::setComponentValue(world, entity, component, value);
    scene::ValueBox after{world, type, ecs_get_id(world, entity, component)};
    return recordValueChange(stack, world, mode, entity, component, std::move(before),
                             std::move(after));
}

RequestsReport applyEditorRequests(std::vector<HistoryRequest>& queue, UndoStack& stack,
                                   flecs::world& world, Mode mode)
{
    RequestsReport report;
    for (const HistoryRequest request : std::exchange(queue, {}))
    {
        const bool redo = request == HistoryRequest::Redo;
        const std::string name = ui::tr(redo ? "Rétablir" : "Annuler");
        switch (historyRefusalOf(stack, mode, redo))
        {
        case HistoryRefusal::NotEditing:
            ++report.refused;
            core::log("editor", core::LogLevel::Info, "{}",
                      ui::trf("{} : seulement en Édition", name));
            continue;
        case HistoryRefusal::Nothing:
            ++report.refused;
            core::log("editor", core::LogLevel::Info, "{}",
                      redo ? ui::trf("{} : rien à rétablir", name)
                           : ui::trf("{} : rien à annuler", name));
            continue;
        case HistoryRefusal::None:
            break;
        }
        stepOnce(stack, world, redo);
        ++(redo ? report.redone : report.undone);
    }
    return report;
}

} // namespace levain::editor
