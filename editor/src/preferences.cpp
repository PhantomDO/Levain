#include "levain/editor/preferences.hpp"

#include <charconv>
#include <cmath>
#include <format>
#include <map>
#include <set>

#include "levain/editor/camera.hpp"

namespace levain::editor
{

namespace
{

constexpr std::string_view HeaderName = "levain-editor";
constexpr std::string_view SectionEditor = "[editeur]";
constexpr std::string_view SectionLayout = "[disposition]";

core::Error refused(std::size_t line, std::string_view why)
{
    return {core::ErrorCode::InvalidData, std::format("ligne {} : {}", line, why)};
}

std::string_view trimmed(std::string_view text)
{
    constexpr std::string_view Blanks = " \t\r";
    const auto first = text.find_first_not_of(Blanks);
    if (first == std::string_view::npos)
    {
        return {};
    }
    return text.substr(first, text.find_last_not_of(Blanks) - first + 1);
}

/// « true » ou « false », rien d'autre : « oui » ou « 1 » seraient deux écritures d'une valeur.
std::optional<bool> boolOf(std::string_view text)
{
    if (text == "true")
    {
        return true;
    }
    if (text == "false")
    {
        return false;
    }
    return std::nullopt;
}

/// Un flottant fini et rien d'autre derrière (`from_chars` s'arrête au premier caractère étranger).
std::optional<float> floatOf(std::string_view text)
{
    float value = 0.0f;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || !std::isfinite(value))
    {
        return std::nullopt;
    }
    return value;
}

core::Result<void> readEditorKey(Preferences& preferences, std::string_view key,
                                 std::string_view value, std::size_t line)
{
    if (key == "vitesse_camera")
    {
        const auto speed = floatOf(value);
        if (!speed || *speed < MinFlySpeed || *speed > MaxFlySpeed)
        {
            return std::unexpected(refused(
                line, std::format("vitesse_camera : un nombre entre {} et {} attendu, « {} » lu",
                                  MinFlySpeed, MaxFlySpeed, value)));
        }
        preferences.flySpeed = *speed;
        return {};
    }
    if (key == "temps_reel")
    {
        const auto flag = boolOf(value);
        if (!flag)
        {
            return std::unexpected(
                refused(line, std::format("temps_reel : true ou false attendu, « {} » lu", value)));
        }
        preferences.realTime = *flag;
        return {};
    }
    return std::unexpected(refused(line, std::format("clé inconnue dans [editeur] : « {} »", key)));
}

} // namespace

std::string formatPreferences(const Preferences& preferences)
{
    std::string text = std::format("{} {}\n", HeaderName, PreferencesVersion);
    text += std::format("{}\nvitesse_camera = {}\ntemps_reel = {}\n", SectionEditor,
                        preferences.flySpeed, preferences.realTime);
    // Dernière : tout ce qui suit la ligne est le texte d'ImGui, tel quel.
    text += std::format("{}\n", SectionLayout);
    text += preferences.layout;
    return text;
}

core::Result<Preferences> parsePreferences(std::string_view text)
{
    if (text.find('\0') != std::string_view::npos)
    {
        return std::unexpected(core::Error{core::ErrorCode::InvalidData,
                                           "un octet nul : ce n'est pas un fichier texte"});
    }
    Preferences preferences;
    std::string_view section; // vide : avant la première section
    std::set<std::string, std::less<>> sectionsSeen;
    std::set<std::string> keysSeen; // de la section en cours
    bool headerSeen = false;
    std::size_t lineNumber = 0;
    std::string_view rest = text;
    while (!rest.empty())
    {
        const auto newline = rest.find('\n');
        const std::string_view rawLine = rest.substr(0, newline);
        rest = newline == std::string_view::npos ? std::string_view{} : rest.substr(newline + 1);
        ++lineNumber;
        const std::string_view line = trimmed(rawLine);
        if (!headerSeen)
        {
            if (line.empty() || line.starts_with('#'))
            {
                continue;
            }
            const std::string expected = std::format("{} ", HeaderName);
            if (!line.starts_with(expected))
            {
                return std::unexpected(
                    refused(lineNumber, std::format("« {} <version> » attendu en tête, « {} » lu",
                                                    HeaderName, line)));
            }
            int version = 0;
            const std::string_view number = trimmed(line.substr(expected.size()));
            const auto [end, error] =
                std::from_chars(number.data(), number.data() + number.size(), version);
            if (error != std::errc{} || end != number.data() + number.size())
            {
                return std::unexpected(
                    refused(lineNumber, std::format("version illisible : « {} »", number)));
            }
            if (version != PreferencesVersion)
            {
                const bool newer = version > PreferencesVersion;
                core::Error error = refused(
                    lineNumber,
                    std::format("version {} (ce code lit la {}) : {}", version, PreferencesVersion,
                                newer ? "écrit par une version plus récente" : "format abandonné"));
                if (newer)
                {
                    error.code = core::ErrorCode::Unsupported; // `isFromNewerVersion`
                }
                return std::unexpected(std::move(error));
            }
            headerSeen = true;
            continue;
        }
        if (line.empty() || line.starts_with('#'))
        {
            continue;
        }
        if (line.starts_with('['))
        {
            if (line != SectionEditor && line != SectionLayout)
            {
                return std::unexpected(
                    refused(lineNumber, std::format("section inconnue : « {} »", line)));
            }
            if (!sectionsSeen.emplace(line).second)
            {
                return std::unexpected(
                    refused(lineNumber, std::format("section en double : « {} »", line)));
            }
            section = line;
            keysSeen.clear();
            if (section == SectionLayout)
            {
                // Le reste du texte, tel quel : le texte d'ImGui n'est pas le nôtre.
                preferences.layout = std::string{rest};
                return preferences;
            }
            continue;
        }
        if (section.empty())
        {
            return std::unexpected(refused(lineNumber, "une ligne avant toute section"));
        }
        const auto equals = line.find('=');
        if (equals == std::string_view::npos)
        {
            return std::unexpected(
                refused(lineNumber, std::format("« clé = valeur » attendu, « {} » lu", line)));
        }
        const std::string_view key = trimmed(line.substr(0, equals));
        const std::string_view value = trimmed(line.substr(equals + 1));
        if (key.empty())
        {
            return std::unexpected(refused(lineNumber, "une valeur sans clé"));
        }
        if (!keysSeen.emplace(key).second)
        {
            return std::unexpected(refused(lineNumber, std::format("clé en double : « {} »", key)));
        }
        const auto read = readEditorKey(preferences, key, value, lineNumber);
        if (!read)
        {
            return std::unexpected(read.error());
        }
    }
    if (!headerSeen)
    {
        return std::unexpected(
            core::Error{core::ErrorCode::InvalidData,
                        std::format("fichier vide : « {} <version> » attendu", HeaderName)});
    }
    // La section de disposition rend le fichier avant d'arriver ici : si la boucle est finie, la
    // marque de fin manque (disque plein, copie coupée) et la disposition aurait été perdue.
    return std::unexpected(refused(
        lineNumber, std::format("fichier tronqué : {} attendue en fin de fichier", SectionLayout)));
}

core::Result<std::optional<std::filesystem::path>>
preferencesFileFor(bool scriptedRun, bool testRun,
                   const std::optional<std::filesystem::path>& testDirectory,
                   const std::function<core::Result<std::filesystem::path>()>& userFolder)
{
    if (scriptedRun)
    {
        return std::optional<std::filesystem::path>{};
    }
    if (testDirectory)
    {
        return std::optional<std::filesystem::path>{*testDirectory / PreferencesFileName};
    }
    if (testRun)
    {
        // Un test qui n'a pas injecté de dossier : le dossier de l'utilisateur n'est pas le sien.
        return std::optional<std::filesystem::path>{};
    }
    const auto folder = userFolder();
    if (!folder)
    {
        return std::unexpected(folder.error());
    }
    return std::optional<std::filesystem::path>{*folder / PreferencesFileName};
}

} // namespace levain::editor
