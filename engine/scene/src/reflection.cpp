#include "levain/scene/reflection.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <format>
#include <string>

#include "levain/core/assert.hpp"
#include "levain/core/log.hpp"

namespace levain::scene
{

namespace detail
{

/// Posée par chaque déclaration, et non par la description d'un type imbriqué : c'est elle qui
/// distingue un `describe` d'un type jamais déclaré.
struct Declared
{
};

} // namespace detail

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

ecs_member_t* memberAt(flecs::world& world, flecs::entity_t component, std::size_t offset)
{
    for (std::int32_t i = 0;; ++i)
    {
        ecs_member_t* member = ecs_struct_get_nth_member(world, component, i);
        if (member == nullptr || std::cmp_equal(member->offset, offset))
        {
            return member;
        }
    }
}

/// `count` feuilles contiguës d'un même type (un champ tableau), une à une.
bool sameElements(const flecs::world& world, flecs::entity_t type, std::int32_t count,
                  const std::byte* first, const std::byte* second)
{
    const auto size = static_cast<std::size_t>(ecs_get_type_info(world, type)->size);
    for (std::size_t i = 0; std::cmp_less(i, count); ++i)
    {
        if (!sameValue(world, type, first + (i * size), second + (i * size)))
        {
            return false;
        }
    }
    return true;
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

void declare(flecs::world& world, flecs::entity_t component, bool authored)
{
    const flecs::entity type{world, component};
    if (type.has<Declared>() && type.has<Authored>() != authored)
    {
        exitOnRefusedDescription(std::format("{} est déclaré par describe et par "
                                             "describeAuthored : une seule déclaration fait foi",
                                             stableKeyOf(world, component)));
    }
    type.add<Declared>();
    if (authored)
    {
        type.add<Authored>();
    }
}

void setFieldRange(flecs::world& world, flecs::entity_t component, std::size_t offset, double min,
                   double max)
{
    ecs_member_t* member = memberAt(world, component, offset);
    if (member == nullptr) // un type décrit d'abord à la main, autrement
    {
        exitOnRefusedDescription(std::format("range sur le champ au décalage {} de {}, absent de "
                                             "sa description",
                                             offset, stableKeyOf(world, component)));
    }
    if (!(min < max)) // NaN compris
    {
        exitOnRefusedDescription(std::format("la borne [{}, {}] du champ « {} » de {} est vide ou "
                                             "inversée",
                                             min, max, member->name,
                                             stableKeyOf(world, component)));
    }
    const std::optional<ecs_member_value_range_t> previous = rangeOf(*member);
    if (previous.has_value() && (previous->min != min || previous->max != max))
    {
        exitOnRefusedDescription(std::format("le champ « {} » de {} a deux bornes, [{}, {}] puis "
                                             "[{}, {}] : une seule déclaration fait foi",
                                             member->name, stableKeyOf(world, component),
                                             previous->min, previous->max, min, max));
    }
    // Comme `untyped_component::range` de flecs sans entité de membre
    // (addons/cpp/mixins/meta/untyped_component.inl) : la borne vit dans `ecs_member_t`.
    member->range = {.min = min, .max = max};
}

} // namespace detail

void setComponentValue(flecs::world& world, flecs::entity_t entity, flecs::entity_t component,
                       const void* value)
{
    const ecs_type_info_t* info = ecs_get_type_info(world, component);
    LEVAIN_ASSERT(info != nullptr && info->size > 0, "setComponentValue : pas un composant valué");
    ecs_set_id(world, entity, component, static_cast<std::size_t>(info->size), value);
}

bool sameValue(const flecs::world& world, flecs::entity_t type, const void* first,
               const void* second)
{
    if (ecs_get_type_info(world, type) == nullptr)
    {
        LEVAIN_ASSERT(ecs_has(world, type, EcsComponent), "sameValue : pas un type");
        return true; // une étiquette (`describeAuthored<Cube>`) n'a pas de valeur
    }
    const auto* left = static_cast<const std::byte*>(first);
    const auto* right = static_cast<const std::byte*>(second);
    if (const auto* description = ecs_get(world, type, EcsStruct))
    {
        const auto* members = static_cast<const ecs_member_t*>(description->members.array);
        for (std::int32_t i = 0; i < description->members.count; ++i)
        {
            const ecs_member_t& member = members[i]; // count : 0 pour un scalaire, N en ligne
            if (!sameElements(world, member.type, member.count > 0 ? member.count : 1,
                              left + member.offset, right + member.offset))
            {
                return false;
            }
        }
        return true;
    }
    // Un flecs::entity porte le monde à côté de l'identifiant, et du remplissage en wasm32.
    if (type == world.id<flecs::entity>())
    {
        return static_cast<const flecs::entity*>(first)->id() ==
               static_cast<const flecs::entity*>(second)->id();
    }
    LEVAIN_ASSERT(ecs_has(world, type, EcsPrimitive) || ecs_has(world, type, EcsEnum) ||
                      ecs_has(world, type, EcsBitmask),
                  "sameValue : une feuille sans remplissage, nombre, booléen ou enum");
    return std::memcmp(left, right,
                       static_cast<std::size_t>(ecs_get_type_info(world, type)->size)) == 0;
}

std::string_view stableKeyOf(const flecs::world& world, flecs::entity_t component)
{
    const char* symbol = ecs_get_symbol(world, component);
    return symbol != nullptr ? std::string_view{symbol} : std::string_view{};
}

flecs::entity_t componentOfKey(const flecs::world& world, std::string_view key)
{
    // Ni chemin en second essai, ni remontée des portées : une clé se relit comme elle s'écrit.
    return ecs_lookup_symbol(world, std::string{key}.c_str(), false, false);
}

std::optional<ecs_member_value_range_t> rangeOf(const ecs_member_t& member)
{
    if (member.range.min == member.range.max)
    {
        return std::nullopt;
    }
    return member.range;
}

} // namespace levain::scene
