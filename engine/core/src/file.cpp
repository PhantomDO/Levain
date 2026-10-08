#include "levain/core/file.hpp"

#include <cstdint>
#include <format>
#include <fstream>
#include <system_error>
#include <utility>

namespace levain::core
{

Result<std::vector<std::byte>> readFile(const std::filesystem::path& path)
{
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    std::ifstream file{path, std::ios::binary};
    if (error || !file)
    {
        return makeError(ErrorCode::FileNotFound,
                         std::format("{} : introuvable ou illisible", path.string()));
    }

    std::vector<std::byte> bytes(size);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size)))
    {
        return makeError(ErrorCode::InvalidData,
                         std::format("{} : lecture incomplète", path.string()));
    }
    return bytes;
}

std::string pathForC(const std::filesystem::path& path)
{
    return path.string();
}

FileWatch watchDirectory(std::filesystem::path directory, std::string extension)
{
    FileWatch watch{
        .directory = std::move(directory), .extension = std::move(extension), .lastWrites = {}};
    (void)takeChangedFiles(watch); // relève les dates de départ
    return watch;
}

std::vector<std::filesystem::path> takeChangedFiles(FileWatch& watch)
{
    // Les versions sans exception : un fichier peut disparaître entre la liste et la lecture de sa
    // date, le temps qu'un éditeur le remplace par sa nouvelle version.
    std::vector<std::filesystem::path> changed;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator{watch.directory, error})
    {
        if (!entry.is_regular_file(error) || entry.path().extension() != watch.extension)
        {
            continue;
        }
        const std::filesystem::file_time_type lastWrite = entry.last_write_time(error);
        if (error)
        {
            continue;
        }
        auto [known, isNew] = watch.lastWrites.try_emplace(entry.path(), lastWrite);
        if (isNew || known->second != lastWrite)
        {
            known->second = lastWrite;
            changed.push_back(entry.path());
        }
    }
    return changed;
}

} // namespace levain::core
