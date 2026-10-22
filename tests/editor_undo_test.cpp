#include <cstdint>
#include <vector>

#include <doctest/doctest.h>
#include <flecs.h>

#include "levain/core/log_ring.hpp"
#include "levain/editor/undo.hpp"
#include "levain/scene/reflection.hpp"
#include "levain/scene/scene.hpp"

// La pile d'annulation (ADR-0038, décisions 1 et 9, morceau A4) : 100 niveaux par les valeurs,
// « modifié » lu dans la pile, la pile gelée en jeu.

using namespace levain::editor;
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
