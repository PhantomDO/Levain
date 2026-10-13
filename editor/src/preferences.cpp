#include "levain/editor/preferences.hpp"

namespace levain::editor
{

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
