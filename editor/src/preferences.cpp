#include "levain/editor/preferences.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <fstream>
#include <ios>
#include <map>
#include <set>
#include <system_error>

#include "levain/assets/asset_id.hpp"
#include "levain/core/file.hpp"
#include "levain/editor/camera.hpp"
#include "levain/editor/shortcuts.hpp"

namespace levain::editor
{

namespace
{

constexpr std::string_view HeaderName = "levain-editor";
constexpr std::string_view SectionEditor = "[editeur]";
constexpr std::string_view SectionWindows = "[fenetres]";
constexpr std::string_view SectionShortcuts = "[raccourcis]";
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

/// Les fenêtres par leur clé de fichier, dans l'ordre du menu. Un seul endroit pour les sept : le
/// format, la lecture et l'écriture en dérivent.
struct WindowKey
{
    std::string_view key;
    bool OpenWindows::* member;
};

constexpr std::array<WindowKey, 7> WindowKeys{{{"hierarchie", &OpenWindows::hierarchy},
                                               {"inspecteur", &OpenWindows::inspector},
                                               {"console", &OpenWindows::console},
                                               {"vue", &OpenWindows::view},
                                               {"image", &OpenWindows::image},
                                               {"passes", &OpenWindows::passes},
                                               {"scene", &OpenWindows::scene}}};

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

core::Result<void> readWindowKey(Preferences& preferences, std::string_view key,
                                 std::string_view value, std::size_t line)
{
    const auto known = std::ranges::find(WindowKeys, key, &WindowKey::key);
    if (known == WindowKeys.end())
    {
        return std::unexpected(
            refused(line, std::format("fenêtre inconnue dans [fenetres] : « {} »", key)));
    }
    const auto flag = boolOf(value);
    if (!flag)
    {
        return std::unexpected(
            refused(line, std::format("{} : true ou false attendu, « {} » lu", key, value)));
    }
    preferences.windows.*(known->member) = *flag;
    return {};
}

/// « Ctrl+Z, Ctrl+Y » : au plus deux accords, chacun un nom stable valide, ou rien (l'action sans
/// accord). La valeur de l'action n'est pas jugée ici, le morceau 16 connaît la table.
core::Result<void> readShortcut(Preferences& preferences, std::string_view key,
                                std::string_view value, std::size_t line)
{
    ShortcutLine shortcut{.action = std::string{key}, .chords = {}};
    std::string_view rest = value;
    while (!trimmed(rest).empty())
    {
        const auto comma = rest.find(',');
        const std::string_view token = trimmed(rest.substr(0, comma));
        if (!chordFromToken(token))
        {
            return std::unexpected(
                refused(line, std::format("{} : « {} » n'est pas un nom d'accord", key, token)));
        }
        shortcut.chords.emplace_back(token);
        rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
        if (comma != std::string_view::npos && trimmed(rest).empty())
        {
            // « Ctrl+Z, » : un accord attendu après la virgule, pas un accord de moins.
            return std::unexpected(
                refused(line, std::format("{} : accord attendu après la virgule", key)));
        }
    }
    if (shortcut.chords.size() > 2)
    {
        return std::unexpected(refused(line, std::format("{} : deux accords au plus", key)));
    }
    preferences.shortcuts.push_back(std::move(shortcut));
    return {};
}

} // namespace

std::string formatPreferences(const Preferences& preferences)
{
    std::string text = std::format("{} {}\n", HeaderName, PreferencesVersion);
    text += std::format("{}\nvitesse_camera = {}\ntemps_reel = {}\n", SectionEditor,
                        preferences.flySpeed, preferences.realTime);
    text += std::format("{}\n", SectionWindows);
    for (const WindowKey& window : WindowKeys)
    {
        text += std::format("{} = {}\n", window.key, preferences.windows.*(window.member));
    }
    // Toujours présente, souvent vide : l'absence d'une action, ce sont les défauts.
    text += std::format("{}\n", SectionShortcuts);
    for (const ShortcutLine& shortcut : preferences.shortcuts)
    {
        text += std::format("{} =", shortcut.action);
        for (std::size_t i = 0; i < shortcut.chords.size(); ++i)
        {
            text += std::format("{} {}", i == 0 ? "" : ",", shortcut.chords.at(i));
        }
        text += '\n';
    }
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
            if (line != SectionEditor && line != SectionWindows && line != SectionShortcuts &&
                line != SectionLayout)
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
        const auto read =
            section == SectionEditor    ? readEditorKey(preferences, key, value, lineNumber)
            : section == SectionWindows ? readWindowKey(preferences, key, value, lineNumber)
                                        : readShortcut(preferences, key, value, lineNumber);
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

core::Result<std::optional<Preferences>> loadPreferences(const std::filesystem::path& file)
{
    // `exists` avec un code d'erreur rend faux pour « introuvable » **et** pour « interdit » : seul
    // le premier est une première session, le second ne se tait pas (règle n°7).
    std::error_code probe;
    const bool present = std::filesystem::exists(file, probe);
    if (probe)
    {
        return std::unexpected(
            core::Error{core::ErrorCode::FileNotFound,
                        std::format("préférences {} : état du fichier illisible ({})",
                                    file.generic_string(), probe.message())});
    }
    if (!present)
    {
        return std::optional<Preferences>{};
    }
    const auto bytes = core::readFile(file);
    if (!bytes)
    {
        return std::unexpected(core::Error{
            bytes.error().code,
            std::format("préférences {} : {}", file.generic_string(), bytes.error().message)});
    }
    const std::string_view text{reinterpret_cast<const char*>(bytes->data()), bytes->size()};
    auto parsed = parsePreferences(text);
    if (!parsed)
    {
        return std::unexpected(core::Error{
            parsed.error().code,
            std::format("préférences {} : {}", file.generic_string(), parsed.error().message)});
    }
    return std::optional<Preferences>{std::move(*parsed)};
}

std::filesystem::path temporaryFileFor(const std::filesystem::path& file, std::string_view writer)
{
    std::filesystem::path temporary = file;
    temporary += std::format(".{}.tmp", writer);
    return temporary;
}

bool isFromNewerVersion(const core::Error& error)
{
    return error.code == core::ErrorCode::Unsupported;
}

core::Result<void> savePreferences(const std::filesystem::path& file,
                                   const Preferences& preferences)
{
    // Le code reste `FileNotFound`, le plus proche des quatre de `ErrorCode` (volontairement
    // courte) : c'est le message qui dit ce qui a échoué, « écriture » ou « renommage ».
    const auto failure = [&file](std::string_view what, const std::string& why)
    {
        return std::unexpected(
            core::Error{core::ErrorCode::FileNotFound,
                        std::format("préférences {} : {} ({})", file.generic_string(), what, why)});
    };
    std::error_code error;
    if (file.has_parent_path())
    {
        std::filesystem::create_directories(file.parent_path(), error);
        if (error)
        {
            return failure("dossier non créé", error.message());
        }
    }
    // Un nom par écriture : voir `temporaryFileFor`.
    const std::filesystem::path temporary =
        temporaryFileFor(file, assets::toString(assets::generateAssetId()));
    // Un échec ne laisse pas son fichier voisin : le prochain enregistrement repart de zéro.
    const auto failureWithoutTemporary = [&](std::string_view what, const std::string& why)
    {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return failure(what, why);
    };
    bool written = false;
    {
        // Binaire : « \n » reste « \n » sous Windows, le texte d'ImGui se relit à l'identique.
        std::ofstream out{temporary, std::ios::binary}; // `trunc` est implicite pour un ofstream
        const std::string text = formatPreferences(preferences);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        written = static_cast<bool>(out);
    } // fermé avant tout retrait : Windows ne retire pas un fichier ouvert
    if (!written)
    {
        return failureWithoutTemporary("écriture échouée", "disque plein ou en lecture seule ?");
    }
    std::filesystem::rename(temporary, file, error);
    if (error)
    {
        return failureWithoutTemporary("renommage échoué", error.message());
    }
    return {};
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
