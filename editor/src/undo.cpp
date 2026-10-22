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

/// Les assets que les deux valeurs d'un `MeshRef` désignent, ceux à épingler. Un autre composant
/// ne désigne rien en A4 (`ModelInstance` et `MeshCollider` viennent avec leurs gestes).
std::vector<assets::AssetId> assetsDesignatedBy(const flecs::world& world,
                                                flecs::entity_t component,
                                                const scene::ValueBox& before,
                                                const scene::ValueBox& after)
{
    if (component != world.id<assets::MeshRef>())
    {
        return {};
    }
    return {static_cast<const assets::MeshRef*>(before.data())->mesh.asset,
            static_cast<const assets::MeshRef*>(after.data())->mesh.asset};
}

/// Le décompte d'épingles du monde, absent d'un monde sans le module des assets (un test).
assets::AssetUsage* usageOf(flecs::world& world)
{
    return world.try_get_mut<assets::AssetUsage>();
}

/// La commande sort de la pile : elle rend ce qu'elle retient.
struct Releaser
{
    flecs::world& world;

    void operator()(ValueCommand& command) const
    {
        if (assets::AssetUsage* usage = usageOf(world))
        {
            assets::releasePin(*usage, command.pin);
        }
    }
};

void dropRedo(UndoStack& stack, flecs::world& world)
{
    // Un point d'enregistrement parmi les refaisables ne revient jamais : les numéros ne sont pas
    // réutilisés, `isModified` reste vrai sans rien tenir de plus.
    for (Command& command : stack.undone)
    {
        releaseCommand(world, command);
    }
    stack.undone.clear();
}

void pushOutOldest(UndoStack& stack, flecs::world& world)
{
    Command& oldest = stack.done.front();
    // L'état d'après elle devient le fond de la pile : annuler tout y ramène, et si c'était l'état
    // enregistré la scène y est propre (`topNumberOf`).
    stack.bottomNumber = oldest.number;
    releaseCommand(world, oldest);
    stack.done.erase(stack.done.begin());
}

/// Écrit l'avant (annuler) ou l'après (refaire) d'une commande de valeur. Une erreur dit pourquoi,
/// sans nom de geste : l'appelant le met devant.
struct Stepper
{
    flecs::world& world;
    bool redo;

    core::Result<void> operator()(const ValueCommand& command) const
    {
        const auto refuse = [](const char* why)
        {
            return core::makeError(core::ErrorCode::InvalidData,
                                   std::format("{}, le monde n'est pas touché", why));
        };
        if (ecs_is_deferred(world))
        {
            return refuse("appelée en mode différé");
        }
        if (!ecs_is_alive(world, command.entity))
        {
            return refuse("l'entité n'existe plus");
        }
        if (!ecs_has_id(world, command.entity, command.component))
        {
            return refuse("l'entité n'a plus le composant");
        }
        scene::setComponentValue(world, command.entity, command.component,
                                 (redo ? command.after : command.before).data());
        return {};
    }
};

/// Annule (`redo` faux) ou refait la commande du sommet de la liste d'où elle vient, et la range
/// dans l'autre. Échec : la commande est abandonnée (libérée), pour que la pile ne reste pas
/// bloquée sur elle. Un visiteur : une sorte de commande de plus ajoute sa surcharge ici.
core::Result<void> stepOnce(UndoStack& stack, flecs::world& world, bool redo)
{
    std::vector<Command>& from = redo ? stack.undone : stack.done;
    std::vector<Command>& to = redo ? stack.done : stack.undone;
    Command command = std::move(from.back());
    from.pop_back();
    auto written = std::visit(Stepper{world, redo}, command.kind);
    if (!written)
    {
        releaseCommand(world, command);
        return written;
    }
    to.push_back(std::move(command));
    return {};
}

/// Pose une commande au sommet : le numéro suivant, puis la plus ancienne sort si la pile dépasse
/// `UndoDepth`. Les refaisables sont déjà vidés par l'appelant (le contrat de la pile, avant que la
/// commande ne s'exécute).
void pushCommand(UndoStack& stack, flecs::world& world, CommandKind kind)
{
    stack.done.push_back({.number = stack.nextNumber++, .kind = std::move(kind)});
    if (stack.done.size() > UndoDepth)
    {
        pushOutOldest(stack, world);
    }
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

void releaseCommand(flecs::world& world, Command& command)
{
    std::visit(Releaser{world}, command.kind);
}

void clearUndoStack(UndoStack& stack, flecs::world& world)
{
    for (Command& command : stack.done)
    {
        releaseCommand(world, command);
    }
    for (Command& command : stack.undone)
    {
        releaseCommand(world, command);
    }
    stack.done.clear();
    stack.undone.clear();
    stack.savedNumber = 0;
    stack.bottomNumber = 0;
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
    // D'abord les refaisables, libérés : une commande qui s'exécute avec des pierres ou des
    // épingles de commandes supplantées encore en main volerait les siennes (tombstone.hpp).
    dropRedo(stack, world);
    assets::AssetPin pin;
    if (assets::AssetUsage* usage = usageOf(world))
    {
        pin = assets::pinAssets(*usage, assetsDesignatedBy(world, component, before, after));
    }
    pushCommand(stack, world,
                ValueCommand{.entity = entity,
                             .component = component,
                             .before = std::move(before),
                             .after = std::move(after),
                             .pin = std::move(pin)});
    return true;
}

core::Result<bool> editValue(UndoStack& stack, flecs::world& world, Mode mode,
                             flecs::entity_t entity, flecs::entity_t component, const void* value)
{
    const auto refuse = [&](const char* why)
    {
        return core::makeError(core::ErrorCode::InvalidData,
                               std::format("Modifier : {}, le monde n'est pas touché", why));
    };
    if (ecs_is_deferred(world))
    {
        return refuse("appelée en mode différé");
    }
    if (!ecs_is_alive(world, entity) || !ecs_has_id(world, entity, component))
    {
        return refuse("l'entité n'existe plus ou n'a pas le composant");
    }
    const flecs::entity_t type = ecs_get_typeid(world, component);
    if (type == 0)
    {
        return refuse("le composant n'a pas de valeur");
    }
    // Avant toute écriture : `sameValue` s'arrête sur une assertion pour un type qu'il ne sait pas
    // comparer, et la valeur serait déjà écrite.
    if (!scene::isComparableValue(world, type))
    {
        return refuse("le type du composant n'est pas décrit, il ne se compare pas");
    }
    // L'avant se copie avant l'écriture : `set` écrit tout de suite un composant déjà là.
    scene::ValueBox before{world, type, ecs_get_id(world, entity, component)};
    scene::setComponentValue(world, entity, component, value);
    scene::ValueBox after{world, type, ecs_get_id(world, entity, component)};
    return recordValueChange(stack, world, mode, entity, component, std::move(before),
                             std::move(after));
}

namespace
{

/// Une étape d'historique de la file : Annuler ou Rétablir.
void applyHistoryStep(RequestsReport& report, UndoStack& stack, flecs::world& world, Mode mode,
                      bool redo)
{
    const std::string name = ui::tr(redo ? "Rétablir" : "Annuler");
    switch (historyRefusalOf(stack, mode, redo))
    {
    case HistoryRefusal::NotEditing:
        ++report.refused;
        core::log("editor", core::LogLevel::Info, "{}", ui::trf("{} : seulement en Édition", name));
        return;
    case HistoryRefusal::Nothing:
        ++report.refused;
        core::log("editor", core::LogLevel::Info, "{}",
                  redo ? ui::trf("{} : rien à rétablir", name)
                       : ui::trf("{} : rien à annuler", name));
        return;
    case HistoryRefusal::None:
        break;
    }
    const auto stepped = stepOnce(stack, world, redo);
    if (!stepped)
    {
        ++report.failed;
        core::log("editor", core::LogLevel::Warning, "{}",
                  ui::trf("{} : commande abandonnée, {}", name, stepped.error().message));
        return;
    }
    ++(redo ? report.redone : report.undone);
}

} // namespace

RequestsReport applyEditorRequests(std::vector<EditorRequest>& queue, UndoStack& stack,
                                   flecs::world& world, Mode mode)
{
    RequestsReport report;
    if (queue.empty())
    {
        return report;
    }
    if (ecs_is_deferred(world))
    {
        core::log("editor", core::LogLevel::Error, "{}",
                  ui::tr("Historique : demande appliquée en mode différé, gardée pour plus tard"));
        return report;
    }
    for (const EditorRequest& request : std::exchange(queue, {}))
    {
        switch (request.kind)
        {
        case RequestKind::Undo:
        case RequestKind::Redo:
            applyHistoryStep(report, stack, world, mode, request.kind == RequestKind::Redo);
            break;
        }
    }
    return report;
}

} // namespace levain::editor
