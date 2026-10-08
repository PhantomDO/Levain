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

/// Ajoute `code` à `utf8`, en 1 à 4 octets : la tête dit combien d'octets suivent, chacun porte six
/// bits.
void appendUtf8(std::string& utf8, char32_t code)
{
    const auto tail = [code](unsigned shift)
    { return static_cast<char>(0x80U | ((code >> shift) & 0x3FU)); };
    if (code < 0x80U)
    {
        utf8 += static_cast<char>(code);
    }
    else if (code < 0x800U)
    {
        utf8 += static_cast<char>(0xC0U | (code >> 6U));
        utf8 += tail(0);
    }
    else if (code < 0x10000U)
    {
        utf8 += static_cast<char>(0xE0U | (code >> 12U));
        utf8 += tail(6);
        utf8 += tail(0);
    }
    else
    {
        utf8 += static_cast<char>(0xF0U | (code >> 18U));
        utf8 += tail(12);
        utf8 += tail(6);
        utf8 += tail(0);
    }
}

} // namespace

core::Result<std::vector<std::string>> shaderReloadCommand(const ShaderBuild& build,
                                                           HostSystem host)
{
    if (host == HostSystem::Linux)
    {
        return cmakeBuildOf(build, build.buildDir.string());
    }
    if (build.wslDistro.empty())
    {
        return core::makeError(
            core::ErrorCode::Unsupported,
            "ce programme Windows n'a pas été compilé dans une distro WSL (WSL_DISTRO_NAME absente "
            "à la configuration) : cmake et slangc sont des programmes Linux qu'il ne peut pas "
            "lancer, recompiler les shaders où il a été compilé (ADR-0035)");
    }
    // --exec et non « -- » : « -- » passe par le shell de la distro, qui coupe un argument à
    // espace ou à « ; » et développe un « $HOME » (essayé : wsl.exe -d <distro> -- /bin/echo 'c;d'
    // '$HOME'), quand --exec passe chaque argument tel quel (wsl.exe --help).
    std::vector<std::string> command{"wsl.exe", "-d", build.wslDistro, "--exec"};
    // Le dossier de build est un chemin de la distro, avec des « / » : generic_string les garde,
    // quelle que soit la façon dont Windows écrit un chemin.
    const std::vector<std::string> cmake = cmakeBuildOf(build, build.buildDir.generic_string());
    command.insert(command.end(), cmake.begin(), cmake.end());
    return command;
}

std::string utf8FromWslOutput(std::string_view output)
{
    if (output.find('\0') == std::string_view::npos)
    {
        return std::string{output};
    }
    const auto unit = [&output](std::size_t index)
    {
        return static_cast<char32_t>(static_cast<unsigned char>(output[index])) |
               static_cast<char32_t>(static_cast<unsigned char>(output[index + 1])) << 8U;
    };
    std::string utf8;
    for (std::size_t index = 0; index + 1 < output.size(); index += 2)
    {
        char32_t code = unit(index);
        if (code >= 0xD800 && code < 0xDC00 && index + 3 < output.size() &&
            unit(index + 2) >= 0xDC00 && unit(index + 2) < 0xE000)
        {
            code = 0x10000 + ((code - 0xD800) << 10U) + (unit(index + 2) - 0xDC00);
            index += 2;
        }
        else if (code >= 0xD800 && code < 0xE000)
        {
            code = 0xFFFD; // une moitié de paire seule
        }
        appendUtf8(utf8, code);
    }
    return utf8;
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
    auto command = shaderReloadCommand(build, CompiledHost);
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
        levain::core::log("shaders", levain::core::LogLevel::Warning,
                          "aucune source .slang lisible dans {} : le rechargement ne verra rien "
                          "(un exe Windows compilé dans une distro doit être lancé depuis elle)",
                          build.sourceDir.string());
    }
    return reload;
}

void reloadChangedShaders(ShaderReload& reload, nvrhi::IDevice& device,
                          const nvrhi::FramebufferInfo& target, levain::render::MeshPass& meshPass)
{
    const Clock::time_point now = Clock::now();
    if (reload.command.empty() || now < reload.nextCheck)
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
    const auto build = levain::platform::runProcess(reload.command);
    if (!build)
    {
        levain::core::log("shaders", levain::core::LogLevel::Error, "{}", build.error().message);
        return;
    }
    if (build->exitCode != 0)
    {
        levain::core::log("shaders", levain::core::LogLevel::Error,
                          "compilation échouée, les shaders en place restent :\n{}",
                          firstFailure(utf8FromWslOutput(build->output)));
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
