#include "levain/platform/process.hpp"

#include <cstddef>
#include <format>
#include <memory>
#include <vector>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_process.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_stdinc.h>

#include "levain/core/assert.hpp"

namespace levain::platform
{

core::Result<ProcessOutput> runProcess(std::span<const std::string> arguments,
                                       std::span<const EnvironmentVariable> environment)
{
    LEVAIN_ASSERT(!arguments.empty(), "runProcess attend au moins le programme à lancer");

    // SDL veut un tableau de chaînes C terminé par nullptr.
    std::vector<const char*> argv;
    argv.reserve(arguments.size() + 1);
    for (const std::string& argument : arguments)
    {
        argv.push_back(argument.c_str());
    }
    argv.push_back(nullptr);

    // Les propriétés plutôt que SDL_CreateProcess : c'est la seule façon de mêler la sortie
    // d'erreur à la sortie standard, où les compilateurs écrivent leurs erreurs (SDL_process.h,
    // SDL_CreateProcessWithProperties).
    const SDL_PropertiesID properties = SDL_CreateProperties();
    SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER,
                           static_cast<void*>(argv.data()));
    SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
    // L'environnement de ce processus, plus `environment` : SDL n'en passe un autre au programme
    // que par SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, une copie qu'il ne garde pas au-delà de
    // la création.
    const std::unique_ptr<SDL_Environment, decltype(&SDL_DestroyEnvironment)> childEnvironment{
        environment.empty() ? nullptr : SDL_CreateEnvironment(true), &SDL_DestroyEnvironment};
    if (!environment.empty())
    {
        if (!childEnvironment)
        {
            SDL_DestroyProperties(properties);
            return core::makeError(core::ErrorCode::Unsupported,
                                   std::format("{} : environnement impossible à créer, {}",
                                               arguments.front(), SDL_GetError()));
        }
        for (const EnvironmentVariable& variable : environment)
        {
            SDL_SetEnvironmentVariable(childEnvironment.get(), variable.name.c_str(),
                                       variable.value.c_str(), true);
        }
        SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER,
                               childEnvironment.get());
    }
    const std::unique_ptr<SDL_Process, decltype(&SDL_DestroyProcess)> process{
        SDL_CreateProcessWithProperties(properties), &SDL_DestroyProcess};
    SDL_DestroyProperties(properties);
    if (!process)
    {
        return core::makeError(core::ErrorCode::FileNotFound,
                               std::format("{} : {}", arguments.front(), SDL_GetError()));
    }

    // Lit toute la sortie jusqu'à la fin du processus, puis rend son code de retour.
    std::size_t size = 0;
    int exitCode = -1;
    const std::unique_ptr<char, decltype(&SDL_free)> data{
        static_cast<char*>(SDL_ReadProcess(process.get(), &size, &exitCode)), &SDL_free};
    if (!data)
    {
        return core::makeError(
            core::ErrorCode::Unsupported,
            std::format("{} : sortie illisible, {}", arguments.front(), SDL_GetError()));
    }
    return ProcessOutput{.exitCode = exitCode, .output = std::string{data.get(), size}};
}

} // namespace levain::platform
