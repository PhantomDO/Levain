#include "levain/scene/entity_name.hpp"

#include <algorithm>
#include <cctype>

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

} // namespace levain::scene
