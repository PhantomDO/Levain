#include "levain/ui/fonts.hpp"

#include <format>
#include <span>
#include <string>

namespace levain::ui
{

namespace
{

/// Le chemin tel qu'ImGui l'ouvre : de l'UTF-8 (sous Windows, `path::string()` est la page de
/// code ANSI et un nom accentué serait mal lu).
std::string utf8Of(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

/// Cherche `file` dans les racines, dans l'ordre ; vide s'il n'y est pas.
std::filesystem::path locate(std::span<const std::filesystem::path> roots, const char* file)
{
    for (const std::filesystem::path& root : roots)
    {
        std::error_code error;
        if (std::filesystem::is_regular_file(root / file, error))
        {
            return root / file;
        }
    }
    return {};
}

std::string rootsText(std::span<const std::filesystem::path> roots)
{
    std::string text;
    for (const std::filesystem::path& root : roots)
    {
        text += (text.empty() ? "" : ", ") + utf8Of(root);
    }
    return text.empty() ? "aucune" : text;
}

/// Une police lue d'un fichier, fusionnée derrière la précédente si `merge`. ImGui prend le
/// fichier à son compte (`AddFontFromFileTTF`, la donnée vit autant que l'atlas) et rend nul s'il
/// ne le lit pas : `ImFontFlags_NoLoadError` lui interdit l'assertion, c'est à l'appelant de lire
/// ce retour.
ImFont* addFile(ImGuiIO& io, const std::filesystem::path& path, float size, bool merge)
{
    ImFontConfig config;
    config.MergeMode = merge;
    config.Flags = ImFontFlags_NoLoadError;
    return io.Fonts->AddFontFromFileTTF(utf8Of(path).c_str(), size, &config);
}

core::Error unreadable(const std::string& name)
{
    return {core::ErrorCode::InvalidData,
            std::format("police illisible : {} (relancer tools/fetch-assets.sh)", name)};
}

} // namespace

core::Result<FontFiles> findFontFiles(std::span<const std::filesystem::path> roots)
{
    FontFiles files{.ui = locate(roots, UiFontFile), .fallback = locate(roots, FallbackFontFile)};
    for (const auto& [path, name] :
         {std::pair{&files.ui, UiFontFile}, std::pair{&files.fallback, FallbackFontFile}})
    {
        if (path->empty())
        {
            return core::makeError(core::ErrorCode::FileNotFound,
                                   std::format("police absente : {} (racines d'assets cherchées : "
                                               "{}) ; lancer tools/fetch-assets.sh",
                                               name, rootsText(roots)));
        }
    }
    return files;
}

core::Result<UiFonts> addFonts(ImGuiIO& io, const FontFiles& files, float sizePixels)
{
    // L'interface : Inter d'abord (la première ajoutée est la police par défaut), puis ce qu'elle
    // n'a pas : Noto Sans (ordre et sources épinglés par tests/ui_fonts_test.cpp).
    UiFonts fonts;
    fonts.ui = addFile(io, files.ui, sizePixels, false);
    if (fonts.ui == nullptr)
    {
        return std::unexpected(unreadable(UiFontFile));
    }
    if (addFile(io, files.fallback, sizePixels, true) == nullptr)
    {
        return std::unexpected(unreadable(FallbackFontFile));
    }
    // La console : ProggyForever, à chasse fixe, Latin-1 ; Inter derrière écrit « œ » et « … ».
    ImFontConfig proggy;
    proggy.SizePixels = sizePixels; // une taille explicite : fusionner Inter derrière en refuse une
                                    // implicite (imgui_draw.cpp, AddFont)
    proggy.PixelSnapH = true;       // des colonnes alignées au pixel, comme le modèle de ImGui
    fonts.console = io.Fonts->AddFontDefaultVector(&proggy);
    if (fonts.console == nullptr)
    {
        return std::unexpected(unreadable("ProggyForever (la police intégrée d'ImGui)"));
    }
    if (addFile(io, files.ui, sizePixels, true) == nullptr)
    {
        return std::unexpected(unreadable(UiFontFile));
    }
    return fonts;
}

} // namespace levain::ui
