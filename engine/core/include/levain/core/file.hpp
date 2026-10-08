#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "levain/core/error.hpp"

namespace levain::core
{

/// Lit un fichier entier en mémoire. Un fichier absent ou illisible est un échec récupérable
/// (ADR-0008), pas un bug : c'est au contenu du disque qu'on ne peut pas se fier.
[[nodiscard]] Result<std::vector<std::byte>> readFile(const std::filesystem::path& path);

/// Le nom d'un fichier pour une bibliothèque C qui le prend en `char*` (ktx, stb). Le piège : sous
/// Windows, `path::c_str()` rend des `wchar_t`, que ces fonctions refusent ; `string()` rend des
/// `char` dans la page de code du processus, que stb passe à `fopen_s`, mais que ktx lit toujours
/// en UTF-8 (`ktxFOpenUTF8`) : les deux ne s'accordent que par le manifeste UTF-8 de l'ADR-0035.
/// Sous Linux, c'est le nom tel quel.
[[nodiscard]] std::string pathForC(const std::filesystem::path& path);

/// Les dates de modification des fichiers d'un dossier, pour repérer ceux qui changent (le
/// hot-reload des shaders, ADR-0014).
struct FileWatch
{
    std::filesystem::path directory;
    std::string extension; ///< « .slang » : les fichiers temporaires des éditeurs sont ignorés.
    std::map<std::filesystem::path, std::filesystem::file_time_type> lastWrites;
};

/// Commence à surveiller les fichiers `extension` de `directory`, sans ses sous-dossiers.
[[nodiscard]] FileWatch watchDirectory(std::filesystem::path directory, std::string extension);

/// Les fichiers créés ou modifiés depuis l'appel précédent, ou depuis `watchDirectory`. Un dossier
/// devenu illisible ne rend rien : la surveillance reprendra quand il reviendra.
[[nodiscard]] std::vector<std::filesystem::path> takeChangedFiles(FileWatch& watch);

} // namespace levain::core
