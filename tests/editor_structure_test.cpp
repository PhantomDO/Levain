#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include <doctest/doctest.h>
#include <flecs.h>

#include "levain/assets/asset_ref.hpp"
#include "levain/core/log_ring.hpp"
#include "levain/editor/editor.hpp"
#include "levain/editor/hierarchy.hpp"
#include "levain/editor/undo.hpp"
#include "levain/scene/reflection.hpp"
#include "levain/scene/scene.hpp"
#include "levain/scene/snapshot.hpp"
#include "levain/scene/tombstone.hpp"

// Créer, supprimer et renommer, avec leur annulation (ADR-0038, décisions 3, 6, 8, 9 et 10,
// morceau A6) : la pile, ses pierres tombales, le monde de l'éditeur (deux plages d'identifiants).

using namespace levain::editor;
using levain::scene::Generated;
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

bool logged(const levain::core::LogCapture& capture, levain::core::LogLevel level,
            std::string_view text)
{
    const auto snapshot = levain::core::snapshotOf(*capture.ring);
    return std::ranges::any_of(
        snapshot.entries, [&](const auto& entry)
        { return entry.level == level && entry.text.find(text) != std::string::npos; });
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

TEST_CASE(
    "créer, annuler, refaire : l'identifiant d'origine, un nom libre, un Transform, la plage de "
    "l'édition")
{
    Bench bench;
    bench.scene("Entité"); // le premier nom est pris
    const auto created = createEntity(bench.stack, bench.world, Mode::Edit, 0, NewEntityName);
    REQUIRE(created.has_value());
    const flecs::entity entity = bench.world.entity(*created);
    CHECK(entity.name().c_str() == std::string{"Entité 2"}); // uniqueNameAmong
    CHECK(entity.has<Transform>());                          // la Hiérarchie le montre
    CHECK(indexOf(entity.id()) < levain::scene::PlayFirstIndex);
    CHECK(bench.stack.done.size() == 1);

    REQUIRE(bench.undo().undone == 1);
    CHECK_FALSE(bench.world.is_alive(entity.id()));
    createBystanders(bench.world); // l'index tient
    const RequestsReport redone = bench.redo();
    CHECK(redone.redone == 1);
    CHECK(redone.select == entity.id()); // la création refaite est la sélection
    CHECK(bench.world.is_alive(entity.id()));
    CHECK(bench.world.entity(entity.id()).name().c_str() == std::string{"Entité 2"});
}

TEST_CASE("une chaîne de créations et d'une suppression : tout s'annule dans l'ordre et se refait "
          "aux mêmes identifiants")
{
    Bench bench;
    const flecs::entity_t parent =
        createEntity(bench.stack, bench.world, Mode::Edit, 0, NewEntityName).value();
    const flecs::entity_t kid =
        createEntity(bench.stack, bench.world, Mode::Edit, parent, NewEntityName).value();
    REQUIRE(deleteEntity(bench.stack, bench.world, Mode::Edit, parent).value());
    CHECK_FALSE(bench.world.is_alive(kid));
    CHECK(bench.stack.done.size() == 3);

    REQUIRE(bench.undo(3).undone == 3); // la suppression, l'enfant, le parent
    CHECK_FALSE(bench.world.is_alive(parent));
    CHECK_FALSE(bench.world.is_alive(kid));
    REQUIRE(bench.redo(2).redone == 2);
    CHECK(bench.world.is_alive(parent));
    CHECK(bench.world.is_alive(kid));
    CHECK(bench.world.entity(kid).get<flecs::Parent>().value == parent);
    REQUIRE(bench.redo().redone == 1); // la suppression refaite
    CHECK_FALSE(bench.world.is_alive(parent));
    REQUIRE(bench.undo().undone == 1);
    CHECK(bench.world.is_alive(kid));
}

TEST_CASE("créer sous un nœud généré est refusé, nommé, le monde intact")
{
    Bench bench;
    const flecs::entity root = bench.scene("Modèle");
    const flecs::entity node = bench.child(root, "os");
    node.add<Generated>();
    const std::size_t entities = levain::scene::sceneEntities(bench.world).size();
    const auto refused =
        createEntity(bench.stack, bench.world, Mode::Edit, node.id(), NewEntityName);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().message.find("nœud généré") != std::string::npos);
    CHECK(levain::scene::sceneEntities(bench.world).size() == entities);
    CHECK(bench.stack.done.empty());
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

TEST_CASE("une demande de création, de suppression et de renommage passe par la file")
{
    Bench bench;
    const flecs::entity cube = bench.scene("Cube");
    const RequestsReport report = bench.apply({{RequestKind::Create, 0},
                                               {RequestKind::Rename, cube.id(), "Caisse"},
                                               {RequestKind::Delete, cube.id()}});
    CHECK(report.created == 1);
    CHECK(report.renamed == 1);
    CHECK(report.deleted == 1);
    CHECK(report.beginRename != 0); // la nouvelle entité s'ouvre en renommage
    CHECK(report.select == report.beginRename);
    CHECK(report.changedWorld());
    CHECK(bench.stack.done.size() == 3);
}

TEST_CASE("les refus de la file sont comptés et dits, le monde intact")
{
    levain::core::LogCapture capture = levain::core::captureLog(16);
    Bench bench;
    const flecs::entity cube = bench.scene("Cube");
    const RequestsReport report = bench.apply({{RequestKind::Rename, cube.id(), "a.b"},
                                               {RequestKind::Delete, 12345678},
                                               {RequestKind::Create, 12345678}});
    CHECK(report.refused == 3);
    CHECK_FALSE(report.changedWorld());
    CHECK(cube.name().c_str() == std::string{"Cube"});
    CHECK(logged(capture, levain::core::LogLevel::Warning, "Renommer"));
    CHECK(bench.stack.done.empty());
}

TEST_CASE("créer ou supprimer en mode différé est refusé et gardé : le monde n'est pas touché")
{
    Bench bench;
    const flecs::entity cube = bench.scene("Cube");
    bench.world.defer_begin();
    bench.queue = {{RequestKind::Delete, cube.id()}};
    const RequestsReport inside =
        applyEditorRequests(bench.queue, bench.stack, bench.world, Mode::Edit);
    CHECK_FALSE(inside.changedWorld());
    CHECK(bench.queue.size() == 1);
    CHECK_FALSE(createEntity(bench.stack, bench.world, Mode::Edit, 0, NewEntityName).has_value());
    bench.world.defer_end();
    CHECK(cube.is_alive());
    CHECK(applyEditorRequests(bench.queue, bench.stack, bench.world, Mode::Edit).deleted == 1);
}

TEST_CASE("une commande nouvelle après une annulation libère les pierres du refaisable")
{
    Bench bench;
    const flecs::entity cube = bench.scene("Cube");
    REQUIRE(deleteEntity(bench.stack, bench.world, Mode::Edit, cube).value());
    REQUIRE(bench.undo().undone == 1);
    // Créer une entité vide le refaisable (la suppression de Cube annulée) : ses pierres, déjà
    // détruites au réveil, ne sont pas libérées une seconde fois.
    REQUIRE(createEntity(bench.stack, bench.world, Mode::Edit, 0, NewEntityName).has_value());
    CHECK(bench.stack.undone.empty());
    CHECK(cube.is_alive());
    // Une création défaite laisse une pierre ; la libérer rend l'index.
    REQUIRE(bench.undo().undone == 1);
    const auto& created = std::get<SubtreeCommand>(bench.stack.undone.back().kind);
    const flecs::entity_t stone = created.tombstones.reserved.front().stone;
    CHECK(bench.world.is_alive(stone));
    REQUIRE(renameEntity(bench.stack, bench.world, Mode::Edit, cube.id(), "Autre").value());
    CHECK_FALSE(bench.world.is_alive(stone)); // la nouvelle commande a libéré le refaisable
}

TEST_CASE(
    "une création défaite puis refaite ne garde pas les pierres de son annulation : en sortir "
    "ne détruit pas celles d'une suppression plus récente")
{
    Bench bench;
    const flecs::entity_t entity =
        createEntity(bench.stack, bench.world, Mode::Edit, 0, NewEntityName).value();
    REQUIRE(bench.undo().undone == 1);
    REQUIRE(bench.redo().redone == 1);
    // Le réveil a rendu les pierres : la commande n'en retient plus (B1).
    CHECK(std::get<SubtreeCommand>(bench.stack.done.back().kind).tombstones.reserved.empty());
    REQUIRE(deleteEntity(bench.stack, bench.world, Mode::Edit, entity).value());
    const flecs::entity other = bench.scene("Autre");
    // 99 commandes de plus : la création, la plus ancienne, sort de la pile.
    for (std::size_t i = 0; i + 2 < UndoDepth; ++i)
    {
        REQUIRE(renameEntity(bench.stack, bench.world, Mode::Edit, other.id(),
                             "Autre " + std::to_string(i))
                    .value());
    }
    REQUIRE(renameEntity(bench.stack, bench.world, Mode::Edit, other.id(), "Dernier").value());
    CHECK(bench.stack.done.size() == UndoDepth);
    const RequestsReport back = bench.undo(static_cast<int>(UndoDepth));
    CHECK(back.undone == static_cast<int>(UndoDepth));
    CHECK(back.failed == 0);
    CHECK(bench.world.is_alive(entity)); // la suppression s'annule : l'entité revient
}

TEST_CASE("annuler une création relit le sous-arbre : un enfant ajouté hors de la pile revient "
          "entier au rétablissement")
{
    Bench bench;
    const flecs::entity_t parent =
        createEntity(bench.stack, bench.world, Mode::Edit, 0, NewEntityName).value();
    // Un enfant fait hors de la pile (un script, un outil) après la création (N2).
    const flecs::entity kid = bench.child(bench.world.entity(parent), "Passager", 3.0F);
    const flecs::entity_t kidId = kid.id();
    REQUIRE(bench.undo().undone == 1);
    CHECK_FALSE(bench.world.is_alive(kidId));
    REQUIRE(bench.redo().redone == 1);
    REQUIRE(bench.world.is_alive(kidId));
    const flecs::entity back = bench.world.entity(kidId);
    CHECK(back.name().c_str() == std::string{"Passager"});
    CHECK(back.get<flecs::Parent>().value == parent);
    CHECK(back.has<Transform>());
    CHECK(back.get<Mass>().kilograms == 3.0F);
}

TEST_CASE(
    "l'entité supprimée par une annulation est désélectionnée, celle qu'on réveille est choisie")
{
    Bench bench;
    const flecs::entity_t created =
        createEntity(bench.stack, bench.world, Mode::Edit, 0, NewEntityName).value();
    flecs::entity_t selected = created;
    REQUIRE(bench.undo().undone == 1);
    CHECK_FALSE(selectedIfAlive(bench.world, selected).is_valid()); // l'ancien identifiant est mort
    const RequestsReport redone = bench.redo();
    selected = redone.select;
    CHECK(selectedIfAlive(bench.world, selected).id() == created);
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
