#pragma once

// Les icônes de l'interface : des points de code de Material Symbols (Outlined), dans la zone
// d'usage privé (U+E000..U+F8FF), que `addFonts` fusionne derrière la police de l'interface. Le nom
// d'un symbole et son code :
// https://github.com/google/material-design-icons/blob/master/variablefont/MaterialSymbolsOutlined%5BFILL%2CGRAD%2Copsz%2Cwght%5D.codepoints
// Écrits en échappements : le source reste ASCII, et le contrôle des littéraux
// (tests/check_untranslated.py) n'a pas à connaître la zone d'usage privé.

#include <array>
#include <string>

namespace levain::ui::icon
{

inline constexpr const char* Play = "\uE037";    // play_arrow
inline constexpr const char* Stop = "\uE047";    // stop
inline constexpr const char* Move = "\uE89F";    // open_with
inline constexpr const char* Rotate = "\uE41A";  // rotate_right
inline constexpr const char* Scale = "\uE85B";   // aspect_ratio
inline constexpr const char* Save = "\uE161";    // save
inline constexpr const char* Help = "\uE8FD";    // help
inline constexpr const char* Console = "\uEB8E"; // terminal
inline constexpr const char* Frame = "\uE3C2";   // crop_free

/// Toutes les icônes : le test des polices vérifie que chacune est dans la police des icônes.
inline constexpr auto All =
    std::to_array<const char*>({Play, Stop, Move, Rotate, Scale, Save, Help, Console, Frame});

/// L'icône devant un libellé de bouton ou de menu. Le préfixe ne change pas l'identifiant d'ImGui,
/// que `labelOf` écrit après « ### ».
[[nodiscard]] inline std::string withIcon(const char* symbol, const std::string& label)
{
    return std::string{symbol} + " " + label;
}

} // namespace levain::ui::icon
