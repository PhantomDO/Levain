#pragma once

// Le catalogue des textes vu de l'interface (ADR-0036, décision 14) ; la table est dans `core`
// (core/i18n.hpp). La clé est le texte français : sans traduction, c'est lui qu'on voit. Un texte
// traduit n'est JAMAIS un format d'ImGui
// (`ImGui::Text(tr(…))` lirait ses « % ») : `TextUnformatted`, ou `textf` pour y mettre des
// valeurs. Un libellé n'est pas un identifiant : `labelOf` garde le sien après « ### », sans quoi
// traduire un titre ferait perdre sa fenêtre à la disposition.

#include <format>
#include <string>
#include <utility>

#include <imgui.h>

#include "levain/core/i18n.hpp"

namespace levain::ui
{

/// Le texte à afficher pour cette clé française : sa traduction, sinon le français. Un homonyme se
/// distingue par son contexte : `tr("Ouvrir", "verbe")`.
[[nodiscard]] inline const char* tr(const char* french, const char* context = "")
{
    return core::translate(core::activeCatalog(), context, french);
}

/// Un texte à valeurs, de `std::format` : le français est vérifié contre elles à la compilation.
template <class... Args>
[[nodiscard]] std::string trf(std::format_string<Args...> french, Args&&... args)
{
    return core::translateFormat(core::activeCatalog(), {}, french.get(),
                                 std::make_format_args(args...));
}

/// Le texte traduit, puis « ### » et la clé : un identifiant qui ne change pas avec la langue
/// (ImGui ne hache que ce qui suit). Pour un titre de fenêtre ou un widget.
[[nodiscard]] inline std::string labelOf(const char* french, const char* context = "")
{
    return std::string{tr(french, context)} + "###" + core::catalogKey(context, french);
}

/// Un texte à valeurs, affiché tel quel.
template <class... Args> void textf(std::format_string<Args...> french, Args&&... args)
{
    ImGui::TextUnformatted(trf(french, std::forward<Args>(args)...).c_str());
}

} // namespace levain::ui
