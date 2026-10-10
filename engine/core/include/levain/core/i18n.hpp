#pragma once

// Le catalogue des textes (ADR-0036, décision 14) : le français est la clé, comme le `msgid` de
// gettext ; une traduction absente, vide ou fautive laisse le français, jamais rien. Seul le
// français existe en M7.7 : le mécanisme est réel, la table du processus est vide. `ui::tr`
// (ui/tr.hpp) en est la façade.

#include <format>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace levain::core
{

/// Les traductions d'une langue, par `catalogKey`.
struct Catalog
{
    std::map<std::string, std::string, std::less<>> entries;
};

/// Le français seul, ou, pour un homonyme, contexte, EOT, français (le `msgctxt` de gettext).
[[nodiscard]] std::string catalogKey(std::string_view context, std::string_view french);

/// Les champs d'un format de `std::format`, dans l'ordre (« {} », « {:.3f} »). Vide si
/// `std::vformat` ne le lirait pas : accolade seule, champ imbriqué.
[[nodiscard]] std::optional<std::vector<std::string>> placeholdersOf(std::string_view format);

/// La traduction de ce français dans ce contexte (vide pour aucun), sinon le français. **Jamais un
/// format** : `TextUnformatted`, ou `translateFormat`. Valable jusqu'à la prochaine écriture du
/// catalogue.
[[nodiscard]] const char* translate(const Catalog& catalog, std::string_view context,
                                    const char* french);

/// Comme `translate`, puis les valeurs. Une traduction aux champs différents (ordre compris) de
/// ceux du français mettrait une valeur au mauvais endroit, ou ferait lever `std::vformat` : c'est
/// le français qui s'affiche.
[[nodiscard]] std::string translateFormat(const Catalog& catalog, std::string_view context,
                                          std::string_view french, std::format_args args);

/// Le catalogue du processus, vide au départ. Se remplace entre deux images, sans verrou.
[[nodiscard]] Catalog& activeCatalog();

} // namespace levain::core
