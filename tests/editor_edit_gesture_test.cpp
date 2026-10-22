#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

#include <doctest/doctest.h>
#include <flecs.h>

#include "levain/editor/edit_gesture.hpp"
#include "levain/scene/reflection.hpp"
#include "levain/scene/scene.hpp"

// Le geste d'édition de l'inspecteur, sans ImGui (ADR-0038, décision 2, morceau A5) : la machine
// reçoit, image par image, les bords que l'interface a vus et ce que l'inspecteur a écrit.

using namespace levain::editor;

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

/// La valeur d'un optional que le test vient d'exiger : l'exception dit l'oubli, et l'analyse
/// statique voit le chemin.
template <class T> const T& required(const std::optional<T>& value)
{
    if (!value)
    {
        throw std::logic_error{"valeur absente"};
    }
    return *value;
}

constexpr std::uint32_t FieldX = 11; ///< l'identifiant actif d'un champ
constexpr std::uint32_t FieldY = 12;

struct Rig
{
    flecs::world world;
    UndoStack stack;
    EditGesture gesture;
    std::vector<ComponentFrame> frames;
    flecs::entity first;
    flecs::entity second;

    Rig()
    {
        world.import<levain::scene::SceneModule>();
        levain::scene::describeAuthored<Mass>(world);
        levain::scene::describeAuthored<Charge>(world);
        first = world.entity("premiere").set(Mass{}).set(Charge{});
        second = world.entity("seconde").set(Mass{}).set(Charge{});
    }

    [[nodiscard]] flecs::entity_t componentOf(bool charge)
    {
        return charge ? world.id<Charge>() : world.id<Mass>();
    }

    /// Ce que fait l'inspecteur pour un composant : copier l'avant **avant** d'écrire, écrire, et
    /// laisser son image à la machine.
    void touch(flecs::entity entity, std::optional<float> write, GestureEdges edges,
               bool charge = false)
    {
        const flecs::entity_t component = componentOf(charge);
        ComponentFrame frame;
        frame.entity = entity;
        frame.component = component;
        frame.edges = edges;
        frame.wrote = write.has_value();
        if (frame.wrote || edges.activated)
        {
            frame.before.emplace(world, ecs_get_typeid(world, component),
                                 ecs_get_id(world, entity, component));
        }
        if (write)
        {
            if (charge)
            {
                const Charge value{.coulombs = *write};
                levain::scene::setComponentValue(world, entity, component, &value);
            }
            else
            {
                const Mass value{.kilograms = *write};
                levain::scene::setComponentValue(world, entity, component, &value);
            }
        }
        frames.push_back(std::move(frame));
    }

    int settle(std::uint32_t activeId, flecs::entity inspected, Mode mode = Mode::Edit)
    {
        return settleGestures(gesture, stack, world, mode, frames, inspected, activeId);
    }

    /// Une image qui touche un seul champ de `first`.
    int frame(std::optional<float> write, GestureEdges edges, std::uint32_t activeId)
    {
        touch(first, write, edges);
        return settle(activeId, first);
    }

    float mass(flecs::entity entity) { return entity.get<Mass>().kilograms; }
};

constexpr GestureEdges Press{.activated = true, .deactivatedAfterEdit = false};
constexpr GestureEdges Release{.activated = false, .deactivatedAfterEdit = true};
/// Échap dans un champ tapé : ImGui rejoue le texte formaté à l'activation et l'applique, d'où
/// `deactivatedAfterEdit` aussi (probe de la relecture, ImGui 1.92.8).
constexpr GestureEdges Escape{.activated = false, .deactivatedAfterEdit = true, .cancelled = true};
constexpr GestureEdges None{};

} // namespace

TEST_CASE("un glissé de 30 images est une commande, l'avant est celui du début")
{
    Rig rig;
    rig.first.set(Mass{.kilograms = 2.0F});
    int posted = rig.frame(std::nullopt, Press, FieldX); // le bouton se referme sur le champ
    for (int i = 1; i <= 30; ++i)
    {
        posted += rig.frame(2.0F + static_cast<float>(i), None, FieldX);
    }
    CHECK(posted == 0); // rien ne se pose tant que le champ est tenu
    CHECK(rig.stack.done.empty());
    posted += rig.frame(std::nullopt, Release, FieldX);
    CHECK(posted == 1);
    REQUIRE(rig.stack.done.size() == 1);
    const auto& command = std::get<ValueCommand>(rig.stack.done.back().kind);
    CHECK(static_cast<const Mass*>(command.before.data())->kilograms == 2.0F);
    CHECK(static_cast<const Mass*>(command.after.data())->kilograms == 32.0F);
    CHECK_FALSE(rig.gesture.open.has_value());
}

TEST_CASE("relâcher puis reprendre le champ : deux commandes, pas de fusion")
{
    Rig rig;
    int posted = rig.frame(std::nullopt, Press, FieldX);
    posted += rig.frame(1.0F, None, FieldX);
    posted += rig.frame(std::nullopt, Release, FieldX);
    posted += rig.frame(std::nullopt, Press, FieldX); // le même champ, le même identifiant actif
    posted += rig.frame(2.0F, None, FieldX);
    posted += rig.frame(std::nullopt, Release, FieldX);
    CHECK(posted == 2);
    CHECK(rig.stack.done.size() == 2);
}

TEST_CASE("une écriture sans geste ouvert est une commande d'une image, l'avant copié avant")
{
    Rig rig;
    // Une liste déroulante : la valeur change, aucun bord.
    CHECK(rig.frame(5.0F, None, 0) == 1);
    REQUIRE(rig.stack.done.size() == 1);
    const auto& command = std::get<ValueCommand>(rig.stack.done.back().kind);
    CHECK(static_cast<const Mass*>(command.before.data())->kilograms == 0.0F);
    CHECK(static_cast<const Mass*>(command.after.data())->kilograms == 5.0F);
}

TEST_CASE("sans écriture ni bord, une image ne pose rien : une liste sans choix, 0")
{
    Rig rig;
    CHECK(rig.frame(std::nullopt, None, 0) == 0);
    CHECK(rig.frame(std::nullopt, None, 0) == 0);
    CHECK(rig.stack.done.empty());
}

TEST_CASE("un champ pris puis relâché sans changer ne pose rien")
{
    Rig rig;
    CHECK(rig.frame(std::nullopt, Press, FieldX) == 0);
    CHECK(rig.frame(std::nullopt, None, FieldX) == 0);
    // Relâché sans changement : pas de bord `AfterEdit`, l'élément n'est plus l'actif.
    CHECK(rig.frame(std::nullopt, None, 0) == 0);
    CHECK(rig.stack.done.empty());
    CHECK_FALSE(rig.gesture.open.has_value());
}

TEST_CASE("Tab de x à y : fermer avant d'ouvrir, deux commandes")
{
    Rig rig;
    int posted = rig.frame(std::nullopt, Press, FieldX);
    posted += rig.frame(4.0F, None, FieldX); // x tapé
    // La même image : la fin de x (la valeur tapée s'applique) et le début de y.
    posted += rig.frame(7.0F, {.activated = true, .deactivatedAfterEdit = true}, FieldY);
    CHECK(posted == 1); // x est fermé, y est ouvert
    REQUIRE(rig.gesture.open.has_value());
    // L'avant de y est le monde après l'écriture de x : annuler y n'annule pas x.
    CHECK(static_cast<const Mass*>(required(rig.gesture.open).before.data())->kilograms == 7.0F);
    posted += rig.frame(9.0F, None, FieldY);
    posted += rig.frame(std::nullopt, Release, FieldY);
    CHECK(posted == 2);
    CHECK(rig.stack.done.size() == 2);
    const auto& x = std::get<ValueCommand>(rig.stack.done.front().kind);
    CHECK(static_cast<const Mass*>(x.before.data())->kilograms == 0.0F);
    CHECK(static_cast<const Mass*>(x.after.data())->kilograms == 7.0F);
}

TEST_CASE("Maj+Tab d'un composant à l'autre : x ferme avant que y ouvre, deux commandes")
{
    Rig rig;
    rig.frame(std::nullopt, Press, FieldX); // x : la masse
    rig.frame(3.0F, None, FieldX);
    // Maj+Tab : y (la charge) est dessinée avant x, son bord d'ouverture arrive avant la fin de x,
    // dont la valeur tapée s'applique dans cette image.
    rig.touch(rig.first, std::nullopt, Press, true);
    rig.touch(rig.first, 6.0F, Release);
    CHECK(rig.settle(FieldY, rig.first) == 1); // la fin de x : une commande, à la valeur finale
    REQUIRE(rig.stack.done.size() == 1);
    const auto& x = std::get<ValueCommand>(rig.stack.done.back().kind);
    CHECK(static_cast<const Mass*>(x.before.data())->kilograms == 0.0F);
    CHECK(static_cast<const Mass*>(x.after.data())->kilograms == 6.0F);
    REQUIRE(rig.gesture.open.has_value());
    CHECK(required(rig.gesture.open).component == rig.world.id<Charge>());
    rig.touch(rig.first, 2.0F, None, true);
    CHECK(rig.settle(FieldY, rig.first) == 0);
    rig.touch(rig.first, std::nullopt, Release, true);
    CHECK(rig.settle(FieldY, rig.first) == 1);
    CHECK(rig.stack.done.size() == 2);
}

TEST_CASE("Échap : l'avant exact revient, aucune commande, même si le texte formaté en diffère")
{
    Rig rig;
    constexpr float Inexact = 0.12345678F; // « %.3f » l'écrit 0.123
    rig.first.set(Mass{.kilograms = Inexact});
    int posted = rig.frame(std::nullopt, Press, FieldX);
    posted += rig.frame(9.0F, None, FieldX); // frappe : écrite en direct
    posted += rig.frame(0.123F, Escape, 0);  // ImGui rejoue le texte d'avant, arrondi
    posted += rig.frame(std::nullopt, None, 0);
    CHECK(posted == 0);
    CHECK(rig.stack.done.empty());
    CHECK(rig.mass(rig.first) == Inexact); // l'avant exact, pas 0.123
    CHECK_FALSE(rig.gesture.open.has_value());
}

TEST_CASE("retaper la même valeur : l'avant égale l'après, la pile ne bouge pas")
{
    Rig rig;
    rig.first.set(Mass{.kilograms = 3.0F});
    int posted = rig.frame(std::nullopt, Press, FieldX);
    posted += rig.frame(std::nullopt, Release, FieldX); // rien n'a changé : le monde est égal
    CHECK(posted == 0);
    CHECK(rig.stack.done.empty());
    // Écrit loin puis revenu à la même valeur dans le même geste.
    posted += rig.frame(std::nullopt, Press, FieldX);
    posted += rig.frame(9.0F, None, FieldX);
    posted += rig.frame(3.0F, None, FieldX);
    posted += rig.frame(std::nullopt, Release, FieldX);
    CHECK(posted == 0);
}

TEST_CASE("une entité qui change ferme le geste à la fin de l'image")
{
    Rig rig;
    rig.frame(std::nullopt, Press, FieldX);
    rig.frame(5.0F, None, FieldX);
    // La sélection change : l'inspecteur dessine `second`, `first` n'est plus inspectée.
    rig.touch(rig.second, std::nullopt, None);
    CHECK(rig.settle(FieldX, rig.second) == 1);
    CHECK_FALSE(rig.gesture.open.has_value());
    REQUIRE(rig.stack.done.size() == 1);
    // Le glissé continue sur le champ, qui est maintenant celui de `second` : son écriture rouvre
    // un geste, rien ne se perd ; la commande se pose au relâchement.
    rig.touch(rig.second, 2.0F, None);
    CHECK(rig.settle(FieldX, rig.second) == 0);
    CHECK(rig.gesture.open.has_value());
    CHECK(rig.stack.done.size() == 1);
    CHECK(rig.settle(0, rig.second) == 1);
    CHECK(rig.stack.done.size() == 2);
    // Une fois lâché, une écriture sans geste redevient une commande d'une image.
    rig.touch(rig.second, 4.0F, None);
    CHECK(rig.settle(0, rig.second) == 1);
}

TEST_CASE("une entité détruite pendant le geste : abandonné, aucune commande sur elle")
{
    Rig rig;
    rig.frame(std::nullopt, Press, FieldX);
    rig.frame(5.0F, None, FieldX);
    rig.first.destruct();
    CHECK(rig.settle(FieldX, flecs::entity{}) == 0);
    CHECK_FALSE(rig.gesture.open.has_value());
    CHECK(rig.stack.done.empty());
}

TEST_CASE("la fenêtre fermée en plein glissé : le geste se ferme à l'image suivante")
{
    Rig rig;
    rig.frame(std::nullopt, Press, FieldX);
    rig.frame(5.0F, None, FieldX);
    // L'inspecteur ne dessine plus rien (fenêtre repliée) : aucun composant, aucune entité.
    CHECK(rig.settle(0, flecs::entity{}) == 1);
    CHECK_FALSE(rig.gesture.open.has_value());
}

TEST_CASE("tout geste ouvert est fermé avant Play : une commande, ni perdue ni doublée")
{
    Rig rig;
    rig.frame(std::nullopt, Press, FieldX);
    rig.frame(5.0F, None, FieldX);
    CHECK(closeOpenGesture(rig.gesture, rig.stack, rig.world, Mode::Edit) == 1);
    CHECK(rig.stack.done.size() == 1);
    CHECK(closeOpenGesture(rig.gesture, rig.stack, rig.world, Mode::Edit) == 0); // pas doublée
    // Play refusé : le glissé continue. Sa suite rouvre un geste, que le relâchement ferme : deux
    // commandes, et Rétablir atteint la valeur finale.
    CHECK(rig.frame(6.0F, None, FieldX) == 0);
    CHECK(rig.frame(7.0F, None, FieldX) == 0);
    CHECK(rig.frame(std::nullopt, Release, FieldX) == 1);
    REQUIRE(rig.stack.done.size() == 2);
    const auto& last = std::get<ValueCommand>(rig.stack.done.back().kind);
    CHECK(static_cast<const Mass*>(last.before.data())->kilograms == 5.0F);
    CHECK(static_cast<const Mass*>(last.after.data())->kilograms == 7.0F);
}

TEST_CASE("hors de l'Édition rien n'est posé et le geste est lâché")
{
    Rig rig;
    rig.frame(std::nullopt, Press, FieldX);
    rig.touch(rig.first, 5.0F, None);
    CHECK(rig.settle(FieldX, rig.first, Mode::Play) == 0);
    CHECK_FALSE(rig.gesture.open.has_value());
    CHECK(rig.stack.done.empty());
    rig.touch(rig.first, 6.0F, None);
    CHECK(rig.settle(0, rig.first, Mode::Play) == 0);
    CHECK(rig.stack.done.empty());
}

TEST_CASE("annuler la commande d'un glissé rend l'avant du début, pas celui d'une image")
{
    Rig rig;
    rig.first.set(Mass{.kilograms = 1.0F});
    rig.frame(std::nullopt, Press, FieldX);
    for (int i = 0; i < 5; ++i)
    {
        rig.frame(10.0F + static_cast<float>(i), None, FieldX);
    }
    rig.frame(std::nullopt, Release, FieldX);
    std::vector<EditorRequest> queue{RequestKind::Undo};
    CHECK(applyEditorRequests(queue, rig.stack, rig.world, Mode::Edit).undone == 1);
    CHECK(rig.mass(rig.first) == 1.0F);
}
