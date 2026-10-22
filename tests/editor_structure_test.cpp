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

// Créer, supprimer et renommer, avec leur annulation (ADR-0038, décisions 3, 4, 6, 8, 9 et 10,
// morceau A6) : la pile, ses pierres tombales, le monde de l'éditeur (deux plages d'identifiants).

using namespace levain::editor;
using levain::assets::AssetId;
using levain::assets::MeshRef;
using levain::scene::Generated;
using levain::scene::Transform;

namespace
{

struct Mass
{
    float kilograms = 1.0F;
};

/// Un composant sans réflexion : l'observateur qui reprend un index le porte (tombstone_test).
struct Marker
{
    int unused = 0;
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

TEST_CASE("une pierre détruite à la main avant l'annulation : refus nommé, le nouveau reste vivant")
{
    levain::core::LogCapture capture = levain::core::captureLog(32);
    Bench bench;
    const flecs::entity cube = bench.scene("Cube");
    REQUIRE(deleteEntity(bench.stack, bench.world, Mode::Edit, cube).value());
    const auto& command = std::get<SubtreeCommand>(bench.stack.done.back().kind);
    ecs_delete(bench.world, command.tombstones.reserved.front().stone); // hors de la pile
    const flecs::entity newcomer = bench.world.entity("nouveau").set(Mass{4.0F});
    REQUIRE(indexOf(newcomer.id()) == indexOf(cube.id())); // l'index est repris

    const RequestsReport undone = bench.undo();
    CHECK(undone.undone == 0);
    CHECK(undone.failed == 1);
    // La garde n'est pas une assertion : en Release aussi, le nouveau n'est pas écrasé.
    CHECK(newcomer.is_alive());
    CHECK(newcomer.get<Mass>().kilograms == 4.0F);
    CHECK(newcomer.name().c_str() == std::string{"nouveau"});
    CHECK(bench.stack.done.empty()); // abandonnée, la pile n'est pas bloquée
    CHECK(logged(capture, levain::core::LogLevel::Warning, "commande abandonnée"));
}

TEST_CASE("un volume supprimé revient sans son rappel, la console nomme l'observateur perdu")
{
    levain::core::LogCapture capture = levain::core::captureLog(32);
    Bench bench;
    const flecs::entity volume = bench.scene("Zone");
    // Ce que `onEnter` range sous le volume : un observateur rangé par `ChildOf`.
    const flecs::entity callback = bench.world.entity("rappel").child_of(volume);
    REQUIRE(callback.is_alive());

    REQUIRE(deleteEntity(bench.stack, bench.world, Mode::Edit, volume).value());
    CHECK(logged(capture, levain::core::LogLevel::Warning, "« rappel »"));
    const std::size_t afterDelete = levain::core::snapshotOf(*capture.ring).entries.size();

    REQUIRE(bench.undo().undone == 1);
    CHECK(volume.is_alive());
    CHECK_FALSE(callback.is_alive()); // il ne revient pas
    const auto entries = levain::core::snapshotOf(*capture.ring).entries;
    CHECK(std::ranges::any_of(entries.begin() + static_cast<std::ptrdiff_t>(afterDelete),
                              entries.end(), [](const auto& entry)
                              { return entry.text.find("« rappel »") != std::string::npos; }));
}

TEST_CASE(
    "le modèle supprimé reste chargé pour l'annulation : l'asset est épinglé, rendu à la sortie")
{
    Bench bench;
    const AssetId fox = levain::assets::generateAssetId();
    const flecs::entity model = bench.scene("Renard");
    model.set(MeshRef{.mesh = {.asset = fox}});
    REQUIRE(levain::assets::referenceCount(bench.world, fox) == 1);

    REQUIRE(deleteEntity(bench.stack, bench.world, Mode::Edit, model).value());
    CHECK(levain::assets::referenceCount(bench.world, fox) == 0);
    // Une image passe : `takeUnusedAssets` décharge ce qui est tombé à zéro, sauf l'épinglé.
    CHECK(levain::assets::takeUnusedAssets(bench.world).empty());
    CHECK(bench.world.get<levain::assets::AssetUsage>().pinned.at(fox) == 1);

    REQUIRE(bench.undo().undone == 1);
    CHECK(levain::assets::referenceCount(bench.world, fox) == 1);
    CHECK(levain::assets::takeUnusedAssets(bench.world).empty());

    // La commande sort de la pile : l'épingle est rendue, l'asset n'est plus utilisé nulle part.
    REQUIRE(bench.redo().redone == 1);
    clearUndoStack(bench.stack, bench.world);
    CHECK(bench.world.get<levain::assets::AssetUsage>().pinned.empty());
    CHECK(levain::assets::takeUnusedAssets(bench.world) == std::vector<AssetId>{fox});
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

TEST_CASE("supprimer un nœud généré, ou une entité sous lui, est refusé et nommé")
{
    Bench bench;
    const flecs::entity root = bench.scene("Modèle");
    const flecs::entity node = bench.child(root, "os");
    node.add<Generated>();
    const flecs::entity sword = bench.child(node, "épée");
    const auto onNode = deleteEntity(bench.stack, bench.world, Mode::Edit, node.id());
    REQUIRE_FALSE(onNode.has_value());
    CHECK(onNode.error().message.find("Supprimer") != std::string::npos);
    CHECK(node.is_alive());
    const auto underNode = deleteEntity(bench.stack, bench.world, Mode::Edit, sword.id());
    REQUIRE_FALSE(underNode.has_value());
    CHECK(sword.is_alive());
    CHECK(bench.stack.done.empty());
    // En jeu, supprimer est un `ecs_delete` simple, mais le même refus vaut.
    CHECK_FALSE(deleteEntity(bench.stack, bench.world, Mode::Play, node.id()).has_value());
    CHECK(node.is_alive());
}

TEST_CASE("renommer un nœud généré est refusé et nommé")
{
    Bench bench;
    const flecs::entity root = bench.scene("Modèle");
    const flecs::entity node = bench.child(root, "os");
    node.add<Generated>();
    const auto refused = renameEntity(bench.stack, bench.world, Mode::Edit, node.id(), "autre");
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().message.find("nœud généré") != std::string::npos);
    CHECK(node.name().c_str() == std::string{"os"});
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

TEST_CASE("après un renommage, Stop retrouve les entités par leur chemin : zéro différence")
{
    Bench bench;
    const flecs::entity cube = bench.scene("Cube");
    const flecs::entity kid = bench.child(cube, "enfant");
    REQUIRE(renameEntity(bench.stack, bench.world, Mode::Edit, cube.id(), "Caisse").value());
    REQUIRE(renameEntity(bench.stack, bench.world, Mode::Edit, kid.id(), "petit").value());

    const auto snapshot = bench.snapshot();
    levain::scene::usePlayIds(bench.world, bench.ranges);
    bench.world.entity("née en jeu").set(Transform{});
    levain::scene::useEditIds(bench.world, bench.ranges);
    const auto restored = levain::scene::restoreSnapshot(bench.world, bench.ranges, snapshot);
    REQUIRE(restored.has_value());
    CHECK(bench.differences(snapshot).empty());
    CHECK(bench.world.lookup("Caisse::petit") == kid);
}

TEST_CASE(
    "en jeu, créer, supprimer et renommer sont simples : la pile ne bouge pas, Stop rend tout")
{
    Bench bench;
    const flecs::entity cube = bench.scene("Cube");
    const flecs::entity crate = bench.scene("Caisse");
    REQUIRE(bench.stack.done.empty());
    const auto snapshot = bench.snapshot();

    levain::scene::usePlayIds(bench.world, bench.ranges);
    REQUIRE(deleteEntity(bench.stack, bench.world, Mode::Play, crate.id()).has_value());
    CHECK_FALSE(crate.is_alive());
    REQUIRE(renameEntity(bench.stack, bench.world, Mode::Play, cube.id(), "Autre").value());
    const auto born = createEntity(bench.stack, bench.world, Mode::Play, 0, NewEntityName);
    REQUIRE(born.has_value());
    CHECK(indexOf(*born) >= levain::scene::PlayFirstIndex);
    CHECK(bench.stack.done.empty()); // perdu à Stop : rien n'entre dans la pile
    CHECK(bench.stack.undone.empty());
    CHECK_FALSE(isModified(bench.stack));

    levain::scene::useEditIds(bench.world, bench.ranges);
    const auto restored = levain::scene::restoreSnapshot(bench.world, bench.ranges, snapshot);
    REQUIRE(restored.has_value());
    CHECK(crate.is_alive()); // Stop réveille l'entité avec son identifiant
    CHECK(cube.name().c_str() == std::string{"Cube"});
    CHECK(bench.differences(snapshot).empty());
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

TEST_CASE("la 101e commande fait sortir une suppression, qui rend sa pierre")
{
    Bench bench;
    const flecs::entity cube = bench.scene("Cube");
    REQUIRE(deleteEntity(bench.stack, bench.world, Mode::Edit, cube).value());
    const flecs::entity_t stone =
        std::get<SubtreeCommand>(bench.stack.done.front().kind).tombstones.reserved.front().stone;
    CHECK(bench.world.is_alive(stone));
    const flecs::entity other = bench.scene("Autre");
    for (int i = 0; i < static_cast<int>(UndoDepth); ++i)
    {
        REQUIRE(renameEntity(bench.stack, bench.world, Mode::Edit, other.id(),
                             "Autre " + std::to_string(i))
                    .value());
    }
    CHECK(bench.stack.done.size() == UndoDepth);
    CHECK_FALSE(bench.world.is_alive(stone));
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

TEST_CASE("une suppression refaite dont l'index est repris : dite perdue, la scène reste modifiée")
{
    Bench bench;
    bool armed = false;
    bench.world.observer<Marker>()
        .event(flecs::OnRemove)
        .each(
            [&](flecs::entity entity, Marker&)
            {
                if (armed)
                {
                    armed = false;
                    entity.world().entity("née pendant la suppression");
                }
            });
    const flecs::entity volume = bench.scene("Volume").set(Marker{});
    bench.child(volume, "enfant");
    markSaved(bench.stack);
    REQUIRE(deleteEntity(bench.stack, bench.world, Mode::Edit, volume).value());
    REQUIRE(bench.undo().undone == 1);
    markSaved(bench.stack); // l'état enregistré est celui d'avant la suppression, refaite ensuite
    CHECK_FALSE(isModified(bench.stack));

    armed = true;
    const RequestsReport redone = bench.redo();
    CHECK(redone.failed == 1);
    CHECK_FALSE(bench.world.is_alive(volume.id())); // la suppression est faite
    CHECK(redone.changedWorld());
    CHECK(isModified(bench.stack)); // S1 : la scène a perdu une entité, elle le dit
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

TEST_CASE("« modifié » suit les créations, les suppressions et les noms, et une pierre perdue le "
          "reste")
{
    Bench bench;
    const flecs::entity cube = bench.scene("Cube");
    CHECK_FALSE(isModified(bench.stack));
    REQUIRE(renameEntity(bench.stack, bench.world, Mode::Edit, cube.id(), "Caisse").value());
    CHECK(isModified(bench.stack));
    markSaved(bench.stack);
    REQUIRE(bench.undo().undone == 1);
    CHECK(isModified(bench.stack));
    REQUIRE(bench.redo().redone == 1);
    CHECK_FALSE(isModified(bench.stack));
    markUnrecoverable(bench.stack); // une suppression que les pierres n'ont pas tenue
    CHECK(isModified(bench.stack));
    CHECK(bench.undo().undone == 1);
    CHECK(isModified(bench.stack));
    markSaved(bench.stack);
    CHECK_FALSE(isModified(bench.stack));
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

TEST_CASE("le champ de renommage s'ouvre sur une entité de la scène, pas sur un nœud généré")
{
    Bench bench;
    const flecs::entity root = bench.scene("Modèle");
    const flecs::entity node = bench.child(root, "os");
    node.add<Generated>();
    Hierarchy hierarchy = createHierarchy(bench.world);

    CHECK(beginRename(hierarchy, bench.world, node.id()).has_value()); // refusé, nommé
    CHECK(hierarchy.rename.entity == 0);

    CHECK_FALSE(beginRename(hierarchy, bench.world, root.id()).has_value());
    CHECK(hierarchy.rename.entity == root.id());
    CHECK(std::string{hierarchy.rename.text.data()} == "Modèle");
    CHECK(hierarchy.rename.justOpened);
    cancelRename(hierarchy);
    CHECK(hierarchy.rename.entity == 0);

    // Une entité morte ne s'ouvre pas non plus.
    const flecs::entity_t dead = bench.scene("Éphémère").id();
    bench.world.entity(dead).destruct();
    CHECK(beginRename(hierarchy, bench.world, dead).has_value());
    CHECK(hierarchy.rename.entity == 0);
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

TEST_CASE("un parent mort refuse l'annulation avant le réveil : rien n'est réveillé vide")
{
    levain::core::LogCapture capture = levain::core::captureLog(32);
    Bench bench;
    const flecs::entity parent = bench.scene("Parent");
    const flecs::entity kid = bench.child(parent, "enfant");
    REQUIRE(deleteEntity(bench.stack, bench.world, Mode::Edit, kid).value());
    parent.destruct(); // hors de la pile : le parent a disparu
    const RequestsReport undone = bench.undo();
    CHECK(undone.undone == 0);
    CHECK(undone.failed == 1);
    // Les deux gardes passent avant le réveil : aucune entité vide, sans nom ni parent, ne traîne
    // dans la scène (la commande abandonnée rend sa pierre).
    CHECK(bench.stack.done.empty());
    CHECK_FALSE(bench.world.is_alive(kid.id()));
    CHECK(logged(capture, levain::core::LogLevel::Warning, "est mort"));
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
