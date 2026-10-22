#include "levain/editor/play.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <optional>
#include <utility>

#include "levain/assets/asset_ref.hpp"
#include "levain/core/log.hpp"
#include "levain/physics/physics.hpp"
#include "levain/physics/physics_world.hpp"
#include "levain/ui/tr.hpp"

namespace levain::editor
{

namespace
{

/// Un monde sans module de physique (l'éditeur sur une scène sans corps) n'a pas de monde Jolt à
/// finir ni à remplacer : `endPlay` et `freshPhysicsWorld` y liraient des singletons absents, et
/// en poser un créerait une physique que le programme n'a pas voulue (le picking du sandbox la
/// croirait là).
bool worldHasPhysics(const flecs::world& world)
{
    return world.has<physics::PhysicsWorld>();
}

core::Error refusalOf(std::string message)
{
    return core::Error{.code = core::ErrorCode::InvalidData, .message = std::move(message)};
}

std::string joinLines(const std::vector<std::string>& lines)
{
    std::string joined;
    for (const std::string& line : lines)
    {
        joined += (joined.empty() ? "" : "\n") + line;
    }
    return joined;
}

double millisecondsSince(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
        .count();
}

} // namespace

core::Result<PlaySession> startPlay(const PlayTarget& target)
{
    flecs::world& world = target.world;
    auto snapshot = scene::takeSnapshot(world);
    if (!snapshot)
    {
        return std::unexpected(snapshot.error());
    }
    // Les assets d'abord : leur refus (deux Play sans Stop) ne change rien, alors que le monde
    // Jolt neuf se défait moins bien.
    if (auto refusal = assets::holdAssetsInUse(world.get_mut<assets::AssetUsage>()))
    {
        return std::unexpected(refusalOf(std::move(*refusal)));
    }
    if (worldHasPhysics(world))
    {
        if (auto refusal = physics::freshPhysicsWorld(world))
        {
            assets::releaseHeldAssets(world.get_mut<assets::AssetUsage>());
            return std::unexpected(refusalOf(std::move(*refusal)));
        }
    }
    scene::usePlayIds(world, target.ranges);
    return PlaySession{.snapshot = std::move(*snapshot),
                       .fixedStep = target.fixedStep,
                       .animation = app::rememberAnimation(target.models, target.animationClock),
                       .stepsAtStart = target.stepsPlayed};
}

StopReport stopPlay(const PlayTarget& target, PlaySession& session, std::string_view from)
{
    const auto start = std::chrono::steady_clock::now();
    flecs::world& world = target.world;
    StopReport report;
    report.from = std::string{from};
    report.steps = target.stepsPlayed - session.stepsAtStart;
    // Dans tous les builds et avec ou sans physique : `restoreSnapshot` refuse aussi le mode
    // différé, mais l'éditeur aurait alors perdu la session et quitté le jeu sur un monde pas
    // rendu.
    if (ecs_is_deferred(world))
    {
        report.refused = true;
        report.refusal =
            "le monde est en mode différé, Stop ne peut pas rendre la scène : l'appeler hors de "
            "tout parcours (ADR-0037)";
        return report;
    }
    // D'abord, avant toute destruction : sans paire `InsideOf`, aucun `onExit` du jeu ne part.
    if (worldHasPhysics(world))
    {
        if (auto refusal = physics::endPlay(world))
        {
            report.refused = true;
            report.refusal = std::move(*refusal);
            return report;
        }
    }
    const auto restored = scene::restoreSnapshot(world, target.ranges, session.snapshot);
    // Même après un échec de la restauration : l'Édition ne doit pas rester dans la plage du jeu.
    scene::useEditIds(world, target.ranges);
    // **La vérification est refaite ici**, par la fonction même du critère, et non lue dans le
    // résultat de la restauration : une restauration sautée ou fausse doit se voir dans la ligne
    // de la CI (règle n°7). Les enfants internes perdus, que `Comparison` rend à part, ont leur
    // avertissement et ne comptent pas.
    scene::Comparison comparison = scene::compareWithSnapshot(world, session.snapshot);
    report.lostInternalChildren = std::move(comparison.lostInternalChildren);
    report.differences = comparison.differenceCount;
    if (!restored)
    {
        // Toujours dite : la restauration peut échouer sur une garde (le monde intact) sans qu'une
        // différence soit comptée.
        report.restoreError = restored.error().message;
    }
    if (report.differences > 0)
    {
        report.differencesText = joinLines(comparison.differences);
    }
    // Les handles de Jolt sont partis avec la restauration (`scene::Transient`) : le monde neuf n'a
    // aucun corps, comme avant le premier Play. Un Édition sans corps est son invariant.
    if (worldHasPhysics(world))
    {
        if (auto refusal = physics::freshPhysicsWorld(world))
        {
            report.problems.push_back(std::move(*refusal));
        }
    }
    target.fixedStep = session.fixedStep;
    app::restoreAnimation(target.models, target.animationClock, session.animation);
    assets::releaseHeldAssets(world.get_mut<assets::AssetUsage>());
    report.milliseconds = millisecondsSince(start);
    return report;
}

std::string stopLineOf(const StopReport& report)
{
    // Le pluriel français : « 0 différence » et « 1 différence », « 2 différences » (« 0 pas »,
    // « 1 pas » : pas ne change pas).
    const char* const noun = report.differences <= 1 ? "différence" : "différences";
    return std::format("editor.stop steps={} differences={} ms={:.1f} from={} -- {}", report.steps,
                       report.differences, report.milliseconds, report.from,
                       ui::trf("Stop : {} pas, {} {}, {:.1f} ms", report.steps, report.differences,
                               ui::tr(noun), report.milliseconds));
}

void logStop(const StopReport& report)
{
    for (const std::string& lost : report.lostInternalChildren)
    {
        core::log(
            "editor", core::LogLevel::Warning, "{}",
            ui::trf("Stop : un déclencheur détruit en jeu revient sans son rappel : {}", lost));
    }
    if (!report.restoreError.empty())
    {
        core::log("editor", core::LogLevel::Error, "{}",
                  ui::trf("Stop : la restauration a échoué : {}", report.restoreError));
    }
    if (report.differences > 0)
    {
        core::log("editor", core::LogLevel::Error, "{}",
                  ui::trf("Stop : la scène n'est pas identique à celle d'avant Play :\n{}",
                          report.differencesText));
    }
    for (const std::string& problem : report.problems)
    {
        core::log("editor", core::LogLevel::Error, "{}", problem);
    }
    core::log("editor", core::LogLevel::Info, "{}", stopLineOf(report));
}

} // namespace levain::editor
