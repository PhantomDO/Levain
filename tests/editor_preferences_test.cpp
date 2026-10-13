// Les préférences de l'éditeur (ADR-0036, décision 10, morceau 15) : le texte versionné qu'on écrit
// puis relit, chaque refus du lecteur (règle n°7 : à la première faute, le fichier entier), le
// fichier sur le disque, et la règle d'une exécution scriptée, qui n'a pas de préférences.
// Le glue d'ImGui est dans `tests/editor_preferences_gpu.cpp`.

#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include <doctest/doctest.h>

#include "levain/assets/asset_id.hpp"
#include "levain/editor/preferences.hpp"
#include "levain/editor/shortcuts.hpp"

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
    preferences.windows.console = false;
    preferences.windows.passes = false;
    preferences.layout = "[Window][Inspecteur###Inspecteur]\nPos=10,20\nSize=300,400\n\n"
                         "[Docking][Data]\nDockSpace ID=0x1 Pos=0,0 Size=1280,720\n";
    return preferences;
}

/// Un fichier valide, auquel les cas ajoutent ou changent une ligne.
std::string valid(std::string_view editorLines = "vitesse_camera = 8\n",
                  std::string_view windowLines = "console = false\n",
                  std::string_view shortcutLines = "")
{
    return std::string{"levain-editor 1\n[editeur]\n"} + std::string{editorLines} + "[fenetres]\n" +
           std::string{windowLines} + "[raccourcis]\n" + std::string{shortcutLines} +
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

/// Les fichiers voisins (« .tmp ») qui traînent dans ce dossier : un enregistrement n'en laisse
/// aucun, réussi ou non. Compter plutôt que chercher un nom : le nom change à chaque écriture.
std::size_t temporariesIn(const fs::path& directory)
{
    std::size_t count = 0;
    for (const auto& entry : fs::directory_iterator{directory})
    {
        count += entry.path().extension() == ".tmp" ? 1U : 0U;
    }
    return count;
}

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

TEST_CASE("préférences : les défauts n'écrivent aucune action, la section des raccourcis est là")
{
    const std::string text = formatPreferences(Preferences{});

    CHECK(text.starts_with("levain-editor 1\n"));
    CHECK(text.find("[raccourcis]\n[disposition]\n") != std::string::npos);
    const auto read = parsePreferences(text);
    REQUIRE(read.has_value());
    CHECK(read->shortcuts.empty());
    CHECK(read->layout.empty());
}

TEST_CASE("préférences : un raccourci écrit par le morceau 16 se garde, deux accords au plus")
{
    Preferences preferences;
    preferences.shortcuts = {
        {.action = "Annuler", .chords = {tokenOf(letterChord('Z', {.ctrl = true})), "pos:26"}},
        {.action = "Supprimer", .chords = {}}};

    const auto read = parsePreferences(formatPreferences(preferences));

    REQUIRE(read.has_value());
    CHECK(read->shortcuts == preferences.shortcuts);
    refuses(
        valid("vitesse_camera = 8\n", "console = false\n", "Annuler = Ctrl+Z, Ctrl+Y, Ctrl+A\n"),
        "deux accords au plus");
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
        refuses(valid("vitesse_camera = 8\n", "console = 1\n"), "true ou false");
        refuses(valid("vitesse_camera = 8\n", "sphere = true\n"), "fenêtre inconnue");
        refuses(valid("vitesse_camera = 8\n", "", "Annuler = Ctrl+W+Z\n"), "pas un nom d'accord");
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

TEST_CASE("préférences : sur le disque, écrites puis relues, sans fichier temporaire oublié")
{
    const TempDirectory directory{"disk"};
    const fs::path file = directory.path / "sous-dossier" / PreferencesFileName;

    REQUIRE(savePreferences(file, changed()).has_value());
    const auto read = loadPreferences(file);

    REQUIRE(presentIn(read) != nullptr);
    CHECK(*presentIn(read) == changed());
    CHECK(temporariesIn(file.parent_path()) == 0);
}

TEST_CASE("préférences : un fichier présent est remplacé par le suivant")
{
    const TempDirectory directory{"replace"};
    const fs::path file = directory.path / PreferencesFileName;
    Preferences first;
    first.flySpeed = 3.0f;
    Preferences second;
    second.flySpeed = 12.0f;
    REQUIRE(savePreferences(file, first).has_value());

    const auto saved = savePreferences(file, second);

    REQUIRE(saved.has_value());
    const auto read = loadPreferences(file);
    REQUIRE(presentIn(read) != nullptr);
    CHECK(*presentIn(read) == second);
    CHECK(temporariesIn(directory.path) == 0);
}

TEST_CASE("préférences : deux écrivains, deux fichiers voisins")
{
    const fs::path file = fs::path{"dossier"} / PreferencesFileName;

    CHECK(temporaryFileFor(file, "a") != temporaryFileFor(file, "b"));
    CHECK(temporaryFileFor(file, "a") == temporaryFileFor(file, "a"));
    CHECK(temporaryFileFor(file, "a").parent_path() == file.parent_path());
    CHECK(temporaryFileFor(file, "a").extension() == ".tmp");
}

TEST_CASE("préférences : deux éditeurs qui écrivent ensemble ne posent jamais un texte hybride")
{
    // Deux écrivains sur le même fichier, l'un long, l'autre court : avec un fichier voisin
    // partagé, le renommage posait le début de l'un et la queue de l'autre (610 fichiers sur 2000
    // tours, mesuré). Chaque lecture, pendant l'autre écriture, doit rendre l'un ou l'autre.
    const TempDirectory directory{"concurrent"};
    const fs::path file = directory.path / PreferencesFileName;
    Preferences longOne;
    longOne.flySpeed = 3.0f;
    for (int i = 0; i < 4000; ++i)
    {
        longOne.layout += "[Window][Longue" + std::to_string(i) + "]\nPos=1,1\n";
    }
    Preferences shortOne;
    shortOne.flySpeed = 12.0f;
    shortOne.layout = "[Window][Courte]\n";
    REQUIRE(savePreferences(file, longOne).has_value());

    std::atomic<int> hybrids{0};
    std::atomic<int> failedSaves{0};
    const auto writer = [&](const Preferences& mine)
    {
        for (int round = 0; round < 150; ++round)
        {
            failedSaves += savePreferences(file, mine).has_value() ? 0 : 1;
            // Lu d'un seul tenant par un fichier ouvert (et non `loadPreferences`, qui mesure le
            // fichier par son chemin avant de l'ouvrir : un renommage entre les deux la ferait
            // échouer, une course de la lecture et non de l'écriture que ce cas éprouve).
            std::ifstream in{file, std::ios::binary};
            const std::string text{std::istreambuf_iterator<char>{in},
                                   std::istreambuf_iterator<char>{}};
            const auto read = parsePreferences(text);
            const Preferences* got = read ? &*read : nullptr;
            hybrids += (got != nullptr && (*got == longOne || *got == shortOne)) ? 0 : 1;
        }
    };
    std::thread other{writer, std::cref(shortOne)};
    writer(longOne);
    other.join();

    CHECK(hybrids == 0);
    CHECK(failedSaves == 0);
    CHECK(temporariesIn(directory.path) == 0);
}

TEST_CASE("préférences : un fichier d'une version plus récente se reconnaît, pas un fichier abîmé")
{
    const TempDirectory directory{"newer"};
    const fs::path file = directory.path / PreferencesFileName;

    std::ofstream{file} << "levain-editor 2\n";
    const auto newer = loadPreferences(file);
    REQUIRE_FALSE(newer.has_value());
    CHECK(isFromNewerVersion(newer.error()));

    std::ofstream{file} << "levain-editor 0\n";
    const auto abandoned = loadPreferences(file);
    REQUIRE_FALSE(abandoned.has_value());
    CHECK_FALSE(isFromNewerVersion(abandoned.error()));

    std::ofstream{file} << "n'importe quoi\n";
    const auto garbage = loadPreferences(file);
    REQUIRE_FALSE(garbage.has_value());
    CHECK_FALSE(isFromNewerVersion(garbage.error()));
}

TEST_CASE("préférences : pas de fichier, pas d'erreur, un fichier refusé nomme le fichier")
{
    const TempDirectory directory{"load"};
    const fs::path file = directory.path / PreferencesFileName;

    const auto absent = loadPreferences(file);
    REQUIRE(absent.has_value());
    CHECK_FALSE(absent->has_value());

    std::ofstream{file} << "levain-editor 9\n";
    const auto broken = loadPreferences(file);
    REQUIRE_FALSE(broken.has_value());
    CHECK(broken.error().message.find(PreferencesFileName) != std::string::npos);
    CHECK(broken.error().message.find("version plus récente") != std::string::npos);
}

TEST_CASE("préférences : un dossier impossible à écrire est une erreur qui le dit")
{
    const TempDirectory directory{"unwritable"};
    // Un fichier à la place du dossier : create_directories échoue partout, même en root.
    std::ofstream{directory.path / "occupe"} << "x";

    const auto saved = savePreferences(directory.path / "occupe" / PreferencesFileName, {});

    REQUIRE_FALSE(saved.has_value());
    CHECK(saved.error().message.find("dossier non créé") != std::string::npos);
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

TEST_CASE("préférences : une virgule de trop dans un raccourci est refusée")
{
    refuses(valid("vitesse_camera = 8\n", "", "Annuler = Ctrl+Z,\n"), "après la virgule");
    refuses(valid("vitesse_camera = 8\n", "", "Annuler = Ctrl+Z, Ctrl+Y,  \n"), "après la virgule");
    refuses(valid("vitesse_camera = 8\n", "", "Annuler = ,Ctrl+Z\n"), "pas un nom d'accord");
    CHECK(parsePreferences(valid("vitesse_camera = 8\n", "", "Annuler = Ctrl+Z, Ctrl+Y\n"))
              .has_value());
}

TEST_CASE("préférences : un fichier écrit avec des fins de ligne Windows est relu")
{
    // Un éditeur de texte sous Windows réécrit les « \n » en « \r\n » : les lignes de l'en-tête et
    // des sections s'en accommodent, la disposition (après sa ligne) est gardée telle quelle.
    const auto read = parsePreferences("levain-editor 1\r\n[editeur]\r\nvitesse_camera = 3\r\n"
                                       "temps_reel = false\r\n[fenetres]\r\nconsole = false\r\n"
                                       "[raccourcis]\r\nAnnuler = Ctrl+Z\r\n[disposition]\r\n"
                                       "[Window][X]\r\n");

    REQUIRE(read.has_value());
    CHECK(read->flySpeed == 3.0f);
    CHECK_FALSE(read->realTime);
    CHECK_FALSE(read->windows.console);
    REQUIRE(read->shortcuts.size() == 1);
    CHECK(read->shortcuts.front().chords == std::vector<std::string>{"Ctrl+Z"});
    CHECK(read->layout == "[Window][X]\r\n");
}

#if !defined(_WIN32)
TEST_CASE("préférences : un fichier dont l'état est illisible est une erreur, pas une première "
          "session")
{
    const TempDirectory directory{"unreadable-state"};
    const fs::path file = directory.path / PreferencesFileName;
    // Un lien qui boucle sur lui-même : `stat` répond ELOOP, ni « absent » ni « présent ». (Un
    // dossier sans droits ne ferait rien en root, que la CI peut être.)
    std::error_code linked;
    fs::create_symlink(file.filename(), file, linked);
    REQUIRE_FALSE(linked);

    const auto loaded = loadPreferences(file);

    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().message.find("état du fichier illisible") != std::string::npos);
    CHECK(loaded.error().message.find(PreferencesFileName) != std::string::npos);
}
#endif

TEST_CASE("préférences : un enregistrement qui échoue ne laisse pas son fichier voisin")
{
    const TempDirectory directory{"failed-save"};
    // Un dossier non vide à la place du fichier : le renommage échoue partout, même en root.
    const fs::path file = directory.path / PreferencesFileName;
    fs::create_directories(file);
    std::ofstream{file / "occupe"} << "x";

    const auto saved = savePreferences(file, {});

    REQUIRE_FALSE(saved.has_value());
    CHECK(saved.error().message.find("renommage échoué") != std::string::npos);
    CHECK(temporariesIn(directory.path) == 0);
}
