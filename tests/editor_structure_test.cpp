#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include <doctest/doctest.h>
#include <flecs.h>

#include "levain/assets/asset_ref.hpp"
#include "levain/editor/hierarchy.hpp"
#include "levain/editor/undo.hpp"
#include "levain/scene/reflection.hpp"
#include "levain/scene/scene.hpp"
#include "levain/scene/snapshot.hpp"

// Supprimer et renommer, avec leur annulation (ADR-0038, décisions 3, 6, 8, 9 et 10, morceau A6) :
// la pile, ses pierres tombales, le monde de l'éditeur (deux plages d'identifiants).

using namespace levain::editor;
using levain::scene::Transform;

namespace
{

struct Mass
{
    float kilograms = 1.0F;
};

std::uint32_t indexOf(flecs::entity_t id)
{
    return static_cast<std::uint32_t>(id);
}

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

/// Ce qu'un éditeur crée sans le savoir entre une suppression et son annulation : mille entités,
/// une requête nommée, un observateur. Rend leurs index.
std::vector<std::uint32_t> createBystanders(flecs::world& world)
{
    std::vector<std::uint32_t> indexes;
    indexes.reserve(1002);
    for (int i = 0; i < 1000; ++i)
    {
        indexes.push_back(indexOf(world.entity().set(Mass{}).id()));
    }
    indexes.push_back(indexOf(world.query_builder<Mass>("la requête").build().entity().id()));
    indexes.push_back(indexOf(world.observer<Mass>().event(flecs::OnAdd).each([](Mass&) {}).id()));
    return indexes;
}

/// Les lignes que la Hiérarchie montre, dans l'ordre : un test de l'ordre des racines.
std::vector<flecs::entity_t> rootsOf(const flecs::world& world)
{
    const Hierarchy hierarchy = createHierarchy(world);
    std::vector<HierarchyRow> rows;
    listHierarchyRows(hierarchy, rows);
    std::vector<flecs::entity_t> roots;
    roots.reserve(rows.size());
    for (const HierarchyRow& row : rows)
    {
        roots.push_back(row.entity);
    }
    return roots;
}

} // namespace

TEST_CASE("supprimer puis annuler rend la même entité, ses enfants, leur ordre et leurs valeurs, "
          "malgré mille créations, une requête et un observateur")
{
    Bench bench;
    const flecs::entity first = bench.scene("A");
    const flecs::entity cube = bench.scene("Cube", 3.0F);
    bench.scene("B");
    bench.child(cube, "un", 2.0F);
    const flecs::entity two = bench.child(cube, "deux", 5.0F);
    bench.child(cube, "trois");
    bench.child(two, "petit-enfant", 7.0F);
    const auto before = bench.snapshot();
    const std::vector<flecs::entity_t> rootsBefore = rootsOf(bench.world);
    REQUIRE(rootsBefore.size() == 3);

    REQUIRE(deleteEntity(bench.stack, bench.world, Mode::Edit, cube).value());
    CHECK_FALSE(bench.world.is_alive(cube));
    CHECK(rootsOf(bench.world).size() == 2);
    const auto bystanders = createBystanders(bench.world);
    CHECK(std::ranges::find(bystanders, indexOf(cube)) == bystanders.end()); // la pierre tient

    const RequestsReport undone = bench.undo();
    CHECK(undone.undone == 1);
    CHECK(undone.select == cube.id()); // décision 8 : l'entité réveillée devient la sélection
    CHECK(bench.world.is_alive(cube));
    CHECK(rootsOf(bench.world) ==
          std::vector<flecs::entity_t>{rootsBefore[0], cube.id(), rootsBefore[2]});
    CHECK(bench.world.lookup("Cube::deux::petit-enfant").is_valid());
    CHECK(first.is_alive());
    // Les entités des mille créations sont de trop dans l'instantané : on les compare à part.
    const auto differences = bench.differences(before);
    CHECK(std::ranges::none_of(differences, [](const std::string& line)
                               { return line.find("Cube") != std::string::npos; }));

    CHECK(bench.redo().redone == 1);
    CHECK_FALSE(bench.world.is_alive(cube));
    CHECK(bench.undo().undone == 1); // et de nouveau : les pierres sont celles de la suppression
    CHECK(bench.world.is_alive(cube));
    CHECK(cube.get<Mass>().kilograms == 3.0F);
    CHECK(bench.world.lookup("Cube::deux::petit-enfant").get<Mass>().kilograms == 7.0F);
}

TEST_CASE("supprimer puis annuler sans rien d'autre : le monde est celui d'avant, à l'identique")
{
    Bench bench;
    bench.scene("A");
    const flecs::entity cube = bench.scene("Cube", 3.0F);
    bench.child(cube, "un");
    bench.child(cube, "deux");
    bench.scene("B");
    const auto before = bench.snapshot();
    REQUIRE(deleteEntity(bench.stack, bench.world, Mode::Edit, cube).value());
    REQUIRE(bench.undo().undone == 1);
    CHECK(bench.differences(before).empty());
}

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

TEST_CASE("supprimer un enfant du milieu puis annuler le rend à sa place parmi ses frères")
{
    Bench bench;
    const flecs::entity cube = bench.scene("Cube");
    bench.child(cube, "un");
    const flecs::entity two = bench.child(cube, "deux");
    bench.child(cube, "trois");
    const auto before = bench.snapshot();
    REQUIRE(deleteEntity(bench.stack, bench.world, Mode::Edit, two).value());
    bench.child(cube, "quatre"); // un frère apparu entre-temps garde sa place
    REQUIRE(bench.undo().undone == 1);
    CHECK(levain::scene::orderedChildrenOf(bench.world, cube.id())[1] == two.id());
    CHECK(levain::scene::orderedChildrenOf(bench.world, cube.id()).size() == 4);
    CHECK(bench.world.entity(two.id()).get<flecs::Parent>().value == cube.id());
    CHECK(before.entities.size() == 4); // l'instantané d'avant : Cube et ses trois enfants
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
