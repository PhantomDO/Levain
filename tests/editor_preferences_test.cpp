// Les préférences de l'éditeur (ADR-0036, décision 10, morceau 15) : le texte versionné qu'on écrit
// puis relit, chaque refus du lecteur (règle n°7 : à la première faute, le fichier entier), le
// fichier sur le disque, et la règle d'une exécution scriptée, qui n'a pas de préférences.

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include <doctest/doctest.h>

#include "levain/assets/asset_id.hpp"
#include "levain/editor/preferences.hpp"

using namespace levain::editor;
namespace fs = std::filesystem;

namespace
{

/// Des préférences qui ne sont pas les défauts, pour qu'un champ oublié se voie.
Preferences changed()
{
    Preferences preferences;
    preferences.flySpeed = 12.5f;
    preferences.realTime = false;
    preferences.layout = "[Window][Inspecteur###Inspecteur]\nPos=10,20\nSize=300,400\n\n"
                         "[Docking][Data]\nDockSpace ID=0x1 Pos=0,0 Size=1280,720\n";
    return preferences;
}

/// Un fichier valide, auquel les cas ajoutent ou changent une ligne.
std::string valid(std::string_view editorLines = "vitesse_camera = 8\n")
{
    return std::string{"levain-editor 1\n[editeur]\n"} + std::string{editorLines} +
           "[disposition]\n";
}

/// Le texte est refusé, et le message contient `needle` (la ligne fautive ou la raison).
void refuses(std::string_view text, std::string_view needle)
{
    const auto parsed = parsePreferences(text);
    REQUIRE_FALSE(parsed.has_value());
    CHECK_MESSAGE(parsed.error().message.find(needle) != std::string::npos, "le message « ",
                  parsed.error().message, " » devrait contenir « ", needle, " »");
}

/// Ce que contient un résultat « peut-être quelque chose » : le pointeur, nul s'il n'y a rien ou si
/// c'est une erreur (la forme que lit clang-tidy sans se plaindre d'un accès non vérifié).
template <typename T> const T* presentIn(const levain::core::Result<std::optional<T>>& result)
{
    if (!result || !*result)
    {
        return nullptr;
    }
    return &**result;
}

/// Un dossier jetable, retiré à la fin du cas.
struct TempDirectory
{
    fs::path path;

    explicit TempDirectory(std::string_view name)
        // Un identifiant au hasard dans le nom : deux ctest en parallèle (ou deux worktrees) ne se
        // partagent pas le dossier de `/tmp`.
        : path(fs::temp_directory_path() /
               ("levain-prefs-" + std::string{name} + "-" +
                levain::assets::toString(levain::assets::generateAssetId())))
    {
        fs::create_directories(path);
    }

    ~TempDirectory()
    {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }

    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;
};

} // namespace

TEST_CASE("préférences : écrire puis relire rend les mêmes préférences, le texte d'ImGui compris")
{
    const Preferences original = changed();

    const auto read = parsePreferences(formatPreferences(original));

    REQUIRE(read.has_value());
    CHECK(*read == original);
    CHECK(read->layout == original.layout); // octet pour octet
}

TEST_CASE("préférences : relu puis réécrit, le texte ne change pas")
{
    const std::string once = formatPreferences(changed());
    const auto read = parsePreferences(once);
    REQUIRE(read.has_value());
    CHECK(formatPreferences(*read) == once);
}

TEST_CASE("préférences : chaque refus nomme sa ligne et sa raison")
{
    SUBCASE("vide, ou sans en-tête")
    {
        refuses("", "fichier vide");
        refuses("# rien\n\n", "fichier vide");
        refuses("[editeur]\n", "levain-editor <version>");
        refuses("autre-chose 1\n", "levain-editor <version>");
    }
    SUBCASE("la version")
    {
        refuses("levain-editor 2\n", "version plus récente");
        refuses("levain-editor 0\n", "format abandonné");
        refuses("levain-editor un\n", "version illisible");
        refuses("levain-editor 1 2\n", "version illisible");
    }
    SUBCASE("la structure")
    {
        refuses("levain-editor 1\nvitesse_camera = 8\n", "avant toute section");
        refuses("levain-editor 1\n[autre]\n", "section inconnue");
        refuses("levain-editor 1\n[editeur]\n[editeur]\n", "section en double");
        refuses("levain-editor 1\n[editeur]\nvitesse_camera\n", "clé = valeur");
        refuses("levain-editor 1\n[editeur]\n = 8\n", "sans clé");
        refuses("levain-editor 1\n[editeur]\nvitesse_camera = 8\nvitesse_camera = 9\n",
                "clé en double");
        std::string withNul = "levain-editor 1\n[editeur]\n";
        withNul += '\0';
        refuses(withNul, "octet nul");
    }
    SUBCASE("les valeurs")
    {
        refuses(valid("vitesse_camera = vite\n"), "vitesse_camera");
        refuses(valid("vitesse_camera = 8 m/s\n"), "vitesse_camera");
        refuses(valid("vitesse_camera = nan\n"), "vitesse_camera");
        refuses(valid("vitesse_camera = 0\n"), "entre");
        refuses(valid("vitesse_camera = 1e9\n"), "entre");
        refuses(valid("temps_reel = oui\n"), "true ou false");
        refuses(valid("vitesse_camera = 8\nvolume = 3\n"), "clé inconnue");
    }
    SUBCASE("la ligne fautive est numérotée")
    {
        refuses("levain-editor 1\n[editeur]\ntemps_reel = peut-être\n", "ligne 3");
    }
}

TEST_CASE(
    "préférences : commentaires et lignes vides avant la disposition, la disposition telle quelle")
{
    const auto read = parsePreferences("\n# écrit à la main\nlevain-editor 1\n\n# la vitesse\n"
                                       "[editeur]\n  vitesse_camera =  3  \n[disposition]\n"
                                       "# pas un commentaire : du texte d'ImGui\n[Window][X]\n");

    REQUIRE(read.has_value());
    CHECK(read->flySpeed == 3.0f);
    CHECK(read->layout == "# pas un commentaire : du texte d'ImGui\n[Window][X]\n");
}

TEST_CASE("préférences : une exécution scriptée n'a pas de fichier, et ne touche pas le dossier de "
          "l'utilisateur")
{
    int asked = 0;
    const auto userFolder = [&asked]() -> levain::core::Result<fs::path>
    {
        ++asked;
        return fs::path{"/utilisateur"};
    };

    SUBCASE("scriptée : rien, et le dossier de l'utilisateur n'est pas même demandé")
    {
        const auto file = preferencesFileFor(true, false, std::nullopt, userFolder);
        REQUIRE(file.has_value());
        CHECK_FALSE(file->has_value());
        // Même avec un dossier de test : le script ne lit ni n'écrit, c'est `testInteractive` qui
        // lève la règle, côté App.
        const auto withTest = preferencesFileFor(true, false, fs::path{"/essai"}, userFolder);
        REQUIRE(withTest.has_value());
        CHECK_FALSE(withTest->has_value());
        CHECK(asked == 0);
    }
    SUBCASE("interactive : le dossier de l'utilisateur, ou celui du test qui le remplace")
    {
        const auto user = preferencesFileFor(false, false, std::nullopt, userFolder);
        REQUIRE(presentIn(user) != nullptr);
        CHECK(*presentIn(user) == fs::path{"/utilisateur"} / PreferencesFileName);
        const auto test = preferencesFileFor(false, false, fs::path{"/essai"}, userFolder);
        REQUIRE(presentIn(test) != nullptr);
        CHECK(*presentIn(test) == fs::path{"/essai"} / PreferencesFileName);
        CHECK(asked == 1); // celui du test ne l'a pas demandé
    }
    SUBCASE("un test sans dossier injecté n'a pas de fichier, et le dossier de l'utilisateur n'est "
            "pas même demandé")
    {
        // Le dossier de l'utilisateur est ici un dossier jetable qu'on regarde : s'il était
        // demandé, il serait créé (`prefPath` crée le sien) et le fichier s'y écrirait.
        const TempDirectory userDirectory{"user"};
        int created = 0;
        const auto counting = [&created, &userDirectory]() -> levain::core::Result<fs::path>
        {
            ++created;
            return userDirectory.path;
        };

        const auto file = preferencesFileFor(false, true, std::nullopt, counting);

        REQUIRE(file.has_value());
        CHECK_FALSE(file->has_value());
        CHECK(created == 0);
        CHECK(fs::is_empty(userDirectory.path));
        // Avec un dossier injecté, le test a bien son fichier, et toujours pas celui de
        // l'utilisateur.
        const auto injected = preferencesFileFor(false, true, fs::path{"/essai"}, counting);
        REQUIRE(presentIn(injected) != nullptr);
        CHECK(*presentIn(injected) == fs::path{"/essai"} / PreferencesFileName);
        CHECK(created == 0);
    }
    SUBCASE("un dossier d'utilisateur introuvable est une erreur, pas un silence")
    {
        const auto missing =
            preferencesFileFor(false, false, std::nullopt,
                               []() -> levain::core::Result<fs::path>
                               {
                                   return levain::core::makeError(
                                       levain::core::ErrorCode::FileNotFound, "pas de profil");
                               });
        REQUIRE_FALSE(missing.has_value());
        CHECK(missing.error().message == "pas de profil");
    }
}

TEST_CASE("préférences : un fichier tronqué est refusé, la section de la disposition marque la fin")
{
    // Ce que laisserait un disque plein ou une copie coupée : tout, sauf la disposition.
    const std::string whole = valid();
    const std::string withoutLayout = whole.substr(0, whole.find("[disposition]"));
    refuses(withoutLayout, "fichier tronqué");
    refuses("levain-editor 1\n", "fichier tronqué");
    refuses("levain-editor 1\n[editeur]\nvitesse_camera = 8\n", "fichier tronqué");
    // La marque seule suffit : une disposition vide est une disposition.
    CHECK(parsePreferences(whole).has_value());
}
