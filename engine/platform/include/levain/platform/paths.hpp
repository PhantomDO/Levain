#pragma once

#include <filesystem>
#include <string_view>

#include "levain/core/error.hpp"

namespace levain::platform
{

/// **Le dossier où un programme garde ce qui est à l'utilisateur** (ses préférences, la disposition
/// de ses fenêtres), créé s'il manque : `SDL_GetPrefPath`. Sous Windows c'est
/// `%APPDATA%\<organisation>\<application>\` (le profil itinérant), sous Linux
/// `$XDG_DATA_HOME/<organisation>/<application>/` (`~/.local/share/…` par défaut). Le seul endroit
/// de `platform` qui sache où vit l'utilisateur : un module qui garde un fichier le reçoit en
/// paramètre, et un test lui en donne un jetable (ADR-0036, décision 10).
/// Un échec est récupérable (ADR-0008) et dit pourquoi : disque en lecture seule, profil absent.
/// Équivalents : `FPaths::ProjectSavedDir` d'Unreal, `Application.persistentDataPath` d'Unity,
/// `user://` de Godot.
[[nodiscard]] core::Result<std::filesystem::path> prefPath(std::string_view organization,
                                                           std::string_view application);

} // namespace levain::platform
