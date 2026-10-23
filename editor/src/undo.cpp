#include "levain/editor/undo.hpp"

#include <algorithm>
#include <format>
#include <string>
#include <utility>

#include "levain/core/log.hpp"
#include "levain/scene/entity_name.hpp"
#include "levain/scene/reflection.hpp"
#include "levain/scene/subtree.hpp"
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

/// La commande sort de la pile : elle rend ce qu'elle retient. Les pierres d'une suppression
/// (ou d'une création défaite) se détruisent ici. Celles d'une commande dont l'entité est
/// vivante ne sont plus là : le réveil les vide (`stepSubtree`). Il le faut, car une suppression
/// plus récente de la même entité reçoit des pierres de mêmes identifiants, que cette commande,
/// plus ancienne, détruirait en sortant de la pile.
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

    void operator()(SubtreeCommand& command) const
    {
        if (auto released = scene::releaseTombstones(world, command.tombstones); !released)
        {
            core::log("editor", core::LogLevel::Warning, "{}",
                      ui::trf("Historique : pierre tombale gardée, {}", released.error().message));
        }
    }

    void operator()(RenameCommand&) const {}
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

/// Le préfixe d'un geste (« Supprimer : », « Annuler : ») que les messages de `scene` portent : la
/// console répète déjà « Annuler : commande abandonnée, … », il ne s'y met pas deux fois.
std::string withoutGestureVerb(std::string message)
{
    for (const std::string_view verb : {"Supprimer : ", "Annuler : ", "Rétablir : "})
    {
        if (message.starts_with(verb))
        {
            return message.substr(verb.size());
        }
    }
    return message;
}

/// Annule (`redo` faux) ou refait une commande, selon sa sorte : un visiteur, une sorte de commande
/// de plus ajoute sa surcharge ici. Une erreur dit pourquoi, sans nom de geste (l'appelant le met
/// devant).
struct Stepper
{
    flecs::world& world;
    bool redo;

    core::Result<StepEffect> operator()(ValueCommand& command) const
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
        return StepEffect{};
    }

    core::Result<StepEffect> operator()(SubtreeCommand& command) const
    {
        auto stepped = stepSubtree(world, command, redo);
        if (!stepped)
        {
            return core::makeError(stepped.error().code,
                                   withoutGestureVerb(std::move(stepped.error().message)));
        }
        return stepped;
    }

    core::Result<StepEffect> operator()(RenameCommand& command) const
    {
        auto stepped = stepRename(world, command, redo);
        if (!stepped)
        {
            return std::unexpected(stepped.error());
        }
        return StepEffect{};
    }
};

/// Annule (`redo` faux) ou refait la commande du sommet de la liste d'où elle vient, et la range
/// dans l'autre. Échec : la commande est abandonnée (libérée), pour que la pile ne reste pas
/// bloquée sur elle.
core::Result<StepEffect> stepOnce(UndoStack& stack, flecs::world& world, bool redo)
{
    std::vector<Command>& from = redo ? stack.undone : stack.done;
    std::vector<Command>& to = redo ? stack.done : stack.undone;
    Command command = std::move(from.back());
    from.pop_back();
    auto stepped = std::visit(Stepper{world, redo}, command.kind);
    if (!stepped)
    {
        releaseCommand(world, command);
        return stepped;
    }
    to.push_back(std::move(command));
    return stepped;
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

/// Le refus d'une opération de structure : une erreur nommée, le monde intact.
std::unexpected<core::Error> refuseStructure(std::string_view gesture, std::string_view why)
{
    return core::makeError(core::ErrorCode::InvalidData,
                           std::format("{} : {}, le monde n'est pas touché", gesture, why));
}

/// Les pierres d'une suppression sont posées (ou ne le sont pas) : la commande, ou rien, dit à la
/// console. Rend vrai si la commande est posée.
bool recordDeletion(UndoStack& stack, flecs::world& world, scene::SubtreeRecord record,
                    scene::Tombstones tombstones)
{
    if (!tombstones.notUndoable.empty())
    {
        core::log("editor", core::LogLevel::Warning, "{}",
                  ui::trf("Supprimer : ne s'annulera pas, {}", tombstones.notUndoable));
        return false;
    }
    pushCommand(stack, world,
                SubtreeCommand{.op = SubtreeOp::Delete,
                               .record = std::move(record),
                               .tombstones = std::move(tombstones)});
    return true;
}

} // namespace

core::Result<flecs::entity_t> createEntity(UndoStack& stack, flecs::world& world, Mode mode,
                                           flecs::entity_t parent, std::string_view wanted)
{
    if (ecs_is_deferred(world))
    {
        return refuseStructure(ui::tr("Créer"), ui::tr("appelée en mode différé"));
    }
    if (const auto refusal = createRefusalOf(world, parent))
    {
        return refuseStructure(ui::tr("Créer"), *refusal);
    }
    auto name = scene::uniqueNameAmong(world, parent, wanted.empty() ? NewEntityName : wanted);
    if (!name)
    {
        return refuseStructure(ui::tr("Créer"), name.error().message);
    }
    if (mode != Mode::Edit)
    {
        return makeEntity(world, parent, *name); // perdu à Stop : pas de commande
    }
    dropRedo(stack, world); // le contrat de la pile : avant que la commande ne s'exécute
    const flecs::entity_t entity = makeEntity(world, parent, *name);
    auto record = scene::readSubtree(world, entity);
    if (!record)
    {
        // Ne devrait pas arriver pour une entité qu'on vient de faire : on la défait plutôt que de
        // laisser une création que la pile ne saurait pas annuler.
        world.entity(entity).destruct();
        return std::unexpected(record.error());
    }
    pushCommand(
        stack, world,
        SubtreeCommand{.op = SubtreeOp::Create, .record = std::move(*record), .tombstones = {}});
    return entity;
}

core::Result<bool> deleteEntity(UndoStack& stack, flecs::world& world, Mode mode,
                                flecs::entity_t entity)
{
    if (ecs_is_deferred(world))
    {
        return refuseStructure(ui::tr("Supprimer"), ui::tr("appelée en mode différé"));
    }
    if (mode != Mode::Edit)
    {
        // `removeKeepingIndex` refuse toute entité de l'édition en jeu : un `ecs_delete` simple,
        // que Stop défait en réveillant l'entité avec son identifiant (décision 9, amendement).
        if (const auto refusal = plainDeleteRefusalOf(world, entity))
        {
            return refuseStructure(ui::tr("Supprimer"), *refusal);
        }
        world.entity(entity).destruct();
        return false;
    }
    auto record = scene::readSubtree(world, entity);
    if (!record)
    {
        return std::unexpected(record.error());
    }
    dropRedo(stack, world); // le contrat de la pile : avant que la commande ne s'exécute
    auto tombstones = scene::removeKeepingIndex(world, entity);
    if (!tombstones)
    {
        return std::unexpected(tombstones.error());
    }
    warnAboutLostChildren(*tombstones, ui::tr("Supprimer"));
    return recordDeletion(stack, world, std::move(*record), std::move(*tombstones));
}

core::Result<bool> renameEntity(UndoStack& stack, flecs::world& world, Mode mode,
                                flecs::entity_t entity, std::string_view wanted)
{
    if (ecs_is_deferred(world))
    {
        return refuseStructure(ui::tr("Renommer"), ui::tr("appelée en mode différé"));
    }
    if (const auto refusal = renameRefusalOf(world, entity, wanted))
    {
        return refuseStructure(ui::tr("Renommer"), *refusal);
    }
    const char* current = ecs_get_name(world, entity);
    if (current != nullptr && wanted == current)
    {
        return false; // le même nom n'est pas un changement
    }
    RenameCommand command{.entity = entity,
                          .before = current != nullptr ? std::optional<std::string>{current}
                                                       : std::nullopt,
                          .after = std::string{wanted}};
    if (mode == Mode::Edit)
    {
        dropRedo(stack, world);
    }
    writeName(world, entity, command.after);
    if (mode == Mode::Edit) // hors de l'Édition le nom est perdu à Stop, qui rend ceux d'avant
    {
        pushCommand(stack, world, std::move(command));
    }
    return true;
}

namespace
{

/// Dit à la console qu'une opération de structure a été refusée, et la compte.
void sayRefused(RequestsReport& report, const core::Error& error)
{
    ++report.refused;
    core::log("editor", core::LogLevel::Warning, "{}", error.message);
}

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
    if (stepped->revived != 0)
    {
        report.select = stepped->revived; // décision 8 : l'entité réveillée devient la sélection
    }
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
        case RequestKind::Create:
            if (const auto made = createEntity(stack, world, mode, request.entity, request.name))
            {
                ++report.created;
                report.select = *made;
                report.beginRename = *made;
            }
            else
            {
                sayRefused(report, made.error());
            }
            break;
        case RequestKind::Delete:
            if (const auto removed = deleteEntity(stack, world, mode, request.entity))
            {
                ++report.deleted;
            }
            else
            {
                sayRefused(report, removed.error());
            }
            break;
        case RequestKind::Rename:
            if (const auto renamed = renameEntity(stack, world, mode, request.entity, request.name))
            {
                report.renamed += *renamed ? 1 : 0;
            }
            else
            {
                sayRefused(report, renamed.error());
            }
            break;
        }
    }
    return report;
}

} // namespace levain::editor
