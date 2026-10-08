#include "shader_reload.hpp"

#include <algorithm>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "levain/core/log.hpp"
#include "levain/platform/process.hpp"

namespace levain::app
{

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

/// `cmake --build <dossier> --target levain_shaders`, la commande de l'ADR-0014.
std::vector<std::string> cmakeBuildOf(const ShaderBuild& build, std::string buildDir)
{
    return {build.cmakeCommand, "--build", std::move(buildDir), "--target",
            std::string{ShaderTarget}};
}

/// La distro et le winsysroot de cet arbre de build (engine/app/CMakeLists.txt).
WslBuild compiledWslBuild()
{
    return {.distro = LEVAIN_WSL_DISTRO, .winsysroot = LEVAIN_WSL_WINSYSROOT};
}

} // namespace

core::Result<ShaderReloadCommand> shaderReloadCommand(const ShaderBuild& build, ExeSystem system,
                                                      const WslBuild& wsl)
{
    if (system == ExeSystem::Linux)
    {
        return ShaderReloadCommand{.arguments = cmakeBuildOf(build, build.buildDir.string()),
                                   .environment = {}};
    }
    if (wsl.distro.empty())
    {
        return core::makeError(
            core::ErrorCode::Unsupported,
            "ce programme Windows n'a pas été compilé dans une distro WSL (WSL_DISTRO_NAME absente "
            "à la configuration) : cmake et slangc sont des programmes Linux qu'il ne peut pas "
            "lancer, recompiler les shaders où il a été compilé (ADR-0035)");
    }
    // --exec et non « -- » : « -- » donne la commande au shell de la distro, où un « ; » la coupe,
    // et où un « $ » ou un « \ » s'interprètent (mesuré : wsl.exe -d <distro> -- /usr/bin/printf
    // '[%s]' 'a b' 'c;d' rend « [a b][c] », puis « d: command not found »). --exec passe chaque
    // argument tel quel.
    ShaderReloadCommand command{.arguments = {"wsl.exe", "-d", wsl.distro, "--exec"},
                                // wsl.exe écrit ses propres messages (une distro inconnue, un
                                // service arrêté) en UTF-16, sauf avec WSL_UTF8=1.
                                .environment = {{.name = "WSL_UTF8", .value = "1"}}};
    // --exec ne lit pas ~/.bashrc : sans LEVAIN_WINSYSROOT, un arbre à régénérer (un CMakeLists
    // modifié depuis le dernier build) s'arrêterait sur la toolchain au lieu de recompiler.
    if (!wsl.winsysroot.empty())
    {
        command.arguments.insert(command.arguments.end(),
                                 {"/usr/bin/env", "LEVAIN_WINSYSROOT=" + wsl.winsysroot});
    }
    // Le dossier de build est un chemin de la distro, avec des « / » : generic_string les garde,
    // quelle que soit la façon dont Windows écrit un chemin.
    const std::vector<std::string> cmake = cmakeBuildOf(build, build.buildDir.generic_string());
    command.arguments.insert(command.arguments.end(), cmake.begin(), cmake.end());
    return command;
}

ShaderReload startShaderReload(const ShaderBuild& build)
{
    ShaderReload reload{.build = build,
                        .command = {},
                        .sources = {},
                        .nextCheck = Clock::now() + ShaderCheckPeriod};
    if (build.sourceDir.empty())
    {
        return reload; // le navigateur : ni sources ni build, rien à dire
    }
    auto command = shaderReloadCommand(build, CompiledSystem, compiledWslBuild());
    if (!command)
    {
        levain::core::log("shaders", levain::core::LogLevel::Warning,
                          "rechargement des shaders désactivé : {}", command.error().message);
        return reload;
    }
    reload.command = std::move(*command);
    reload.sources = levain::core::watchDirectory(build.sourceDir, ".slang");
    if (reload.sources.lastWrites.empty())
    {
        // Règle n°7 : une surveillance qui ne voit rien ne doit pas passer pour une surveillance
        // qui ne voit aucun changement. Sous Windows, un chemin « /home/… » ne se résout que si le
        // dossier courant est celui de la distro (lancer l'exe depuis elle).
        levain::core::log(
            "shaders", levain::core::LogLevel::Warning,
            "aucune source .slang lisible dans {} : le rechargement ne verra rien{}",
            build.sourceDir.string(),
            CompiledSystem == ExeSystem::Windows
                ? " (un exe Windows compilé dans une distro doit être lancé depuis elle)"
                : "");
    }
    return reload;
}

void reloadChangedShaders(ShaderReload& reload, nvrhi::IDevice& device,
                          const nvrhi::FramebufferInfo& target, levain::render::MeshPass& meshPass)
{
    const Clock::time_point now = Clock::now();
    if (reload.command.arguments.empty() || now < reload.nextCheck)
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
    // ponytail: bloquant, la boucle s'arrête le temps du build : ~350 ms sous Linux (ADR-0014),
    // ~900 ms pour un exe Windows, dont ~100 ms pour démarrer wsl.exe (build/GOTCHA.md). Un thread
    // si ça gêne.
    const Clock::time_point buildStart = Clock::now();
    const auto build =
        levain::platform::runProcess(reload.command.arguments, reload.command.environment);
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

} // namespace levain::app
