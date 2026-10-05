#include "levain/assets/asset_id.hpp"

#include <charconv>
#include <format>
#include <fstream>
#include <random>
#include <ranges>

#include "levain/core/file.hpp"

namespace levain::assets
{

namespace
{

std::string_view trimmed(std::string_view text)
{
    const auto first = text.find_first_not_of(" \t\r");
    const auto last = text.find_last_not_of(" \t\r");
    return first == std::string_view::npos ? std::string_view{}
                                           : text.substr(first, last - first + 1);
}

std::optional<std::uint64_t> hexNumber(std::string_view text)
{
    std::uint64_t value = 0;
    const char* const first = text.data();
    const char* const last = first + text.size();
    const auto parsed = std::from_chars(first, last, value, 16);
    return parsed.ec == std::errc{} && parsed.ptr == last && !text.empty() ? std::optional{value}
                                                                           : std::nullopt;
}

} // namespace

AssetId generateAssetId()
{
    // random_device pour la graine seulement : il peut être lent, et le générateur suffit ensuite.
    static thread_local std::mt19937_64 generator{std::random_device{}()};
    return AssetId{.high = generator(), .low = generator()};
}

std::string toString(AssetId id)
{
    return std::format("{:016x}{:016x}", id.high, id.low);
}

std::optional<AssetId> parseAssetId(std::string_view text)
{
    if (text.size() != 32)
    {
        return std::nullopt;
    }
    const auto high = hexNumber(text.substr(0, 16));
    const auto low = hexNumber(text.substr(16));
    return high && low ? std::optional{AssetId{.high = *high, .low = *low}} : std::nullopt;
}

std::uint64_t contentHash(std::span<const std::byte> bytes)
{
    constexpr std::uint64_t Offset = 14695981039346656037ull;
    constexpr std::uint64_t Prime = 1099511628211ull;
    std::uint64_t hash = Offset;
    // La taille d'abord : deux fichiers de tailles différentes n'ont jamais le même hash.
    for (std::size_t shift = 0; shift < 64; shift += 8)
    {
        hash = (hash ^ ((bytes.size() >> shift) & 0xffu)) * Prime;
    }
    for (const std::byte byte : bytes)
    {
        hash = (hash ^ static_cast<std::uint64_t>(byte)) * Prime;
    }
    return hash;
}

std::filesystem::path metaPathOf(const std::filesystem::path& asset)
{
    std::filesystem::path meta = asset;
    meta += ".meta";
    return meta;
}

core::Result<AssetMeta> readMeta(const std::filesystem::path& path)
{
    auto bytes = core::readFile(path);
    if (!bytes)
    {
        return std::unexpected(bytes.error());
    }
    const std::string_view text{reinterpret_cast<const char*>(bytes->data()), bytes->size()};

    std::optional<AssetId> id;
    std::optional<std::uint64_t> hash;
    for (const auto rawLine : std::views::split(text, '\n'))
    {
        std::string_view line{rawLine};
        line = trimmed(line.substr(0, line.find('#')));
        const auto equals = line.find('=');
        if (equals == std::string_view::npos)
        {
            continue;
        }
        const std::string_view key = trimmed(line.substr(0, equals));
        const std::string_view value = trimmed(line.substr(equals + 1));
        if (key == "guid")
        {
            id = parseAssetId(value);
        }
        else if (key == "hash")
        {
            hash = hexNumber(value);
        }
    }
    if (!id || !hash)
    {
        return core::makeError(core::ErrorCode::InvalidData,
                               std::format("{} : guid ou hash absent ou illisible", path.string()));
    }
    return AssetMeta{.id = *id, .hash = *hash};
}

core::Result<void> writeMeta(const std::filesystem::path& path, const AssetMeta& meta)
{
    std::ofstream file{path};
    file << "# Levain (ADR-0019) : à versionner avec le fichier.\n"
         << std::format("guid = {}\nhash = {:016x}\n", toString(meta.id), meta.hash);
    if (!file)
    {
        return core::makeError(core::ErrorCode::InvalidData,
                               std::format("{} : écriture impossible", path.string()));
    }
    return {};
}

} // namespace levain::assets
