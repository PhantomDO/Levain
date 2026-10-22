#include <optional>
#include <stdexcept>
#include <string>

#include <doctest/doctest.h>
#include <flecs.h>

#include "levain/editor/undo_session.hpp"
#include "levain/scene/components.hpp"
#include "levain/scene/scene.hpp"

// La session scriptée de la CI de l'annulation (ADR-0038, morceau A5), sans fenêtre : les nombres
// de la ligne `editor.undo` sont exigés tels quels (règle n°7).

using namespace levain::editor;

namespace
{

/// Le bilan que le test vient d'exiger : l'exception dit l'oubli, et l'analyse statique voit le
/// chemin.
const UndoSessionReport& required(const std::optional<UndoSessionReport>& report)
{
    if (!report)
    {
        throw std::logic_error{"pas de bilan"};
    }
    return *report;
}

struct Session
{
    flecs::world world;
    UndoStack stack;
    UndoSession session;

    Session()
    {
        world.import<levain::scene::SceneModule>();
        world.entity("caisse").set(levain::scene::Transform{});
        world.entity("autre").set(levain::scene::Transform{});
        auto started = startUndoSession(world, "caisse");
        REQUIRE(started.has_value());
        session = std::move(*started);
    }

    /// Les images de la session jusqu'au bilan ; vide si elle ne finit pas dans `frames`.
    std::optional<UndoSessionReport> run(int frames = 300)
    {
        for (int i = 0; i < frames; ++i)
        {
            if (auto report = stepUndoSession(session, stack, world, Mode::Edit))
            {
                return report;
            }
        }
        return std::nullopt;
    }
};

} // namespace

TEST_CASE("la session fait 150 changements, 100 annulations et rend zéro différence")
{
    Session bench;
    const auto report = bench.run();
    REQUIRE(report.has_value());
    const UndoSessionReport& done = required(report);
    CHECK(done.changes == 150);
    CHECK(done.undone == 100);
    CHECK(done.differences == 0);
    // L'état est celui d'après le 50e changement, et la pile a gardé ses 100 niveaux.
    CHECK(bench.world.lookup("caisse").get<levain::scene::Transform>().position.x == 50.0F);
    CHECK(bench.stack.undone.size() == 100);
    CHECK(bench.stack.done.empty());       // les 100 niveaux sont tous défaits
    CHECK_FALSE(bench.run(5).has_value()); // le bilan ne se rend qu'une fois
}

TEST_CASE("une pile de 99 niveaux : la session le dit à la première annulation refusée")
{
    Session bench;
    // Une pile qui ne garde que 99 commandes : la centième annulation n'a rien à défaire.
    std::optional<UndoSessionReport> report;
    for (int i = 0; i < 270 && !report; ++i)
    {
        if (bench.stack.done.size() > 99)
        {
            bench.stack.done.erase(bench.stack.done.begin());
        }
        report = stepUndoSession(bench.session, bench.stack, bench.world, Mode::Edit);
    }
    REQUIRE(report.has_value());
    const UndoSessionReport& seen = required(report);
    CHECK(seen.changes == 150);
    CHECK(seen.undone == 99);
    CHECK(seen.differences > 0);
    CHECK(seen.differencesText.find("annulation 100 refusée") != std::string::npos);
    // La ligne de la CI tombe sur sa branche rouge, avec la cause.
    CHECK(undoLineOf(seen).starts_with("editor.undo changes=150 undone=99 differences=1 -- "));
}

TEST_CASE("un changement refusé est dit à la première image, avec la cause")
{
    Session bench;
    bench.world.entity("caisse").remove<levain::scene::Transform>();
    const auto report = bench.run(5);
    REQUIRE(report.has_value());
    const UndoSessionReport& seen = required(report);
    CHECK(seen.changes == 0);
    CHECK(seen.differences > 0);
    CHECK(seen.differencesText.find("changement 1 refusé") != std::string::npos);
}

TEST_CASE("une annulation qui ne rend pas l'état est comptée en différences")
{
    Session bench;
    int frame = 0;
    std::optional<UndoSessionReport> report;
    for (; frame < 300 && !report; ++frame)
    {
        report = stepUndoSession(bench.session, bench.stack, bench.world, Mode::Edit);
        if (bench.session.undone == UndoSessionUndos && !report)
        {
            // Un monde modifié derrière la pile, sans commande, après la dernière annulation : le
            // bilan doit le voir.
            auto caisse = bench.world.lookup("caisse");
            auto transform = caisse.get<levain::scene::Transform>();
            transform.position.y = 123.0F;
            caisse.set(transform);
        }
    }
    REQUIRE(report.has_value());
    const UndoSessionReport& seen = required(report);
    CHECK(seen.differences > 0);
    CHECK(seen.differencesText.find("caisse") != std::string::npos);
}

TEST_CASE("une session sans cible est refusée, nommée")
{
    flecs::world world;
    world.import<levain::scene::SceneModule>();
    world.entity("sans transform");
    const auto missing = startUndoSession(world, "absente");
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().message.find("absente") != std::string::npos);
    CHECK_FALSE(startUndoSession(world, "sans transform").has_value());
}

TEST_CASE("la ligne de la CI a un nom stable, le pluriel français vient après")
{
    CHECK(undoLineOf({.changes = 150, .undone = 100, .differences = 0, .differencesText = {}})
              .starts_with("editor.undo changes=150 undone=100 differences=0 -- "));
    CHECK(undoLineOf({.changes = 150, .undone = 99, .differences = 2, .differencesText = {}})
              .starts_with("editor.undo changes=150 undone=99 differences=2 -- "));
}
