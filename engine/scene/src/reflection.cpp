#include "levain/scene/reflection.hpp"

#include <cstdint>
#include <cstdlib>
#include <format>
#include <string>

#include "levain/core/log.hpp"

namespace levain::scene
{

namespace
{

/// exitOnRefusedDescription : un refus s'écrit au journal, puis arrête le programme, en Release
/// aussi (règle n°7) : flecs écarterait le champ du JSON d'une ligne de journal, et une borne
/// fausse cesserait de borner l'inspecteur sans un mot. Ni une assertion, qui disparaît en Release,
/// ni `std::abort`, dont le signal ferait d'un refus attendu un plantage pour CTest ; spdlog a vidé
/// sa sortie, `_Exit` ne perd rien.
[[noreturn]] void exitOnRefusedDescription(const std::string& refusal)
{
    core::log("scene", core::LogLevel::Critical, "réflexion : {} (ADR-0034)", refusal);
    std::_Exit(EXIT_FAILURE);
}

/// hasReflection : flecs sait-il écrire ce type ? Une enum sans constante (`std::byte`) a une
/// description, mais aucune valeur n'y a de nom : une seule viderait le `to_json` de l'entité.
bool hasReflection(const flecs::world& world, flecs::entity_t type)
{
    const auto* constants = ecs_get(world, type, EcsConstants);
    return ecs_has(world, type, EcsType) &&
           (constants == nullptr || ecs_vec_count(&constants->ordered_constants) > 0);
}

/// refuseFieldWithoutReflection : un champ dont le type n'a pas de réflexion arrête l'import en le
/// nommant.
[[noreturn]] void refuseFieldWithoutReflection(const flecs::world& world, flecs::entity_t owner,
                                               std::string_view field, flecs::entity_t type)
{
    exitOnRefusedDescription(std::format("le champ « {} » de {} a un type sans réflexion ({}) : "
                                         "le décrire à la main ou en changer",
                                         field, stableKeyOf(world, owner),
                                         flecs::entity(world, type).path(".", "").c_str()));
}

} // namespace

namespace detail
{

void addField(flecs::world& world, flecs::entity_t owner, std::string_view name,
              flecs::entity_t type, std::size_t offset)
{
    if (!hasReflection(world, type))
    {
        refuseFieldWithoutReflection(world, owner, name, type);
    }
    const std::string terminated{name}; // flecs copie le nom, qu'il veut terminé par \0
    ecs_member_t member{};
    member.name = terminated.c_str();
    member.type = type;
    member.offset = static_cast<std::int32_t>(offset);
    // Le décalage lu dans la struct fait foi, celui d'un premier champ (0) aussi : flecs ne le
    // recalcule pas (`ecs_member_t::use_offset`, addons/meta.h).
    member.use_offset = true;
    if (ecs_struct_add_member(world, owner, &member) != 0)
    {
        exitOnRefusedDescription(std::format("flecs refuse le champ « {} » de {}, son journal dit "
                                             "pourquoi",
                                             name, stableKeyOf(world, owner)));
    }
}

} // namespace detail

std::string_view stableKeyOf(const flecs::world& world, flecs::entity_t component)
{
    const char* symbol = ecs_get_symbol(world, component);
    return symbol != nullptr ? std::string_view{symbol} : std::string_view{};
}

} // namespace levain::scene
