// Les refus de la réflexion à l'import d'un module (ADR-0034), un cas par lancement : chacun doit
// arrêter le programme en nommant la struct et le champ. CTest lit la sortie : WILL_FAIL passerait
// sur n'importe quel plantage, doctest n'a pas de test de mort, et « aucun refus » fait échouer.

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <map>
#include <optional>
#include <span>
#include <string_view>

#include <flecs.h>
#include <glm/glm.hpp>

#include "levain/scene/reflection.hpp"
#include "levain/scene/scene.hpp"

namespace refusals
{

struct WithOptional
{
    std::optional<float> maybe;
};

struct WithLeaf
{
    glm::vec4 offset{0.0f}; // SceneModule ne décrit pas glm::vec4
};

struct WithByte
{
    std::byte raw{}; // une enum sans constante : flecs n'écrirait aucune valeur
};

struct Ranged
{
    float speed = 1.0f;
};

struct HandMade // décrit d'abord à la main, sans son second champ
{
    float kept = 0.0f;
    float missing = 0.0f;
};

/// Les déclarations de chaque cas. Une table locale : globale, elle serait construite avant `main`,
/// hors de son `try`.
void declare(flecs::world& world, std::string_view refusal)
{
    using levain::scene::describe;
    using levain::scene::describeAuthored;
    const std::map<std::string_view, void (*)(flecs::world&)> cases{
        {"optional", [](flecs::world& world) { describe<WithOptional>(world); }},
        {"glm-leaf", [](flecs::world& world) { describe<WithLeaf>(world); }},
        {"byte", [](flecs::world& world) { describe<WithByte>(world); }},
        {"empty-range",
         [](flecs::world& world) { describe<Ranged>(world).range(&Ranged::speed, 1.0, 1.0); }},
        {"inverted-range",
         [](flecs::world& world) { describe<Ranged>(world).range(&Ranged::speed, 2.0, 1.0); }},
        {"nan-range", [](flecs::world& world)
         { describe<Ranged>(world).range(&Ranged::speed, std::nan(""), 1.0); }},
        {"two-ranges",
         [](flecs::world& world)
         {
             describe<Ranged>(world).range(&Ranged::speed, 0.0, 1.0);
             describe<Ranged>(world).range(&Ranged::speed, 0.0, 2.0);
         }},
        {"range-unknown-field",
         [](flecs::world& world)
         {
             world.component<HandMade>().member<float>("kept");
             describe<HandMade>(world).range(&HandMade::missing, 0.0, 1.0);
         }},
        {"read-only-then-authored",
         [](flecs::world& world)
         {
             describe<Ranged>(world);
             describeAuthored<Ranged>(world);
         }},
        {"authored-then-read-only",
         [](flecs::world& world)
         {
             describeAuthored<Ranged>(world);
             describe<Ranged>(world);
         }},
    };
    cases.at(refusal)(world);
}

} // namespace refusals

int main(int argc, char** argv)
{
    try
    {
        const std::span arguments{argv, static_cast<std::size_t>(argc)};
        flecs::world world;
        world.import<levain::scene::SceneModule>();
        refusals::declare(world, arguments.size() == 2 ? arguments[1] : "");
        std::puts("aucun refus"); // le motif de FAIL_REGULAR_EXPRESSION : le test échoue
        return 0;
    }
    catch (const std::exception& e)
    {
        std::fputs(e.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
}
