#include "levain/core/i18n.hpp"

namespace levain::core
{

namespace
{

/// La traduction de ce français, ou rien : une entrée vide compte pour absente.
const std::string* translationOf(const Catalog& catalog, std::string_view context,
                                 std::string_view french)
{
    const auto found = context.empty() ? catalog.entries.find(french)
                                       : catalog.entries.find(catalogKey(context, french));
    return found != catalog.entries.end() && !found->second.empty() ? &found->second : nullptr;
}

} // namespace

std::string catalogKey(std::string_view context, std::string_view french)
{
    return context.empty() ? std::string{french} : std::format("{}\x04{}", context, french);
}

std::optional<std::vector<std::string>> placeholdersOf(std::string_view format)
{
    std::vector<std::string> fields;
    for (std::size_t i = 0; i < format.size(); ++i)
    {
        const char brace = format[i];
        if (brace != '{' && brace != '}')
        {
            continue;
        }
        if (i + 1 < format.size() && format[i + 1] == brace) // « {{ » et « }} » : du texte
        {
            ++i;
            continue;
        }
        const std::size_t close = brace == '{' ? format.find('}', i) : format.npos;
        if (close == format.npos)
        {
            return std::nullopt; // une accolade seule
        }
        fields.emplace_back(format.substr(i, close - i + 1));
        i = close;
    }
    return fields;
}

const char* translate(const Catalog& catalog, std::string_view context, const char* french)
{
    const std::string* translated = translationOf(catalog, context, french);
    return translated != nullptr ? translated->c_str() : french;
}

std::string translateFormat(const Catalog& catalog, std::string_view context,
                            std::string_view french, std::format_args args)
{
    const std::string* translated = translationOf(catalog, context, french);
    const auto wanted = placeholdersOf(french);
    const bool usable = translated != nullptr && wanted && wanted == placeholdersOf(*translated);
    return std::vformat(usable ? std::string_view{*translated} : french, args);
}

Catalog& activeCatalog()
{
    static Catalog catalog;
    return catalog;
}

} // namespace levain::core
