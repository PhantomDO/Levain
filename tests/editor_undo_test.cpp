#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <doctest/doctest.h>
#include <flecs.h>

#include "levain/assets/asset_ref.hpp"
#include "levain/core/log_ring.hpp"
#include "levain/editor/undo.hpp"
#include "levain/scene/reflection.hpp"
#include "levain/scene/scene.hpp"

// La pile d'annulation (ADR-0038, décisions 1 et 9, morceau A4) : 100 niveaux par les valeurs,
// « modifié » lu dans la pile, la pile gelée en jeu, les épingles rendues avec les commandes.

using namespace levain::editor;
using levain::assets::AssetId;
using levain::assets::MeshRef;
using levain::scene::ValueBox;

namespace
{

struct Mass
{
    float kilograms = 0.0F;
};

struct Charge
{
    float coulombs = 0.0F;
};

struct Bench
{
    flecs::world world;
    UndoStack stack;
    std::vector<HistoryRequest> queue;
    flecs::entity entity;

    Bench()
    {
        world.import<levain::scene::SceneModule>();
        world.import<levain::assets::AssetsModule>();
        levain::scene::describeAuthored<Mass>(world);
        levain::scene::describeAuthored<Charge>(world);
        entity = world.entity("cible").set(Mass{}).set(Charge{});
    }

    flecs::entity_t mass() { return world.id<Mass>(); }

    /// Un geste d'une image : écrire la masse, poser la commande. Vrai si elle est posée.
    bool setMass(float kilograms, Mode mode = Mode::Edit)
    {
        const Mass value{.kilograms = kilograms};
        const auto recorded = editValue(stack, world, mode, entity, mass(), &value);
        REQUIRE(recorded.has_value());
        return *recorded;
    }

    float kilograms() { return entity.get<Mass>().kilograms; }

    RequestsReport apply(std::vector<HistoryRequest> requests, Mode mode = Mode::Edit)
    {
        queue = std::move(requests);
        return applyEditorRequests(queue, stack, world, mode);
    }

    RequestsReport undo(int times = 1, Mode mode = Mode::Edit)
    {
        return apply(
            std::vector<HistoryRequest>(static_cast<std::size_t>(times), HistoryRequest::Undo),
            mode);
    }

    RequestsReport redo(int times = 1, Mode mode = Mode::Edit)
    {
        return apply(
            std::vector<HistoryRequest>(static_cast<std::size_t>(times), HistoryRequest::Redo),
            mode);
    }
};

int pinsOf(Bench& bench, AssetId asset)
{
    const auto& pinned = bench.world.get<levain::assets::AssetUsage>().pinned;
    const auto found = pinned.find(asset);
    return found == pinned.end() ? 0 : found->second;
}

} // namespace

TEST_CASE(
    "150 changements, 100 annulations : l'état est celui d'après le 50e, la 101e ne fait rien")
{
    Bench bench;
    for (int i = 1; i <= 150; ++i)
    {
        REQUIRE(bench.setMass(static_cast<float>(i)));
    }
    CHECK(bench.stack.done.size() == UndoDepth); // la 101e a fait sortir la plus ancienne

    const RequestsReport undone = bench.undo(100);
    CHECK(undone.undone == 100);
    CHECK(bench.kilograms() == 50.0F);
    const RequestsReport beyond = bench.undo(); // la 101e annulation
    CHECK(beyond.undone == 0);
    CHECK(beyond.refused == 1);
    CHECK(bench.kilograms() == 50.0F);

    CHECK(bench.redo(100).redone == 100);
    CHECK(bench.kilograms() == 150.0F);
}

TEST_CASE("une commande dont l'avant égale l'après est jetée, la pile ne bouge pas")
{
    Bench bench;
    REQUIRE(bench.setMass(3.0F));
    CHECK_FALSE(bench.setMass(3.0F)); // retaper la même valeur
    CHECK(bench.stack.done.size() == 1);
    CHECK(isModified(bench.stack)); // la première commande suffit à modifier
    const std::uint64_t top = topNumberOf(bench.stack);
    CHECK_FALSE(bench.setMass(3.0F));
    CHECK(topNumberOf(bench.stack) == top);
}

TEST_CASE("une commande nouvelle vide les refaisables et rend leurs épingles")
{
    Bench bench;
    const AssetId fox = levain::assets::generateAssetId();
    const AssetId hen = levain::assets::generateAssetId();
    const AssetId cow = levain::assets::generateAssetId();
    const flecs::entity model = bench.world.entity().set(MeshRef{.mesh = {.asset = fox}});
    const auto change = [&](AssetId to)
    {
        const MeshRef value{.mesh = {.asset = to}};
        const auto recorded = editValue(bench.stack, bench.world, Mode::Edit, model,
                                        bench.world.id<MeshRef>(), &value);
        REQUIRE(recorded.has_value());
        REQUIRE(*recorded);
    };

    change(hen);
    CHECK(pinsOf(bench, fox) == 1);
    CHECK(pinsOf(bench, hen) == 1);
    REQUIRE(bench.undo().undone == 1);
    CHECK(pinsOf(bench, hen) == 1); // refaisable : elle retient encore

    change(cow); // vide d'abord les refaisables
    CHECK(bench.stack.undone.empty());
    CHECK(pinsOf(bench, hen) == 0); // rendue
    CHECK(pinsOf(bench, fox) == 1);
    CHECK(pinsOf(bench, cow) == 1);
}

TEST_CASE("la plus ancienne sort à la 101e commande et rend ce qu'elle retient")
{
    Bench bench;
    const AssetId fox = levain::assets::generateAssetId();
    const AssetId hen = levain::assets::generateAssetId();
    const flecs::entity model = bench.world.entity().set(MeshRef{.mesh = {.asset = fox}});
    const MeshRef value{.mesh = {.asset = hen}};
    REQUIRE(
        editValue(bench.stack, bench.world, Mode::Edit, model, bench.world.id<MeshRef>(), &value)
            .value());
    CHECK(pinsOf(bench, hen) == 1);

    for (int i = 1; i <= static_cast<int>(UndoDepth) - 1; ++i)
    {
        REQUIRE(bench.setMass(static_cast<float>(i)));
    }
    CHECK(pinsOf(bench, hen) == 1); // 100 commandes : toutes là
    REQUIRE(bench.setMass(500.0F));
    CHECK(pinsOf(bench, hen) == 0); // la 101e l'a poussée dehors
    CHECK(pinsOf(bench, fox) == 0);
}

TEST_CASE("« modifié » se lit dans la pile : annuler jusqu'à l'enregistrement rend la scène propre")
{
    Bench bench;
    CHECK_FALSE(isModified(bench.stack));
    REQUIRE(bench.setMass(1.0F));
    REQUIRE(bench.setMass(2.0F));
    CHECK(isModified(bench.stack));
    markSaved(bench.stack);
    CHECK_FALSE(isModified(bench.stack));

    REQUIRE(bench.setMass(3.0F));
    CHECK(isModified(bench.stack));
    REQUIRE(bench.undo().undone == 1);
    CHECK_FALSE(isModified(bench.stack)); // revenue au point d'enregistrement
    REQUIRE(bench.undo().undone == 1);
    CHECK(isModified(bench.stack));
    REQUIRE(bench.redo().redone == 1);
    CHECK_FALSE(isModified(bench.stack));
}

TEST_CASE(
    "l'enregistrement sorti de la pile par le bas : tout annuler y ramène, la scène est propre")
{
    Bench bench;
    REQUIRE(bench.setMass(1.0F));
    markSaved(bench.stack); // l'état enregistré est celui d'après la commande 1
    for (int i = 2; i <= static_cast<int>(UndoDepth) + 1; ++i)
    {
        REQUIRE(bench.setMass(static_cast<float>(i)));
    }
    CHECK(bench.stack.done.size() == UndoDepth); // la commande 1 est sortie : son état est le fond
    CHECK(isModified(bench.stack));
    REQUIRE(bench.undo(static_cast<int>(UndoDepth) - 1).undone == static_cast<int>(UndoDepth) - 1);
    CHECK(isModified(bench.stack)); // il reste une commande à défaire
    REQUIRE(bench.undo().undone == 1);
    CHECK(bench.kilograms() == 1.0F); // l'état enregistré
    CHECK_FALSE(isModified(bench.stack));
}

TEST_CASE("l'état d'ouverture sorti de la pile : plus de retour à propre sans enregistrer")
{
    Bench bench;
    for (int i = 1; i <= static_cast<int>(UndoDepth) + 1; ++i)
    {
        REQUIRE(bench.setMass(static_cast<float>(i)));
    }
    REQUIRE(bench.undo(static_cast<int>(UndoDepth)).undone == static_cast<int>(UndoDepth));
    CHECK(bench.kilograms() == 1.0F);
    CHECK(isModified(bench.stack)); // l'état d'ouverture (0) n'est plus atteignable
    markSaved(bench.stack);
    CHECK_FALSE(isModified(bench.stack));
}

TEST_CASE("le point d'enregistrement parmi les refaisables vidés : modifié pour de bon")
{
    Bench bench;
    REQUIRE(bench.setMass(1.0F));
    REQUIRE(bench.setMass(2.0F));
    markSaved(bench.stack);
    REQUIRE(bench.undo().undone == 1);
    REQUIRE(bench.setMass(9.0F)); // la commande 2, enregistrée, est vidée
    REQUIRE(bench.undo().undone == 1);
    CHECK(topNumberOf(bench.stack) == 1);
    CHECK(isModified(bench.stack));
}

TEST_CASE("en jeu la pile est gelée, rien n'y entre, Stop la rend utilisable")
{
    levain::core::LogCapture capture = levain::core::captureLog(16);
    Bench bench;
    REQUIRE(bench.setMass(1.0F));
    REQUIRE(bench.setMass(2.0F));
    const std::uint64_t top = topNumberOf(bench.stack);

    for (const Mode playing : {Mode::Play, Mode::Simulate})
    {
        CHECK_FALSE(bench.setMass(7.0F, playing)); // écrit, jamais enregistré
        CHECK(bench.kilograms() == 7.0F);
        const RequestsReport refused = bench.undo(1, playing);
        CHECK(refused.undone == 0);
        CHECK(refused.refused == 1);
        CHECK(bench.redo(1, playing).refused == 1);
        CHECK(bench.kilograms() == 7.0F);
        CHECK(bench.stack.done.size() == 2);
        CHECK(topNumberOf(bench.stack) == top);
        CHECK(historyRefusalOf(bench.stack, playing, false) == HistoryRefusal::NotEditing);
    }
    // Stop rend la scène d'avant Play (ici : à la main), puis la pile sert encore.
    bench.entity.set(Mass{.kilograms = 2.0F});
    REQUIRE(bench.undo().undone == 1);
    CHECK(bench.kilograms() == 1.0F);
}

TEST_CASE("une demande faite en mode différé est refusée bruyamment et gardée")
{
    levain::core::LogCapture capture = levain::core::captureLog(16);
    Bench bench;
    REQUIRE(bench.setMass(4.0F));

    bench.world.defer_begin();
    bench.queue = {HistoryRequest::Undo};
    const RequestsReport inside =
        applyEditorRequests(bench.queue, bench.stack, bench.world, Mode::Edit);
    bench.world.defer_end();

    CHECK(inside.undone == 0);
    CHECK(bench.queue.size() == 1); // gardée pour le point nommé
    CHECK(bench.kilograms() == 4.0F);
    const levain::core::LogSnapshot snapshot = levain::core::snapshotOf(*capture.ring);
    REQUIRE_FALSE(snapshot.entries.empty());
    CHECK(snapshot.entries.back().level == levain::core::LogLevel::Error);
    CHECK(snapshot.entries.back().text.starts_with(
        "Historique : ")); // pas « Annuler » pour un Rétablir
    CHECK(applyEditorRequests(bench.queue, bench.stack, bench.world, Mode::Edit).undone == 1);
    CHECK(bench.kilograms() == 0.0F);
}

TEST_CASE("une commande dont l'entité a disparu est abandonnée, la suivante s'annule")
{
    levain::core::LogCapture capture = levain::core::captureLog(16);
    Bench bench;
    const flecs::entity other = bench.world.entity("autre").set(Mass{});
    const Mass value{.kilograms = 5.0F};
    REQUIRE(editValue(bench.stack, bench.world, Mode::Edit, other, bench.mass(), &value).value());
    REQUIRE(bench.setMass(8.0F));
    REQUIRE(bench.setMass(9.0F)); // la plus récente est sur la cible

    other.destruct(); // hors de la pile
    CHECK(bench.undo().undone == 1);
    const RequestsReport failed = bench.undo(2); // celle de la cible, puis celle de l'autre
    CHECK(failed.undone == 1);
    CHECK(failed.failed == 1);
    CHECK(bench.stack.done.empty()); // abandonnée, pas bloquée
    CHECK(bench.undo().refused == 1);
    // Un seul préfixe de geste, la cause ensuite (pas « Annuler : … (Annuler : …) »).
    bool said = false;
    for (const auto& entry : levain::core::snapshotOf(*capture.ring).entries)
    {
        said = said || entry.text == "Annuler : commande abandonnée, l'entité n'existe plus, le "
                                     "monde n'est pas touché";
    }
    CHECK(said);
}

TEST_CASE("charger une scène vide la pile et rend les épingles")
{
    Bench bench;
    const AssetId fox = levain::assets::generateAssetId();
    const AssetId hen = levain::assets::generateAssetId();
    const flecs::entity model = bench.world.entity().set(MeshRef{.mesh = {.asset = fox}});
    const MeshRef value{.mesh = {.asset = hen}};
    REQUIRE(
        editValue(bench.stack, bench.world, Mode::Edit, model, bench.world.id<MeshRef>(), &value)
            .value());
    REQUIRE(bench.undo().undone == 1);
    REQUIRE(bench.setMass(1.0F));
    markSaved(bench.stack);

    clearUndoStack(bench.stack, bench.world);

    CHECK(bench.stack.done.empty());
    CHECK(bench.stack.undone.empty());
    CHECK(pinsOf(bench, fox) == 0);
    CHECK(pinsOf(bench, hen) == 0);
    CHECK_FALSE(isModified(bench.stack));
}

TEST_CASE("editValue refuse, nommé et le monde intact : entité morte, composant absent, différé")
{
    Bench bench;
    const Mass value{.kilograms = 6.0F};
    const flecs::entity_t dead = bench.world.entity().id();
    bench.world.entity(dead).destruct();
    const flecs::entity bare = bench.world.entity();

    CHECK_FALSE(editValue(bench.stack, bench.world, Mode::Edit, dead, bench.mass(), &value));
    CHECK_FALSE(editValue(bench.stack, bench.world, Mode::Edit, bare, bench.mass(), &value));
    bench.world.defer_begin();
    CHECK_FALSE(
        editValue(bench.stack, bench.world, Mode::Edit, bench.entity, bench.mass(), &value));
    bench.world.defer_end();
    CHECK(bench.kilograms() == 0.0F);
    CHECK(bench.stack.done.empty());
}

TEST_CASE(
    "editValue refuse, nommé et avant d'écrire, un composant que sameValue ne sait pas comparer")
{
    struct Label // non décrit : sameValue s'arrêterait sur une assertion
    {
        float length = 0.0F;
    };

    Bench bench;
    const flecs::entity_t label = bench.world.component<Label>().id();
    bench.entity.set(Label{.length = 1.0F});
    const Label value{.length = 2.0F};

    const auto refused =
        editValue(bench.stack, bench.world, Mode::Edit, bench.entity, label, &value);

    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().message.find("ne se compare pas") != std::string::npos);
    CHECK(bench.entity.get<Label>().length == 1.0F); // rien n'a été écrit
    CHECK(bench.stack.done.empty());
}

namespace
{

/// Les épingles que la pile devrait tenir : la somme de celles de ses commandes.
std::map<AssetId, int> expectedPins(const UndoStack& stack)
{
    std::map<AssetId, int> pins;
    for (const auto* list : {&stack.done, &stack.undone})
    {
        for (const Command& command : *list)
        {
            for (const AssetId asset : std::get<ValueCommand>(command.kind).pin.assets)
            {
                ++pins[asset];
            }
        }
    }
    return pins;
}

std::map<AssetId, int> actualPins(const flecs::world& world)
{
    std::map<AssetId, int> pins;
    for (const auto& [asset, count] : world.get<levain::assets::AssetUsage>().pinned)
    {
        if (count != 0)
        {
            pins[asset] = count;
        }
    }
    return pins;
}

} // namespace

TEST_CASE("150 changements de MeshRef, annuler, refaire, une commande nouvelle, le débordement et "
          "le chargement : les épingles et les références suivent la pile à chaque pas")
{
    levain::core::LogCapture capture = levain::core::captureLog(4096);
    Bench bench;
    const AssetId assets[3] = {levain::assets::generateAssetId(), levain::assets::generateAssetId(),
                               levain::assets::generateAssetId()};
    const flecs::entity model =
        bench.world.entity("modèle").set(MeshRef{.mesh = {.asset = assets[0]}});
    const auto setMesh = [&](AssetId asset)
    {
        const MeshRef value{.mesh = {.asset = asset}};
        return editValue(bench.stack, bench.world, Mode::Edit, model, bench.world.id<MeshRef>(),
                         &value)
            .value();
    };
    const auto current = [&] { return model.get<MeshRef>().mesh.asset; };
    // L'asset de l'entité compte une référence, les autres aucune : les crochets du module suivent
    // chaque écriture de l'annulation comme celle de l'inspecteur.
    const auto referencesRight = [&]
    {
        return std::ranges::all_of(assets,
                                   [&](AssetId asset)
                                   {
                                       return levain::assets::referenceCount(bench.world, asset) ==
                                              (asset == current() ? 1 : 0);
                                   });
    };
    for (int i = 1; i <= 150; ++i)
    {
        REQUIRE(setMesh(assets[i % 3]));
        REQUIRE(actualPins(bench.world) == expectedPins(bench.stack));
    }
    CHECK(bench.stack.done.size() == UndoDepth);
    CHECK(referencesRight());
    for (int i = 0; i < 60; ++i)
    {
        REQUIRE(bench.undo().undone == 1);
        REQUIRE(referencesRight());
        REQUIRE(actualPins(bench.world) == expectedPins(bench.stack));
    }
    CHECK(current() == assets[(150 - 60) % 3]);
    REQUIRE(bench.redo(10).redone == 10);
    CHECK(referencesRight());
    CHECK(actualPins(bench.world) == expectedPins(bench.stack));
    REQUIRE(setMesh(assets[current() == assets[0] ? 1 : 0])); // vide 50 refaisables
    CHECK(bench.stack.undone.empty());
    CHECK(actualPins(bench.world) == expectedPins(bench.stack));
    for (int i = 1; i <= static_cast<int>(UndoDepth); ++i) // pousse dehors toutes les MeshRef
    {
        REQUIRE(bench.setMass(static_cast<float>(i)));
    }
    CHECK(actualPins(bench.world).empty());
    for (int i = 0; i < 30; ++i)
    {
        REQUIRE(setMesh(assets[i % 3] == current() ? assets[(i + 1) % 3] : assets[i % 3]));
    }
    REQUIRE(bench.undo(10).undone == 10);
    clearUndoStack(bench.stack, bench.world);
    CHECK(actualPins(bench.world).empty());
    CHECK(referencesRight());
    // Rendre une épingle sans la tenir est une erreur écrite au journal : aucune ici.
    for (const auto& entry : levain::core::snapshotOf(*capture.ring).entries)
    {
        CHECK(entry.level < levain::core::LogLevel::Warning);
    }
    // Les deux assets inutilisés et non épinglés sortent.
    CHECK(levain::assets::takeUnusedAssets(bench.world).size() == 2);
}
