#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

#include <doctest/doctest.h>

#include "levain/core/environment.hpp"
#include "levain/platform/paths.hpp"

namespace fs = std::filesystem;

namespace
{

/// Le dossier d'utilisateur où `prefPath` doit tomber, selon la plateforme : `%APPDATA%` sous
/// Windows (le profil itinérant, ADR-0036 décision 10), `XDG_DATA_HOME` ou `~/.local/share` sous
/// Linux.
fs::path userDataRoot()
{
#ifdef _WIN32
    // `environmentVariable` plutôt que `getenv` : la CRT de Microsoft le déclare « unsafe »
    // (erreur sous /WX).
    const std::string appData = levain::core::environmentVariable("APPDATA").value_or("");
    REQUIRE_MESSAGE(!appData.empty(), "%APPDATA% manque : le portable est-il un Windows ?");
    return fs::path{
        std::u8string(reinterpret_cast<const char8_t*>(appData.data()), appData.size())};
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); // NOLINT(concurrency-mt-unsafe)
        xdg != nullptr && *xdg != '\0')
    {
        return fs::path{xdg};
    }
    const char* home = std::getenv("HOME"); // NOLINT(concurrency-mt-unsafe)
    REQUIRE_MESSAGE(home != nullptr, "$HOME manque");
    return fs::path{home} / ".local" / "share";
#endif
}

bool isUnder(const fs::path& path, const fs::path& root)
{
    const fs::path relative =
        fs::weakly_canonical(path).lexically_relative(fs::weakly_canonical(root));
    return !relative.empty() && *relative.begin() != "..";
}

} // namespace

TEST_CASE("prefPath : le dossier de l'utilisateur (Windows : sous %APPDATA%), créé, au nom de "
          "l'application")
{
    // Une organisation d'essai, pour ne pas toucher au dossier de l'éditeur : SDL crée ce qu'on lui
    // demande. Le dossier n'est retiré que s'il n'existait pas avant le test.
    const fs::path expected = userDataRoot() / "LevainTest" / "PrefPath";
    const bool existed = fs::exists(expected.parent_path());

    // Retiré à la sortie du cas, **même si un CHECK échoue ou qu'une exception passe** : le dossier
    // n'est effacé que s'il n'existait pas avant le test.
    const struct Cleanup
    {
        fs::path folder;
        bool existed;

        ~Cleanup()
        {
            if (!existed)
            {
                std::error_code ignored;
                fs::remove_all(folder, ignored);
            }
        }
    } cleanup{.folder = expected.parent_path(), .existed = existed};

    const auto path = levain::platform::prefPath("LevainTest", "PrefPath");

    REQUIRE(path.has_value());
    MESSAGE("prefPath résolu : ", path->generic_string());
    CHECK(fs::is_directory(*path));
    CHECK(isUnder(*path, userDataRoot()));
    CHECK(path->parent_path().filename() == "PrefPath"); // SDL termine par un séparateur
    CHECK(fs::equivalent(*path, expected));
}
