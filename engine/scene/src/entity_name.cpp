#include "levain/scene/entity_name.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>

namespace levain::scene
{

namespace
{

/// « # » suivi d'au moins un chiffre, et rien d'autre : ce que `flecs_name_is_id` prend pour un
/// identifiant (src/entity_name.c:109-125).
bool looksLikeEntityId(std::string_view name)
{
    return name.size() > 1 && name.front() == '#' &&
           std::ranges::all_of(name.substr(1),
                               [](unsigned char c) { return std::isdigit(c) != 0; });
}

/// Le nom sans son numéro final : « Cube 2 » devient « Cube ». Un nom qui n'est que « 2 » reste tel
/// quel, pour ne pas fabriquer un nom vide.
std::string_view baseOfNumbered(std::string_view name)
{
    const std::size_t space = name.rfind(' ');
    if (space == std::string_view::npos || space == 0 || space + 1 == name.size())
    {
        return name;
    }
    const std::string_view tail = name.substr(space + 1);
    const bool numbered =
        std::ranges::all_of(tail, [](unsigned char c) { return std::isdigit(c) != 0; });
    return numbered ? name.substr(0, space) : name;
}

/// Un frère porte déjà ce nom ? flecs répond par son index de noms, la vérité de `lookup`.
bool takenAmongSiblings(const flecs::world& world, flecs::entity_t parent, const std::string& name,
                        flecs::entity_t self)
{
    const flecs::entity_t found = ecs_lookup_child(world, parent, name.c_str());
    return found != 0 && found != self;
}

} // namespace

std::optional<NameRefusal> refusalOfName(std::string_view name, bool atRoot)
{
    if (name.empty())
    {
        return NameRefusal::Empty;
    }
    if (name.find('.') != std::string_view::npos)
    {
        return NameRefusal::ContainsDot;
    }
    if (name.find("::") != std::string_view::npos)
    {
        return NameRefusal::ContainsScope;
    }
    if (looksLikeEntityId(name))
    {
        return NameRefusal::IdentifierSyntax;
    }
    if (atRoot && name.front() == '#')
    {
        return NameRefusal::HashAtRoot;
    }
    if (atRoot && (name == "_" || name == "*" || name == "$"))
    {
        return NameRefusal::WildcardAtRoot;
    }
    return std::nullopt;
}

std::string_view reasonOf(NameRefusal refusal)
{
    switch (refusal)
    {
    case NameRefusal::Empty:
        return "le nom est vide";
    case NameRefusal::ContainsDot:
        return "le nom ne peut pas contenir « . »";
    case NameRefusal::ContainsScope:
        return "le nom ne peut pas contenir « :: »";
    case NameRefusal::IdentifierSyntax:
        return "« # » suivi de chiffres désigne un identifiant de flecs, pas un nom";
    case NameRefusal::HashAtRoot:
        return "à la racine, un nom ne peut pas commencer par « # »";
    case NameRefusal::WildcardAtRoot:
        return "« _ », « * » et « $ » seuls sont réservés à la racine";
    }
    return "nom refusé"; // inatteignable : l'énumération est complète
}

core::Result<std::string> uniqueNameAmong(const flecs::world& world, flecs::entity_t parent,
                                          std::string_view wanted, flecs::entity_t self)
{
    if (const auto refusal = refusalOfName(wanted, parent == 0))
    {
        return core::makeError(core::ErrorCode::InvalidData,
                               std::format("nom « {} » refusé : {}", wanted, reasonOf(*refusal)));
    }
    std::string candidate{wanted};
    if (!takenAmongSiblings(world, parent, candidate, self))
    {
        return candidate;
    }
    const std::string_view base = baseOfNumbered(wanted);
    // Un nom déjà numéroté reprend après son numéro (« Lampe 10 » pris donne « Lampe 11 », pas
    // « Lampe 2 » qui ferait remonter la liste) ; un nom nu commence à 2.
    unsigned long long first = 2;
    if (base.size() != wanted.size())
    {
        const std::string_view tail = wanted.substr(base.size() + 1);
        unsigned long long taken = 0;
        if (std::from_chars(tail.data(), tail.data() + tail.size(), taken).ec == std::errc{} &&
            taken < 1'000'000'000ULL)
        {
            first = taken + 1;
        }
    }
    for (unsigned long long number = first;; ++number)
    {
        candidate = std::format("{} {}", base, number);
        if (!takenAmongSiblings(world, parent, candidate, self))
        {
            return candidate;
        }
    }
}

} // namespace levain::scene
