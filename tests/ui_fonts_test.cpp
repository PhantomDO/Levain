// Les polices de l'interface (M7.7, morceau 17, ADR-0036 décision 15) : les VRAIES polices, lues
// dans assets-cache/Fonts. Absentes, le test échoue en disant quoi lancer (règle n°7) : il ne se
// contente pas de ne pas s'exécuter.

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

namespace
{

namespace fs = std::filesystem;

/// Les sources de la police de l'interface, dans l'ordre où `addFonts` les fusionne.
constexpr int InterSource = 0;
constexpr int NotoSource = 1;

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

/// Les octets d'Inter, la vraie : une copie que findFontFiles accepte, quoi qu'il vérifie.
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

/// Les deux polices de assets-cache dans un contexte neuf, ou l'échec qui dit quoi lancer.
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
    const std::vector<fs::path> roots{dir.path};
    const auto missing = levain::ui::findFontFiles(roots);
    REQUIRE_FALSE(missing);
    CHECK(missing.error().message.find("NotoSans") != std::string::npos);
    CHECK(missing.error().message.find("fetch-assets.sh") != std::string::npos);
    CHECK_MESSAGE(missing.error().message.find(dir.path.string()) != std::string::npos,
                  "l'erreur liste les racines cherchées");
    writeBytes(dir.path / levain::ui::FallbackFontFile, inter);
    CHECK(levain::ui::findFontFiles(roots));
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

TEST_CASE("ui.fonts : l'ordre et la source de chaque glyphe sont ceux de l'ADR (Inter, Noto)")
{
    const LoadedFonts loaded;
    ImFont* ui = loaded.fonts->ui;
    ImFont* console = loaded.fonts->console;
    REQUIRE(ui->Sources.Size == 2);
    CHECK(std::string{ui->Sources[InterSource]->Name}.starts_with("Inter-Regular.ttf"));
    CHECK(std::string{ui->Sources[NotoSource]->Name}.starts_with("NotoSans-Regular.ttf"));
    // La console : ProggyForever d'abord (chasse fixe), Inter derrière.
    REQUIRE(console->Sources.Size == 2);
    CHECK(std::string{console->Sources[0]->Name}.starts_with("ProggyForever"));
    CHECK(std::string{console->Sources[1]->Name}.starts_with("Inter-Regular.ttf"));
    // Les lettres et la liste viennent d'Inter ; un glyphe d'Inter dans la
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
