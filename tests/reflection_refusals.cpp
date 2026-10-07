// Les refus de la réflexion à l'import d'un module (ADR-0034), un cas par lancement : chacun doit
// arrêter le programme en nommant la struct et le champ. CTest lit la sortie : WILL_FAIL passerait
// sur n'importe quel plantage, doctest n'a pas de test de mort, et « aucun refus » fait échouer.

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
    glm::vec2 offset{0.0f}; // SceneModule ne décrit pas glm::vec2
};

struct WithByte
{
    std::byte raw{}; // une enum sans constante : flecs n'écrirait aucune valeur
};

/// Les déclarations de chaque cas. Une table locale : globale, elle serait construite avant `main`,
/// hors de son `try`.
void declare(flecs::world& world, std::string_view refusal)
{
    using levain::scene::describe;
    const std::map<std::string_view, void (*)(flecs::world&)> cases{
        {"optional", [](flecs::world& world) { describe<WithOptional>(world); }},
        {"glm-leaf", [](flecs::world& world) { describe<WithLeaf>(world); }},
        {"byte", [](flecs::world& world) { describe<WithByte>(world); }},
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
