#include "levain/editor/undo_session.hpp"

#include <format>
#include <utility>

#include "levain/core/log.hpp"
#include "levain/scene/components.hpp"
#include "levain/scene/reflection.hpp"
#include "levain/ui/tr.hpp"

namespace levain::editor
{

namespace
{

/// Le 1er, le 2e… changement : la position X de l'entité devient ce numéro, une valeur que rien
/// d'autre n'écrit, donc un état différent à chaque niveau de la pile.
scene::Transform transformAfterChange(const scene::Transform& current, int number)
{
    scene::Transform changed = current;
    changed.position.x = static_cast<float>(number);
    return changed;
}

/// Une demande d'annulation, par la file de l'éditeur (la porte de Ctrl+Z) ; vrai si elle a défait.
bool undoOnce(UndoStack& stack, flecs::world& world, Mode mode)
{
    std::vector<EditorRequest> queue{RequestKind::Undo};
    return applyEditorRequests(queue, stack, world, mode).undone == 1;
}

} // namespace

core::Result<UndoSession> startUndoSession(const flecs::world& world, const std::string& entityName)
{
    const flecs::entity entity = world.lookup(entityName.c_str());
    if (!entity || !entity.has<scene::Transform>())
    {
        return core::makeError(
            core::ErrorCode::InvalidData,
            std::format("--undo-session : aucune entité « {} » à Transform", entityName));
    }
    return UndoSession{.entity = entity.id()};
}

namespace
{

/// Le bilan d'une session qui s'arrête : un refus ne se retente pas jusqu'à la fin des pas, il se
/// dit à la première image avec les comptes d'alors et sa cause (règle n°7). Une différence au
/// moins : la ligne tombe sur la branche rouge de la CI.
UndoSessionReport refusedReport(UndoSession& session, std::string reason)
{
    session.reported = true;
    core::log("editor", core::LogLevel::Error, "{}", reason);
    return UndoSessionReport{.changes = session.changes,
                             .undone = session.undone,
                             .differences = 1,
                             .differencesText = std::move(reason)};
}

} // namespace

std::optional<UndoSessionReport> stepUndoSession(UndoSession& session, UndoStack& stack,
                                                 flecs::world& world, Mode mode)
{
    if (session.reported)
    {
        return std::nullopt;
    }
    const flecs::entity entity = world.entity(session.entity);
    if (session.changes >= UndoSessionReference && !session.reference)
    {
        auto taken = scene::takeSnapshot(world);
        if (!taken)
        {
            core::log("editor", core::LogLevel::Error, "{}", taken.error().message);
            session.reported = true;
            return UndoSessionReport{.changes = session.changes,
                                     .undone = session.undone,
                                     .differences = 1,
                                     .differencesText = taken.error().message};
        }
        session.reference = std::move(*taken);
    }
    if (session.changes < UndoSessionChanges)
    {
        const auto* current = entity.try_get<scene::Transform>();
        if (current == nullptr)
        {
            return refusedReport(
                session, std::format("changement {} refusé : l'entité n'a plus de Transform",
                                     session.changes + 1));
        }
        const scene::Transform value = transformAfterChange(*current, session.changes + 1);
        const auto recorded =
            editValue(stack, world, mode, session.entity, world.id<scene::Transform>(), &value);
        if (!recorded)
        {
            return refusedReport(session,
                                 std::format("changement {} refusé : {}", session.changes + 1,
                                             recorded.error().message));
        }
        if (!*recorded)
        {
            return refusedReport(
                session,
                std::format("changement {} sans commande (égal à lui-même)", session.changes + 1));
        }
        ++session.changes;
        return std::nullopt;
    }
    if (session.undone < UndoSessionUndos)
    {
        if (!undoOnce(stack, world, mode))
        {
            return refusedReport(
                session, std::format("annulation {} refusée : la pile n'a plus rien à défaire",
                                     session.undone + 1));
        }
        ++session.undone;
        return std::nullopt;
    }
    // Les 100 annulations sont faites à l'image d'avant : la comparaison voit le monde posé.
    session.reported = true;
    if (!session.reference)
    {
        return UndoSessionReport{.changes = session.changes,
                                 .undone = session.undone,
                                 .differences = 1,
                                 .differencesText = "pas de monde de référence"};
    }
    scene::Comparison comparison = scene::compareWithSnapshot(world, *session.reference);
    return UndoSessionReport{.changes = session.changes,
                             .undone = session.undone,
                             .differences = comparison.differenceCount,
                             .differencesText = [&comparison]
                             {
                                 std::string text;
                                 for (const std::string& line : comparison.differences)
                                 {
                                     text += line + "\n";
                                 }
                                 return text;
                             }()};
}

std::string undoLineOf(const UndoSessionReport& report)
{
    // Le pluriel français comme `stopLineOf` : « 0 différence », « 2 différences ».
    const char* const noun = report.differences <= 1 ? "différence" : "différences";
    return std::format("editor.undo changes={} undone={} differences={} -- {}", report.changes,
                       report.undone, report.differences,
                       ui::trf("Annulation : {} changements, {} annulations, {} {}", report.changes,
                               report.undone, report.differences, ui::tr(noun)));
}

} // namespace levain::editor
