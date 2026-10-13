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
