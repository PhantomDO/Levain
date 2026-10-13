#pragma once

// Les polices de l'interface (M7.7, ADR-0036 décision 15) : Inter, avec Noto Sans fusionnée
// derrière pour un glyphe qu'Inter n'a pas, puis Material Symbols pour les icônes ; la console
// garde ProggyForever (à chasse fixe), Inter fusionnée derrière. Les fichiers ne sont pas
// versionnés : `tools/fetch-assets.sh` les met dans `assets-cache/Fonts/`.

#include <filesystem>
#include <span>

#include <imgui.h>

#include "levain/core/error.hpp"

namespace levain::ui
{

/// Les noms des fichiers, relatifs à une racine d'assets : ceux de `tools/assets.lock`.
inline constexpr const char* UiFontFile = "Fonts/Inter-Regular.ttf";
inline constexpr const char* FallbackFontFile = "Fonts/NotoSans-Regular.ttf";
inline constexpr const char* IconFontFile =
    "Fonts/MaterialSymbolsOutlined[FILL,GRAD,opsz,wght].ttf";

struct FontFiles
{
    std::filesystem::path ui;
    std::filesystem::path fallback;
    std::filesystem::path icons;
};

/// Cherche chaque police dans les racines d'assets, dans l'ordre. **Une police absente est une
/// erreur** qui nomme le fichier, les racines cherchées et ce qu'il faut lancer (règle n°7) :
/// l'éditeur ne retombe jamais sans bruit sur la police d'ImGui, dont les glyphes manquants se
/// dessineraient « ? ».
[[nodiscard]] core::Result<FontFiles> findFontFiles(std::span<const std::filesystem::path> roots);

/// Les deux polices à poser : `ui` est la police par défaut, `console` celle de la console. Dans
/// `ui`, les sources sont dans cet ordre : Inter (0), Noto Sans (1), Material Symbols (2) ; chacune
/// ne répond qu'à ce que les précédentes n'ont pas, et Material Symbols qu'à la zone d'usage privé.
struct UiFonts
{
    ImFont* ui = nullptr;
    ImFont* console = nullptr;
};

/// Ajoute les polices à l'atlas d'ImGui (1.92 : l'atlas est dynamique, les glyphes sont rastérisés
/// à la demande, à la taille et à l'échelle d'écran de `style.FontScaleDpi`). Avant la première
/// image : la première police ajoutée devient celle de l'interface. Une police illisible est une
/// erreur qui la nomme.
[[nodiscard]] core::Result<UiFonts> addFonts(ImGuiIO& io, const FontFiles& files, float sizePixels);

} // namespace levain::ui
