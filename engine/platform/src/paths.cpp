#include "levain/platform/paths.hpp"

#include <format>
#include <string>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

namespace levain::platform
{

core::Result<std::filesystem::path> prefPath(std::string_view organization,
                                             std::string_view application)
{
    // SDL veut des chaînes C : les vues sont recopiées.
    const std::string org{organization};
    const std::string app{application};
    // Le résultat est à nous (SDL_free), contrairement à SDL_GetBasePath.
    char* raw = SDL_GetPrefPath(org.c_str(), app.c_str());
    if (raw == nullptr)
    {
        return core::makeError(
            core::ErrorCode::FileNotFound,
            std::format("dossier des préférences ({}/{}) : {}", org, app, SDL_GetError()));
    }
    // Construit depuis un `u8string` : SDL rend de l'UTF-8, et le profil d'un utilisateur peut
    // porter des accents (sous Windows, `path(const char*)` lirait la page de code du processus ;
    // ADR-0035).
    std::filesystem::path result{std::u8string(reinterpret_cast<const char8_t*>(raw))};
    SDL_free(raw);
    return result;
}

} // namespace levain::platform
