/// Le coût de l'annulation (ADR-0038, « Les contrôles », « Le coût de l'annulation ») :
///   - supprimer puis annuler un sous-arbre de 1 000 entités : lire l'enregistrement et supprimer
///     en gardant les index, puis rendre les pierres et l'enregistrement ;
///   - 150 changements de valeur et 100 annulations, par la porte de l'inspecteur et de Ctrl+Z.
/// Une ligne : `undo.cost subtree=1000 delete_ms=… undo_ms=… values_ms=…`, que `tools/undo-cost.sh`
/// lit et résume. Exécutable séparé, hors de `ctest`, comme `levain_scene_bench` : le chiffre
/// dépend de la machine, il se mesure en Release et ne va au journal que s'il vient de la machine
/// de référence (SPECS §10, règle n°6).
///
/// **Un chiffre n'est rendu que si le résultat est juste** (règle n°7) : l'état après l'annulation
/// est comparé à celui d'avant (`differencesFrom`), et l'échec sort en 1 sans ligne.

#include <chrono>
#include <cstdio>
#include <exception>
#include <format>
#include <print>
#include <stdexcept>
#include <string>
#include <vector>

#include <flecs.h>

#include "levain/editor/undo.hpp"
#include "levain/scene/components.hpp"
#include "levain/scene/reflection.hpp"
#include "levain/scene/scene.hpp"
#include "levain/scene/snapshot.hpp"
#include "levain/scene/subtree.hpp"
#include "levain/scene/tombstone.hpp"

namespace
{

using Clock = std::chrono::steady_clock;

constexpr int Branches = 9;
constexpr int LeavesPerBranch = 110; ///< 1 + 9 + 9 × 110 = 1 000 entités.
constexpr int SubtreeSize = 1 + Branches + (Branches * LeavesPerBranch);

struct Mass
{
    float kilograms = 1.0F;
};

double millisecondsSince(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

void require(bool holds, const char* what)
{
    if (!holds)
    {
        throw std::runtime_error{what};
    }
}

flecs::entity childOf(const flecs::world& world, flecs::entity parent, const std::string& name)
{
    return world.entity().set(flecs::Parent{parent}).set_name(name.c_str()).set(Mass{2.0F});
}

/// Un arbre : une racine, neuf branches, 110 feuilles par branche.
flecs::entity buildTree(flecs::world& world)
{
    const flecs::entity root = world.entity("arbre").set(Mass{});
    for (int b = 0; b < Branches; ++b)
    {
        const flecs::entity branch = childOf(world, root, std::format("branche_{}", b));
        for (int l = 0; l < LeavesPerBranch; ++l)
        {
            childOf(world, branch, std::format("feuille_{}", l));
        }
    }
    return root;
}

struct Costs
{
    double deleteMs = 0.0;
    double undoMs = 0.0;
    double valuesMs = 0.0;
};

void subtreeCosts(Costs& costs)
{
    flecs::world world;
    require(levain::scene::createIdRanges(world).has_value(), "plages d'identifiants");
    world.import<levain::scene::SceneModule>();
    levain::scene::describeAuthored<Mass>(world);
    const flecs::entity root = buildTree(world);
    auto before = levain::scene::takeSnapshot(world);
    require(before.has_value(), "instantané d'avant");
    require(levain::scene::sceneEntities(world).size() == SubtreeSize, "1 000 entités");

    const auto deleteStart = Clock::now();
    auto record = levain::scene::readSubtree(world, root.id());
    require(record.has_value(), "lecture du sous-arbre");
    auto removed = levain::scene::removeKeepingIndex(world, root.id());
    require(removed.has_value(), "suppression");
    costs.deleteMs = millisecondsSince(deleteStart);
    require(levain::scene::sceneEntities(world).empty(), "le sous-arbre est parti");

    const auto undoStart = Clock::now();
    require(levain::scene::checkTombstonesHold(world, *removed).has_value(), "garde des pierres");
    require(levain::scene::checkSubtreeRestorable(world, *record).has_value(),
            "garde du sous-arbre");
    require(levain::scene::reviveFromTombstones(world, *removed).has_value(), "réveil");
    require(levain::scene::restoreSubtree(world, *record).has_value(), "restauration");
    costs.undoMs = millisecondsSince(undoStart);
    require(levain::scene::differencesFrom(world, *before).empty(),
            "l'annulation ne rend pas le sous-arbre");
}

void valueCosts(Costs& costs)
{
    flecs::world world;
    world.import<levain::scene::SceneModule>();
    const flecs::entity entity = world.entity("cible").set(levain::scene::Transform{});
    const flecs::entity_t component = world.id<levain::scene::Transform>();
    levain::editor::UndoStack stack;
    std::vector<levain::editor::EditorRequest> queue;

    const auto start = Clock::now();
    for (int i = 1; i <= 150; ++i)
    {
        levain::scene::Transform value = entity.get<levain::scene::Transform>();
        value.position.x = static_cast<float>(i);
        const auto recorded = levain::editor::editValue(stack, world, levain::editor::Mode::Edit,
                                                        entity, component, &value);
        require(recorded.has_value() && *recorded, "un changement de valeur");
    }
    queue.assign(100, levain::editor::RequestKind::Undo);
    const auto report =
        levain::editor::applyEditorRequests(queue, stack, world, levain::editor::Mode::Edit);
    costs.valuesMs = millisecondsSince(start);
    require(report.undone == 100, "100 annulations");
    require(entity.get<levain::scene::Transform>().position.x == 50.0F, "l'état d'après le 50e");
}

} // namespace

int main()
{
    try
    {
        Costs costs;
        subtreeCosts(costs);
        valueCosts(costs);
        std::println("undo.cost subtree={} delete_ms={:.3f} undo_ms={:.3f} values_ms={:.3f}",
                     SubtreeSize, costs.deleteMs, costs.undoMs, costs.valuesMs);
        return 0;
    }
    catch (const std::exception& e)
    {
        std::fputs("échec : ", stderr);
        std::fputs(e.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
}
