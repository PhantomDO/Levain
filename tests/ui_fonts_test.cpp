// Les polices de l'interface (M7.7, morceau 17, ADR-0036 décision 15) : les VRAIES polices, lues
// dans assets-cache/Fonts. Absentes, le test échoue en disant quoi lancer (règle n°7) : il ne se
// contente pas de ne pas s'exécuter.

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <doctest/doctest.h>
#include <imgui.h>
#include <imgui_internal.h>

#include "levain/assets/asset_id.hpp"
#include "levain/ui/context.hpp"
#include "levain/ui/fonts.hpp"
#include "levain/ui/icons.hpp"

namespace
{

namespace fs = std::filesystem;

/// Les sources de la police de l'interface, dans l'ordre où `addFonts` les fusionne.
constexpr int InterSource = 0;
constexpr int NotoSource = 1;
constexpr int IconSource = 2;

fs::path assetsDir()
{
    return LEVAIN_TEST_ASSETS_DIR;
}

/// Un dossier à soi, effacé à la fin : un nom fixe se heurterait à un autre lancement (ctest -j).
struct TempDir
{
    fs::path path;

    TempDir()
        : path{fs::temp_directory_path() /
               ("levain-fonts-" + levain::assets::toString(levain::assets::generateAssetId()))}
    {
        fs::create_directories(path / "Fonts");
    }

    ~TempDir()
    {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

void writeBytes(const fs::path& path, const std::string& bytes)
{
    std::ofstream{path, std::ios::binary} << bytes;
}

void putBigEndian(std::string& bytes, unsigned value, unsigned count)
{
    for (unsigned i = count; i > 0; --i)
    {
        bytes.push_back(static_cast<char>((value >> (8U * (i - 1U))) & 0xFFU));
    }
}

/// Un fichier dont l'en-tête OpenType (CFF) et les cinq tables que stb_truetype exige sont là mais
/// vides : il passe le premier contrôle de findFontFiles (fontProblem), pas le second (stb_truetype
/// l'ouvre). Il sert aux cas corrompus, jamais à un fichier qu'on veut accepté (interBytes).
std::string fakeFontBytes(const std::vector<std::string>& tags = {"cmap", "head", "hhea", "hmtx",
                                                                  "CFF "},
                          std::size_t padding = 200, const char* signature = "OTTO")
{
    std::string bytes = signature;
    putBigEndian(bytes, static_cast<unsigned>(tags.size()), 2);
    bytes.append(6, '\0'); // searchRange, entrySelector, rangeShift : six octets nuls
    for (const std::string& tag : tags)
    {
        bytes += tag;
        putBigEndian(bytes, 0, 4);  // somme de contrôle
        putBigEndian(bytes, 12, 4); // décalage
        putBigEndian(bytes, 0, 4);  // longueur : une table vide tient dans tout fichier
    }
    bytes.append(padding, '\0');
    return bytes;
}

/// Les octets d'Inter, la vraie : stb_truetype la lit jusqu'au bout (findFontFiles aussi), un faux
/// fichier minimal ne passerait plus.
std::string interBytes()
{
    std::ifstream real{assetsDir() / levain::ui::UiFontFile, std::ios::binary};
    REQUIRE_MESSAGE(real.is_open(), "Inter absente de assets-cache : lancer tools/fetch-assets.sh");
    return {std::istreambuf_iterator<char>{real}, std::istreambuf_iterator<char>{}};
}

/// Les caractères de tests/data/ui-glyphs.txt, en points de code. La règle de lecture : une ligne
/// qui commence par « # » est un commentaire, et
/// l'espace, la tabulation et le retour chariot séparent ; tout autre caractère est un glyphe (une
/// espace insécable aussi : elle doit se dessiner). Un BOM est refusé : il deviendrait un glyphe.
std::vector<unsigned> listedGlyphs()
{
    std::ifstream file{fs::path{LEVAIN_TEST_DATA_DIR} / "ui-glyphs.txt", std::ios::binary};
    REQUIRE_MESSAGE(file.is_open(), "tests/data/ui-glyphs.txt introuvable");
    std::vector<unsigned> glyphs;
    std::string line;
    bool first = true;
    while (std::getline(file, line))
    {
        REQUIRE_MESSAGE(!(first && line.starts_with("\xef\xbb\xbf")),
                        "ui-glyphs.txt commence par un BOM");
        first = false;
        if (line.starts_with('#'))
        {
            continue;
        }
        for (const char* at = line.c_str(); *at != '\0';)
        {
            unsigned code = 0;
            at += ImTextCharFromUtf8(&code, at, nullptr);
            if (code != ' ' && code != '\t' && code != '\r' && code != 0)
            {
                glyphs.push_back(code);
            }
        }
    }
    return glyphs;
}

unsigned codeOf(const char* utf8)
{
    unsigned code = 0;
    ImTextCharFromUtf8(&code, utf8, nullptr);
    return code;
}

/// Le glyphe que la police dessinerait pour ce code, sans le repli sur « ? » : nul s'il n'y en a
/// pas.
ImFontGlyph* glyphOf(ImFont* font, unsigned code)
{
    return font->GetFontBaked(15.0f)->FindGlyphNoFallback(static_cast<ImWchar>(code));
}

/// Les trois polices de assets-cache dans un contexte neuf, ou l'échec qui dit quoi lancer.
struct LoadedFonts
{
    levain::ui::UiContext context = levain::ui::createUiContext(1.0f);
    levain::core::Result<levain::ui::UiFonts> fonts =
        std::unexpected(levain::core::Error{levain::core::ErrorCode::InvalidData, "pas chargé"});

    LoadedFonts()
    {
        const std::vector<fs::path> roots{assetsDir()};
        const auto files = levain::ui::findFontFiles(roots);
        REQUIRE_MESSAGE(files, "polices absentes de assets-cache : lancer tools/fetch-assets.sh");
        fonts = levain::ui::addFonts(ImGui::GetIO(), *files, 15.0f);
        REQUIRE(fonts);
    }
};

} // namespace

TEST_CASE("ui.fonts : une police absente des racines est une erreur qui la nomme")
{
    const TempDir dir;
    const std::string inter = interBytes();
    writeBytes(dir.path / levain::ui::UiFontFile, inter);
    writeBytes(dir.path / levain::ui::FallbackFontFile, inter);
    const std::vector<fs::path> roots{dir.path};
    const auto missing = levain::ui::findFontFiles(roots);
    REQUIRE_FALSE(missing);
    CHECK(missing.error().message.find("MaterialSymbols") != std::string::npos);
    CHECK(missing.error().message.find("fetch-assets.sh") != std::string::npos);
    CHECK_MESSAGE(missing.error().message.find(dir.path.string()) != std::string::npos,
                  "l'erreur liste les racines cherchées");
    writeBytes(dir.path / levain::ui::IconFontFile, inter);
    CHECK(levain::ui::findFontFiles(roots));
}

TEST_CASE("ui.fonts : un fichier qui n'est pas une police est une erreur nommée, sans assertion")
{
    const TempDir dir;
    const std::string inter = interBytes();
    for (const char* name :
         {levain::ui::UiFontFile, levain::ui::FallbackFontFile, levain::ui::IconFontFile})
    {
        writeBytes(dir.path / name, inter);
    }
    const std::vector<fs::path> roots{dir.path};
    REQUIRE(levain::ui::findFontFiles(roots));
    // Un vrai fichier coupé net : la table des tables est intacte, les tables dépassent.
    const std::string truncated = inter.substr(0, 3000);
    // Un vrai fichier de la bonne taille dont le milieu est à zéro : la table des tables et les
    // tables sont en place, seul stb_truetype voit que l'intérieur est vide (sans ce contrôle,
    // ImGui s'arrêterait sur une assertion en Debug).
    std::string zeroed = inter;
    std::fill(zeroed.begin() + 100000, zeroed.begin() + 300000, '\0');

    struct Case
    {
        const char* what;
        std::string bytes;
    };

    const Case corrupt[] = {
        {"vide", ""},
        {"tronqué après la signature", "OTTOxx"},
        {"page d'erreur", "<html>404 Not Found</html>" + std::string(200, ' ')},
        {"signature fausse", "wOFF" + std::string(200, 'x')},
        {"signature juste, rien derrière",
         std::string("\x00\x01\x00\x00", 4) + std::string(400, '\0')},
        {"un vrai fichier tronqué", truncated},
        {"un vrai fichier dont le milieu est à zéro (octets 100000 à 300000)", zeroed},
        {"signature fausse, tables justes",
         fakeFontBytes({"cmap", "head", "hhea", "hmtx", "CFF "}, 200, "wOFF")},
        {"tables justes mais 92 octets seulement",
         fakeFontBytes({"cmap", "head", "hhea", "hmtx", "CFF "}, 0)},
        {"une table manque (hmtx)", fakeFontBytes({"cmap", "head", "hhea", "CFF "})},
        {"ni glyf ni CFF", fakeFontBytes({"cmap", "head", "hhea", "hmtx"})}};
    for (const Case& broken : corrupt)
    {
        CAPTURE(broken.what);
        writeBytes(dir.path / levain::ui::FallbackFontFile, broken.bytes);
        const auto files = levain::ui::findFontFiles(roots);
        REQUIRE_FALSE(files);
        CHECK(files.error().message.find("NotoSans-Regular.ttf") != std::string::npos);
        CHECK(files.error().message.find("corrompue") != std::string::npos);
    }
}

TEST_CASE("ui.fonts : les vraies polices ont chaque glyphe de la liste, dans l'interface et dans "
          "la console")
{
    const LoadedFonts loaded;
    std::vector<unsigned> wanted = listedGlyphs();
    // Les trois du cahier des charges, même si la liste changeait.
    for (const char* character : {"\xc5\x93", "\xe2\x80\x94", "\xc3\x89"})
    {
        wanted.push_back(codeOf(character));
    }
    CHECK(wanted.size() > 20);
    for (const unsigned code : wanted)
    {
        CAPTURE(code);
        CHECK(glyphOf(loaded.fonts->ui, code) != nullptr);
        CHECK(glyphOf(loaded.fonts->console, code) != nullptr);
    }
}

TEST_CASE("ui.fonts : l'ordre et la source de chaque glyphe sont ceux de l'ADR (Inter, Noto, "
          "icônes)")
{
    const LoadedFonts loaded;
    ImFont* ui = loaded.fonts->ui;
    ImFont* console = loaded.fonts->console;
    REQUIRE(ui->Sources.Size == 3);
    CHECK(std::string{ui->Sources[InterSource]->Name}.starts_with("Inter-Regular.ttf"));
    CHECK(std::string{ui->Sources[NotoSource]->Name}.starts_with("NotoSans-Regular.ttf"));
    CHECK(std::string{ui->Sources[IconSource]->Name}.starts_with("MaterialSymbols"));
    // La console : ProggyForever d'abord (chasse fixe), Inter derrière.
    REQUIRE(console->Sources.Size == 2);
    CHECK(std::string{console->Sources[0]->Name}.starts_with("ProggyForever"));
    CHECK(std::string{console->Sources[1]->Name}.starts_with("Inter-Regular.ttf"));
    // Les lettres et la liste viennent d'Inter, jamais des icônes ; un glyphe d'Inter dans la
    // console vient de Proggy (Latin-1) ou d'Inter derrière.
    for (const unsigned code : listedGlyphs())
    {
        CAPTURE(code);
        const ImFontGlyph* glyph = glyphOf(ui, code);
        REQUIRE(glyph != nullptr);
        CHECK(glyph->SourceIdx == InterSource);
    }
    for (unsigned code = 'A'; code <= 'z'; ++code)
    {
        CAPTURE(code);
        REQUIRE(glyphOf(ui, code) != nullptr);
        CHECK(glyphOf(ui, code)->SourceIdx == InterSource);
        REQUIRE(glyphOf(console, code) != nullptr);
        CHECK(glyphOf(console, code)->SourceIdx == 0);
    }
    // Les icônes : la source des icônes, et elle seule.
    for (const char* icon : levain::ui::icon::All)
    {
        CAPTURE(codeOf(icon));
        const ImFontGlyph* glyph = glyphOf(ui, codeOf(icon));
        REQUIRE(glyph != nullptr);
        CHECK(glyph->SourceIdx == IconSource);
    }
}

/// Vrai si la source répondrait à ce code : sa liste d'exclusion (terminée par 0, des paires
/// inclusives) ne le couvre pas. C'est la règle d'ImGui (imgui_draw.cpp, la liste est copiée par
/// l'atlas), lue sur la config que l'atlas garde.
bool sourceAccepts(const ImFontConfig& source, unsigned code)
{
    for (const ImWchar* range = source.GlyphExcludeRanges; range != nullptr && range[0] != 0;
         range += 2)
    {
        if (code >= range[0] && code <= range[1])
        {
            return false;
        }
    }
    return true;
}

TEST_CASE("ui.fonts : les icônes ne répondent que dans la zone d'usage privé, et elle est à elles")
{
    const LoadedFonts loaded;
    const ImFont& ui = *loaded.fonts->ui;
    // Material Symbols n'a le droit de répondre à aucun code hors de U+E000..U+F8FF (sinon un
    // caractère qu'Inter et Noto n'ont pas se dessinerait en icône) ; Inter et Noto, à aucun code
    // dedans (Inter y a 745 glyphes, qui masqueraient des icônes). Mesuré le 13/10/2026.
    for (unsigned code = 1; code <= 0xFFFF; ++code)
    {
        const bool privateUse = code >= 0xE000 && code <= 0xF8FF;
        CAPTURE(code);
        CHECK(sourceAccepts(*ui.Sources[IconSource], code) == privateUse);
        CHECK(sourceAccepts(*ui.Sources[InterSource], code) == !privateUse);
        CHECK(sourceAccepts(*ui.Sources[NotoSource], code) == !privateUse);
    }
    // Et ce que l'atlas dessine : dans la zone, rien d'autre qu'une icône ; hors d'elle (de quoi
    // couvrir les écritures que l'éditeur écrit), jamais une icône.
    int answered = 0;
    for (unsigned code = 1; code < 0x3000; ++code)
    {
        const ImFontGlyph* glyph = glyphOf(loaded.fonts->ui, code);
        if (glyph != nullptr)
        {
            ++answered;
            CHECK_MESSAGE(glyph->SourceIdx != IconSource, "U+", std::hex, code);
        }
    }
    CHECK_MESSAGE(answered > 500, "le parcours doit voir les glyphes d'Inter et de Noto");
    for (unsigned code = 0xE000; code <= 0xF8FF; ++code)
    {
        const ImFontGlyph* glyph = glyphOf(loaded.fonts->ui, code);
        if (glyph != nullptr)
        {
            CHECK_MESSAGE(glyph->SourceIdx == IconSource, "U+", std::hex, code);
        }
    }
}

TEST_CASE("ui.fonts : Noto Sans comble un glyphe qu'Inter n'a pas")
{
    const LoadedFonts loaded;
    // U+A640 (Ꙁ, cyrillique ancien) : absent d'Inter, présent dans Noto Sans (mesuré le
    // 13/10/2026). Sans la fusion de Noto, le glyphe serait nul.
    const ImFontGlyph* glyph = glyphOf(loaded.fonts->ui, 0xA640);
    REQUIRE(glyph != nullptr);
    CHECK(glyph->SourceIdx == NotoSource);
}

TEST_CASE("ui.fonts : chaque icône est centrée sur les capitales et pas plus petite qu'elles")
{
    // À 125 %, l'icône Jouer faisait 9 px contre 12 px de capitales et flottait 4 px au-dessus de
    // la ligne de base (revue de la pièce 17a). Les étendues verticales sont celles des glyphes
    // rastérisés, relatives au haut de la ligne, comme celles de « H ». 15 px est la taille de
    // l'éditeur, 19 px celle à 125 %.
    const LoadedFonts loaded;
    for (const float size : {15.0f, 19.0f})
    {
        ImFontBaked* baked = loaded.fonts->ui->GetFontBaked(size);
        // Copiées : rastériser un glyphe de plus déplace le tableau de l'atlas, un pointeur gardé
        // sur « H » pendrait.
        const ImFontGlyph* found = baked->FindGlyphNoFallback('H');
        REQUIRE(found != nullptr);
        const float capitalTop = found->Y0;
        const float capitalBottom = found->Y1;
        const float capitalHeight = capitalBottom - capitalTop;
        const float capitalCentre = (capitalTop + capitalBottom) / 2.0f;
        for (const char* icon : levain::ui::icon::All)
        {
            const ImFontGlyph* glyph =
                baked->FindGlyphNoFallback(static_cast<ImWchar>(codeOf(icon)));
            REQUIRE(glyph != nullptr);
            CAPTURE(size);
            CAPTURE(codeOf(icon));
            CHECK(glyph->Y0 < capitalBottom);
            CHECK(glyph->Y1 > capitalTop);
            // Les bords sont arrondis au pixel : trois quarts de pixel de tolérance sur le centre.
            CHECK(std::abs((glyph->Y0 + glyph->Y1) / 2.0f - capitalCentre) <= 0.75f);
            CHECK(glyph->Y1 - glyph->Y0 >= 0.85f * capitalHeight);
            CHECK(glyph->AdvanceX >= size); // la chasse minimale : les libellés s'alignent
        }
    }
}
