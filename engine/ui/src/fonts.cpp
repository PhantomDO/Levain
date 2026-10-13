#include "levain/ui/fonts.hpp"

#include <format>
#include <span>
#include <string>

namespace levain::ui
{

namespace
{

/// La zone d'usage privé du BMP (U+E000..U+F8FF), où Material Symbols met ses icônes : ni Inter ni
/// Noto Sans n'y répondent, pour qu'aucun des deux ne puisse masquer une icône (la première police
/// qui a un glyphe répond). ImGui copie la liste (`ImFontAtlas::AddFont`, `ImMemdup`).
constexpr ImWchar PrivateUseRange[] = {0xE000, 0xF8FF, 0};

/// À l'inverse, Material Symbols ne répond JAMAIS hors de cette zone : tout ce qui est à gauche et
/// à droite est exclu (U+0001..U+DFFF, U+F900..U+FFFF). Sans cela, elle répondrait à tout code
/// qu'Inter et Noto n'ont pas, et un caractère manquant se dessinerait en ligature d'icône plutôt
/// qu'en « ? ». `GlyphRanges` ne ferait pas l'affaire : avec `RendererHasTextures` (ImGui 1.92) il
/// n'est lu que par le préchargement « legacy », c'est `GlyphExcludeRanges` que le chargeur
/// applique (imgui.h, `ImFontConfig`).
constexpr ImWchar IconExcludeRanges[] = {0x1, 0xDFFF, 0xF900, 0xFFFF, 0};

/// Les icônes sont posées sur la ligne d'Inter par trois réglages, tirés des métriques des deux
/// fichiers (hhea/OS/2, lues le 13/10/2026), pas au jugé. ImGui rastérise une source à
/// `SizePixels / (ascender - descender)` pixels par unité.
///   Inter : 2048 unités/em, ascender 1984, descender -494 (ligne de 2478), hauteur des capitales
///           1490 : centre des capitales à 745 / 2478 = 0,3006 x taille au-dessus de la base.
///   Material Symbols : 960 unités/em, ascender 1056, descender -96 (ligne de 1152) ; le symbole
///           occupe la boîte 0..960 posée sur la base, son centre est à 480 unités : à la taille
///           IconScale x taille, 480 / 1152 x IconScale = 0,4167 x IconScale x taille au-dessus.
/// 1. IconScale : un symbole n'occupe que ~64 % de son em (play_arrow : 8 px à 15 px, mesuré) et
///    paraissait plus petit que les capitales (9 px) ; à 1,2 il fait ~9,6 px, un peu plus haut
///    qu'elles, comme il se doit pour une icône.
/// 2. IconOffsetY : sans décalage le centre du symbole était 0,4167 x 1,2 = 0,50 x taille au-dessus
///    de la base, celui des capitales à 0,30 : 0,20 x taille de trop (4 px à 19 px). ImGui
///    multiplie ce décalage par le rapport taille / SizePixels de la source (imgui.h, GlyphOffset).
/// 3. La chasse minimale vaut la taille : tous les symboles ont la même largeur, les libellés
///    qui les suivent s'alignent d'une ligne à l'autre.
constexpr float IconScale = 1.2f;
constexpr float InterCapCentre = 745.0f / 2478.0f;
constexpr float MaterialCentre = 480.0f / 1152.0f;
constexpr float IconOffsetY = MaterialCentre * IconScale - InterCapCentre;

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

/// Une police lue d'un fichier, fusionnée derrière la précédente si `merge`, sans glyphes dans
/// `exclude` (liste terminée par 0, ou nul). ImGui prend le fichier à son compte
/// (`AddFontFromFileTTF`, la donnée vit autant que l'atlas) et rend nul s'il ne le lit pas :
/// `ImFontFlags_NoLoadError` lui interdit l'assertion, c'est à l'appelant de lire ce retour.
ImFont* addFile(ImGuiIO& io, const std::filesystem::path& path, float size, bool merge,
                const ImWchar* exclude, ImVec2 glyphOffset = {}, float minAdvanceX = 0.0f)
{
    ImFontConfig config;
    config.MergeMode = merge;
    config.GlyphOffset = glyphOffset;
    config.GlyphMinAdvanceX = minAdvanceX;
    config.GlyphExcludeRanges = exclude;
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
    FontFiles files{.ui = locate(roots, UiFontFile),
                    .fallback = locate(roots, FallbackFontFile),
                    .icons = locate(roots, IconFontFile)};
    for (const auto& [path, name] :
         {std::pair{&files.ui, UiFontFile}, std::pair{&files.fallback, FallbackFontFile},
          std::pair{&files.icons, IconFontFile}})
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
    // n'a pas : Noto Sans, puis les icônes. Inter et Noto n'ont aucun glyphe dans la zone des
    // icônes, et les icônes aucun hors d'elle (ordre et sources épinglés par
    // tests/ui_fonts_test.cpp).
    UiFonts fonts;
    fonts.ui = addFile(io, files.ui, sizePixels, false, PrivateUseRange);
    if (fonts.ui == nullptr)
    {
        return std::unexpected(unreadable(UiFontFile));
    }
    if (addFile(io, files.fallback, sizePixels, true, PrivateUseRange) == nullptr)
    {
        return std::unexpected(unreadable(FallbackFontFile));
    }
    // Les icônes : plus grandes que le texte et recentrées sur ses capitales (IconScale).
    if (addFile(io, files.icons, sizePixels * IconScale, true, IconExcludeRanges,
                {0.0f, IconOffsetY * sizePixels}, sizePixels) == nullptr)
    {
        return std::unexpected(unreadable(IconFontFile));
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
    if (addFile(io, files.ui, sizePixels, true, PrivateUseRange) == nullptr)
    {
        return std::unexpected(unreadable(UiFontFile));
    }
    return fonts;
}

} // namespace levain::ui
