#include "shader_reload.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "levain/core/log.hpp"
#include "levain/platform/process.hpp"

namespace
{

using Clock = std::chrono::steady_clock;

double millisecondsSince(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

/// Ce qu'a écrit la première commande en échec, sans les lignes de ninja qui l'encadrent : son
/// « FAILED: », la commande complète qui suit, puis la ligne d'état « [2/2] » de la commande
/// suivante. Les deux points d'entrée d'un fichier échouent sur les mêmes erreurs : une fois
/// suffit. Toute la sortie si elle n'a pas cette forme, plutôt qu'un échec muet.
std::string firstFailure(const std::string& output)
{
    std::string failure;
    std::istringstream lines{output};
    std::string line;
    while (std::getline(lines, line) && !line.starts_with("FAILED:"))
    {
    }
    std::getline(lines, line); // la commande, déjà connue
    while (std::getline(lines, line) && !line.starts_with('[') && !line.starts_with("FAILED:") &&
           !line.starts_with("ninja:"))
    {
        failure += line + '\n';
    }
    return failure.empty() ? output : failure;
}

} // namespace

ShaderReload startShaderReload()
{
    return ShaderReload{.sources = levain::core::watchDirectory(LEVAIN_SHADER_SOURCE_DIR, ".slang"),
                        .nextCheck = Clock::now() + ShaderCheckPeriod};
}

void reloadChangedShaders(ShaderReload& reload, nvrhi::IDevice& device,
                          const nvrhi::FramebufferInfo& target, levain::render::MeshPass& meshPass)
{
    const Clock::time_point now = Clock::now();
    if (now < reload.nextCheck)
    {
        return;
    }
    reload.nextCheck = now + ShaderCheckPeriod;

    const std::vector<std::filesystem::path> changed =
        levain::core::takeChangedFiles(reload.sources);
    if (changed.empty())
    {
        return;
    }
    for (const std::filesystem::path& file : changed)
    {
        // file_clock : la même horloge que les dates de modification, sans conversion.
        std::error_code error;
        const auto age = std::filesystem::file_time_type::clock::now() -
                         std::filesystem::last_write_time(file, error);
        levain::core::log("shaders", levain::core::LogLevel::Info, "{} modifié il y a {:.0f} ms",
                          file.filename().string(),
                          std::chrono::duration<double, std::milli>(age).count());
    }

    // ADR-0014 : les commandes exactes du build, dans le dossier de build qui a produit ce binaire.
    // ponytail: bloquant, la boucle s'arrête le temps du build (~350 ms) ; un thread si ça gêne.
    const Clock::time_point buildStart = Clock::now();
    const std::array<std::string, 5> command{LEVAIN_CMAKE_COMMAND, "--build", LEVAIN_BUILD_DIR,
                                             "--target", "levain_shaders"};
    const auto build = levain::platform::runProcess(command);
    if (!build)
    {
        levain::core::log("shaders", levain::core::LogLevel::Error, "{}", build.error().message);
        return;
    }
    if (build->exitCode != 0)
    {
        levain::core::log("shaders", levain::core::LogLevel::Error,
                          "compilation échouée, les shaders en place restent :\n{}",
                          firstFailure(build->output));
        return;
    }
    levain::core::log("shaders", levain::core::LogLevel::Info, "recompilés en {:.0f} ms",
                      millisecondsSince(buildStart));

    // Seules les passes dont la source a changé recréent leur pipeline (#45).
    const bool touchesMeshPass = std::ranges::any_of(
        changed,
        [](const std::filesystem::path& file)
        {
            return std::ranges::contains(levain::render::MeshPassShaderFiles, file.stem().string());
        });
    if (!touchesMeshPass)
    {
        levain::core::log("shaders", levain::core::LogLevel::Info, "aucun pipeline à recréer");
        return;
    }
    const Clock::time_point pipelineStart = Clock::now();
    if (auto reloaded = levain::render::reloadMeshPassShaders(device, meshPass, target); !reloaded)
    {
        levain::core::log("shaders", levain::core::LogLevel::Error, "{}", reloaded.error().message);
        return;
    }
    levain::core::log("shaders", levain::core::LogLevel::Info,
                      "pipeline des meshes recréé en {:.1f} ms", millisecondsSince(pipelineStart));
}
