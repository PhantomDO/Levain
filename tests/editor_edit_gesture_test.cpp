#include <cstdint>
#include <optional>
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

constexpr std::uint32_t FieldX = 11; ///< l'identifiant actif d'un champ

struct Rig
{
    flecs::world world;
    UndoStack stack;
    EditGesture gesture;
    std::vector<ComponentFrame> frames;
    flecs::entity first;

    Rig()
    {
        world.import<levain::scene::SceneModule>();
        levain::scene::describeAuthored<Mass>(world);
        first = world.entity("premiere").set(Mass{});
    }

    /// Ce que fait l'inspecteur pour un composant : copier l'avant **avant** d'écrire, écrire, et
    /// laisser son image à la machine.
    void touch(flecs::entity entity, std::optional<float> write, GestureEdges edges)
    {
        const flecs::entity_t component = world.id<Mass>();
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
            const Mass value{.kilograms = *write};
            levain::scene::setComponentValue(world, entity, component, &value);
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
