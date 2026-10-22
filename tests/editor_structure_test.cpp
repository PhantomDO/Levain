#include <string>
#include <vector>

#include <doctest/doctest.h>
#include <flecs.h>

#include "levain/assets/asset_ref.hpp"
#include "levain/editor/undo.hpp"
#include "levain/scene/reflection.hpp"
#include "levain/scene/scene.hpp"
#include "levain/scene/snapshot.hpp"

// Renommer, avec son annulation (ADR-0038, décisions 6 et 10, morceau A6) : la pile, le monde de
// l'éditeur (deux plages d'identifiants).

using namespace levain::editor;
using levain::scene::Transform;

namespace
{

struct Mass
{
    float kilograms = 1.0F;
};

/// Le monde de l'éditeur : les plages d'identifiants, la scène, les assets, une valeur décrite.
struct Bench
{
    flecs::world world;
    levain::scene::IdRanges ranges;
    UndoStack stack;
    std::vector<EditorRequest> queue;

    Bench()
    {
        auto created = levain::scene::createIdRanges(world);
        REQUIRE(created.has_value());
        ranges = *created;
        world.import<levain::scene::SceneModule>();
        world.import<levain::assets::AssetsModule>();
        levain::scene::describeAuthored<Mass>(world);
    }

    RequestsReport apply(std::vector<EditorRequest> requests, Mode mode = Mode::Edit)
    {
        queue = std::move(requests);
        return applyEditorRequests(queue, stack, world, mode);
    }

    RequestsReport undo(int times = 1)
    {
        return apply(
            std::vector<EditorRequest>(static_cast<std::size_t>(times), RequestKind::Undo));
    }

    RequestsReport redo(int times = 1)
    {
        return apply(
            std::vector<EditorRequest>(static_cast<std::size_t>(times), RequestKind::Redo));
    }

    /// Une entité de la scène telle que l'éditeur les fait : nommée, placée, avec une masse.
    flecs::entity scene(const char* name, float kilograms = 1.0F)
    {
        return world.entity(name).set(Transform{}).set(Mass{kilograms});
    }

    flecs::entity child(flecs::entity parent, const char* name, float kilograms = 1.0F)
    {
        return world.entity(flecs::Parent{parent}, name).set(Transform{}).set(Mass{kilograms});
    }

    levain::scene::Snapshot snapshot()
    {
        auto taken = levain::scene::takeSnapshot(world);
        REQUIRE(taken.has_value());
        return std::move(*taken);
    }

    /// Ce qui diffère de l'instantané : vide si la scène est celle d'alors.
    std::vector<std::string> differences(const levain::scene::Snapshot& snapshot) const
    {
        return levain::scene::differencesFrom(world, snapshot);
    }
};

} // namespace

TEST_CASE("les noms piégés sont refusés, rien ne change, aucune commande n'est posée")
{
    Bench bench;
    const flecs::entity cube = bench.scene("Cube");
    bench.scene("Autre");
    const flecs::entity kid = bench.child(cube, "enfant");
    bench.child(cube, "frère");
    const std::vector<std::string> trapped{"a.b", "a::b", "#12", "_", "*", "$", "", "Autre"};
    for (const std::string& name : trapped)
    {
        CAPTURE(name);
        const auto refused = renameEntity(bench.stack, bench.world, Mode::Edit, cube.id(), name);
        REQUIRE_FALSE(refused.has_value());
        CHECK(cube.name().c_str() == std::string{"Cube"});
        CHECK(renameRefusalOf(bench.world, cube.id(), name).has_value());
    }
    // Sous un parent, « _ », « * » et « $ » se retrouvent par `lookup` : permis. « #12 » ne l'est
    // pas.
    CHECK_FALSE(renameRefusalOf(bench.world, kid.id(), "_").has_value());
    CHECK(renameRefusalOf(bench.world, kid.id(), "#12").has_value());
    CHECK(renameRefusalOf(bench.world, kid.id(), "frère").has_value()); // un frère le porte
    CHECK(bench.stack.done.empty());
    CHECK_FALSE(isModified(bench.stack));
}

TEST_CASE("renommer : un nom libre s'annule et se refait, un nom inchangé ne pose rien")
{
    Bench bench;
    const flecs::entity cube = bench.scene("Cube");
    CHECK_FALSE(renameEntity(bench.stack, bench.world, Mode::Edit, cube.id(), "Cube").value());
    CHECK(bench.stack.done.empty());

    CHECK(renameEntity(bench.stack, bench.world, Mode::Edit, cube.id(), "Caisse").value());
    CHECK(cube.name().c_str() == std::string{"Caisse"});
    CHECK(bench.world.lookup("Caisse") == cube);
    CHECK_FALSE(bench.world.lookup("Cube").is_valid());
    CHECK(bench.undo().undone == 1);
    CHECK(cube.name().c_str() == std::string{"Cube"});
    CHECK(bench.world.lookup("Cube") == cube);
    CHECK(bench.redo().redone == 1);
    CHECK(bench.world.lookup("Caisse") == cube);
}

TEST_CASE("un nom refusé n'est jamais écrit, même demandé directement à la file")
{
    Bench bench;
    const flecs::entity cube = bench.scene("Cube");
    const RequestsReport report = bench.apply({{RequestKind::Rename, cube.id(), "a::b"}});
    CHECK(report.refused == 1);
    CHECK(report.renamed == 0);
    CHECK(cube.name().c_str() == std::string{"Cube"});
    CHECK(bench.world.lookup("Cube") == cube);
}
