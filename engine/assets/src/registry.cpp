#include "levain/assets/registry.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <string>
#include <system_error>

#include "levain/core/file.hpp"
#include "levain/core/log.hpp"
#include "levain/core/profile.hpp"

namespace levain::assets
{

namespace
{

namespace fs = std::filesystem;

core::Result<std::uint64_t> hashOfFile(const fs::path& path)
{
    auto bytes = core::readFile(path);
    if (!bytes)
    {
        return std::unexpected(bytes.error());
    }
    return contentHash(*bytes);
}

/// Réécrit le hash d'un `.meta`, sans toucher à son GUID.
core::Result<void> updateMetaHash(const fs::path& metaPath, std::uint64_t hash)
{
    auto meta = readMeta(metaPath);
    if (!meta)
    {
        return std::unexpected(meta.error());
    }
    meta->hash = hash;
    return writeMeta(metaPath, *meta);
}

/// Enregistre `id` pour `path`, ou échoue si un autre fichier le porte déjà (cas 5).
core::Result<void> registerAsset(AssetRegistry& registry, AssetId id, const fs::path& path,
                                 const fs::path& root, std::uint64_t hash)
{
    const auto [existing, inserted] =
        registry.entries.try_emplace(id, AssetEntry{.file = path, .root = root, .hash = hash});
    if (!inserted)
    {
        return core::makeError(
            core::ErrorCode::InvalidData,
            std::format(
                "{} et {} ont le même GUID {} : un fichier copié avec son .meta ? Supprimer "
                "le .meta de la copie, un nouveau GUID lui sera donné",
                existing->second.file.string(), path.string(), toString(id)));
    }
    return {};
}

} // namespace

bool isImportable(const fs::path& path)
{
    static constexpr std::array Extensions{".png", ".jpg", ".jpeg", ".gltf", ".glb"};
    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return std::ranges::find(Extensions, extension) != Extensions.end();
}

core::Result<ScanReport> scanAssets(const fs::path& root, AssetRegistry& registry)
{
    LEVAIN_PROFILE_SCOPE();
    std::error_code error;
    if (!fs::is_directory(root, error))
    {
        return core::makeError(core::ErrorCode::FileNotFound,
                               std::format("{} : racine d'assets introuvable", root.string()));
    }

    // Trié : deux scans du même dossier se déroulent dans le même ordre, et rattachent pareil.
    std::vector<fs::path> assets;
    std::vector<fs::path> orphans; ///< Les .meta dont le fichier n'existe plus.
    for (const fs::directory_entry& entry : fs::recursive_directory_iterator{root, error})
    {
        const fs::path& path = entry.path();
        if (!entry.is_regular_file())
        {
            continue;
        }
        if (path.extension() == ".meta")
        {
            if (!fs::exists(path.parent_path() / path.stem()))
            {
                orphans.push_back(path);
            }
        }
        else if (isImportable(path))
        {
            assets.push_back(path);
        }
    }
    std::ranges::sort(assets);
    std::ranges::sort(orphans);

    ScanReport report;
    std::vector<std::pair<fs::path, std::uint64_t>> withoutMeta;
    for (const fs::path& asset : assets)
    {
        // ponytail: chaque asset est relu et haché à chaque scan (50 Mo pour Sponza). Comparer la
        // date de modification d'abord si le démarrage s'en ressent.
        auto hash = hashOfFile(asset);
        if (!hash)
        {
            return std::unexpected(hash.error());
        }
        const fs::path metaPath = metaPathOf(asset);
        if (!fs::exists(metaPath))
        {
            withoutMeta.emplace_back(asset, *hash);
            continue;
        }
        auto meta = readMeta(metaPath);
        if (!meta)
        {
            return std::unexpected(meta.error());
        }
        // Cas 1. Le hash suit le contenu, pour qu'un renommage futur le retrouve.
        if (meta->hash != *hash)
        {
            meta->hash = *hash;
            if (auto written = writeMeta(metaPath, *meta); !written)
            {
                return std::unexpected(written.error());
            }
        }
        if (auto registered = registerAsset(registry, meta->id, asset, root, *hash); !registered)
        {
            return std::unexpected(registered.error());
        }
    }

    for (const auto& [asset, hash] : withoutMeta)
    {
        // Cas 2 : un .meta orphelin de même hash, c'est ce fichier renommé.
        AssetMeta meta{.id = generateAssetId(), .hash = hash};
        bool reattached = false;
        for (auto orphan = orphans.begin(); orphan != orphans.end(); ++orphan)
        {
            auto candidate = readMeta(*orphan);
            if (candidate && candidate->hash == hash)
            {
                meta.id = candidate->id;
                fs::rename(*orphan, metaPathOf(asset), error);
                if (error)
                {
                    return core::makeError(
                        core::ErrorCode::InvalidData,
                        std::format("{} : {}", orphan->string(), error.message()));
                }
                orphans.erase(orphan);
                reattached = true;
                break;
            }
        }
        // Cas 3 : un nouvel asset.
        if (!reattached)
        {
            if (auto written = writeMeta(metaPathOf(asset), meta); !written)
            {
                return std::unexpected(written.error());
            }
        }
        (reattached ? report.reattached : report.created).push_back(asset);
        if (auto registered = registerAsset(registry, meta.id, asset, root, hash); !registered)
        {
            return std::unexpected(registered.error());
        }
    }

    report.orphans = std::move(orphans); // Cas 4.
    return report;
}

AssetWatch watchAssets(const AssetRegistry& registry)
{
    AssetWatch watch;
    for (const auto& [id, entry] : registry.entries)
    {
        // Illisible, la date vaut file_time_type::min() : le fichier comptera comme modifié
        // quand il reviendra.
        std::error_code error;
        watch.lastWrites.emplace(id, fs::last_write_time(entry.file, error));
    }
    return watch;
}

std::vector<AssetId> takeChangedAssets(AssetRegistry& registry, AssetWatch& watch)
{
    std::vector<AssetId> changed;
    for (auto& [id, entry] : registry.entries)
    {
        // Les versions sans exception : un éditeur peut supprimer le fichier juste avant d'écrire
        // sa nouvelle version.
        std::error_code error;
        const fs::file_time_type lastWrite = fs::last_write_time(entry.file, error);
        if (error)
        {
            continue;
        }
        auto [known, isNew] = watch.lastWrites.try_emplace(id, lastWrite);
        if (isNew || known->second == lastWrite)
        {
            continue;
        }
        auto hash = hashOfFile(entry.file);
        if (!hash)
        {
            continue; // la date n'est pas relevée : retenté au prochain appel
        }
        known->second = lastWrite;
        if (*hash == entry.hash)
        {
            continue;
        }
        entry.hash = *hash;
        changed.push_back(id);

        // Le .meta suit, comme au scan (cas 1). S'il ne s'écrit pas, le registre a déjà le bon
        // hash, et le prochain scan réparera le fichier.
        if (auto written = updateMetaHash(metaPathOf(entry.file), *hash); !written)
        {
            core::log("assets", core::LogLevel::Warning, "{}", written.error().message);
        }
    }
    return changed;
}

std::optional<AssetId> idOf(const AssetRegistry& registry, const fs::path& path)
{
    std::error_code error;
    for (const auto& [id, entry] : registry.entries)
    {
        if (fs::equivalent(entry.file, path, error))
        {
            return id;
        }
    }
    return std::nullopt;
}

std::optional<fs::path> cookedPathOf(const AssetRegistry& registry, AssetId id,
                                     std::string_view extension)
{
    const auto found = registry.entries.find(id);
    if (found == registry.entries.end())
    {
        return std::nullopt;
    }
    return found->second.root / ".cooked" / (toString(id) + std::string{extension});
}

std::optional<fs::path> pathOf(const AssetRegistry& registry, AssetId id)
{
    const auto found = registry.entries.find(id);
    return found == registry.entries.end() ? std::nullopt : std::optional{found->second.file};
}

} // namespace levain::assets
