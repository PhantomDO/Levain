#include "levain/assets/asset_ref.hpp"

#include <filesystem>
#include <format>

#include "levain/assets/cooked.hpp"
#include "levain/core/log.hpp"
#include "levain/core/profile.hpp"
#include "levain/scene/reflection.hpp"

namespace levain::assets
{

namespace
{

void addReference(AssetUsage& usage, const AssetRef& ref)
{
    if (ref.isSet())
    {
        ++usage.counts[ref.asset];
        usage.unused.erase(ref.asset);
    }
}

void removeReference(AssetUsage& usage, const AssetRef& ref)
{
    if (!ref.isSet())
    {
        return;
    }
    const auto count = usage.counts.find(ref.asset);
    if (count != usage.counts.end() && --count->second == 0)
    {
        usage.counts.erase(count);
        usage.unused.insert(ref.asset);
    }
}

} // namespace

AssetsModule::AssetsModule(flecs::world& world)
{
    world.module<AssetsModule>();
    world.set<AssetUsage>({});

    // Les hooks de flecs (manuel, « Component hooks ») : `on_replace` voit l'ancienne et la
    // nouvelle valeur à chaque `set`, la toute première comprise (l'ancienne est alors la valeur
    // par défaut, qui ne désigne rien) ; `on_remove` voit la valeur qui part, que le composant soit
    // retiré ou l'entité détruite. À eux deux, ils couvrent tous les chemins, testés un par un
    // (tests/asset_ref_test.cpp).
    //
    // Conséquence imposée par flecs : un `MeshRef` ne se modifie plus sur place (`get_mut`,
    // `ensure`), seulement par `set`, qui passe par `on_replace`. Et `entity.clone()` ne marche
    // plus sur une entité qui en porte un : flecs le fait par `get_mut`, et l'arrête sur une
    // assertion. On copie par `set`, et les prefabs (`IsA`) partagent le composant sans le copier.
    world.component<MeshRef>()
        .on_replace(
            [](flecs::entity entity, MeshRef& previous, MeshRef& next)
            {
                AssetUsage& usage = entity.world().get_mut<AssetUsage>();
                removeReference(usage, previous.mesh);
                addReference(usage, next.mesh);
            })
        .on_remove([](flecs::entity entity, MeshRef& ref)
                   { removeReference(entity.world().get_mut<AssetUsage>(), ref.mesh); });
    // En lecture seule (ADR-0034). Le JSON écrit le GUID de l'asset, deux entiers de 64 bits, et
    // l'indice du mesh, pas son nom : c'est le dessinateur d'asset de l'éditeur qui le nommera.
    // `AssetUsage`, un singleton de conteneurs, n'est pas décrit.
    scene::describe<MeshRef>(world);
}

int referenceCount(const flecs::world& world, AssetId asset)
{
    const AssetUsage& usage = world.get<AssetUsage>();
    const auto count = usage.counts.find(asset);
    return count == usage.counts.end() ? 0 : count->second;
}

std::vector<AssetId> takeUnusedAssets(flecs::world& world)
{
    AssetUsage& usage = world.get_mut<AssetUsage>();
    std::vector<AssetId> unused{usage.unused.begin(), usage.unused.end()};
    usage.unused.clear();
    return unused;
}

core::Result<const Model*> loadModel(ModelCache& cache, const AssetRegistry& registry,
                                     AssetId asset)
{
    LEVAIN_PROFILE_SCOPE();
    if (const auto loaded = cache.models.find(asset); loaded != cache.models.end())
    {
        return &loaded->second;
    }
    const auto path = pathOf(registry, asset);
    if (!path)
    {
        return core::makeError(
            core::ErrorCode::FileNotFound,
            std::format("asset {} inconnu du registre : son fichier a disparu, ou il a été renommé "
                        "et modifié à la fois (ADR-0019)",
                        toString(asset)));
    }
    // La version cuite, si elle est à jour (ADR-0020) ; sinon la source, et on le signale : on a
    // toujours une image, et le retard de cuisson se voit.
    const auto cooked = cookedPathOf(registry, asset, ".lvmesh");
    if (cooked && std::filesystem::exists(*cooked))
    {
        auto read = readCookedModel(*cooked, registry.entries.at(asset).hash);
        if (read)
        {
            return &cache.models.emplace(asset, std::move(*read)).first->second;
        }
        core::log("assets", core::LogLevel::Warning, "{} ; chargé depuis la source",
                  read.error().message);
    }
    else
    {
        core::log("assets", core::LogLevel::Warning,
                  "{} n'est pas cuit : chargé depuis la source (lancer levain_cook)",
                  path->string());
    }
    auto model = loadGltf(*path, asset, registry);
    if (!model)
    {
        return std::unexpected(model.error());
    }
    return &cache.models.emplace(asset, std::move(*model)).first->second;
}

core::Result<Image> loadTexture(const AssetRegistry& registry, const ModelCache& models,
                                AssetRef texture)
{
    // Une image embarquée : dans le modèle qui la porte, déjà chargé.
    if (const auto model = models.models.find(texture.asset); model != models.models.end())
    {
        const auto image = model->second.embeddedImages.find(texture.sub);
        if (image == model->second.embeddedImages.end())
        {
            return core::makeError(core::ErrorCode::InvalidData,
                                   std::format("modèle {} : pas d'image embarquée {}",
                                               toString(texture.asset), texture.sub));
        }
        return image->second;
    }
    // Un fichier image du registre.
    const auto path = pathOf(registry, texture.asset);
    if (!path)
    {
        return core::makeError(
            core::ErrorCode::FileNotFound,
            std::format("texture {} inconnue du registre (ADR-0019)", toString(texture.asset)));
    }
    return loadImage(*path);
}

std::optional<std::filesystem::path> cookedTextureStem(const AssetRegistry& registry,
                                                       AssetRef texture, ImageEncoding encoding)
{
    const auto entry = registry.entries.find(texture.asset);
    if (entry == registry.entries.end())
    {
        return std::nullopt;
    }
    const std::filesystem::path extension = entry->second.file.extension();
    const bool embedded = extension == ".gltf" || extension == ".glb";
    std::string name = embedded ? std::format("{}.{}", toString(texture.asset), texture.sub)
                                : toString(texture.asset);
    if (encoding == ImageEncoding::Linear)
    {
        name += ".linear";
    }
    return entry->second.root / ".cooked" / name;
}

core::Result<TextureData> loadTextureData(const AssetRegistry& registry, const ModelCache& models,
                                          AssetRef texture, TextureFormat target,
                                          ImageEncoding encoding)
{
    const auto stem = cookedTextureStem(registry, texture, encoding);
    if (!stem)
    {
        return core::makeError(
            core::ErrorCode::FileNotFound,
            std::format("texture {} inconnue du registre (ADR-0019)", toString(texture.asset)));
    }
    const std::uint64_t hash = registry.entries.at(texture.asset).hash;
    const auto suffixed = [&](std::string_view suffix)
    {
        std::filesystem::path path = *stem;
        path += suffix;
        return path;
    };

    // Le cache de la plateforme, puis le maître UASTC : le premier qui existe et soit à jour.
    std::string why = "pas cuite";
    for (const auto& path :
         {target == TextureFormat::Bc7Srgb ? suffixed(".bc7.ktx2") : std::filesystem::path{},
          suffixed(".ktx2")})
    {
        if (path.empty() || !std::filesystem::exists(path))
        {
            continue;
        }
        auto cooked = readCookedTexture(path, hash, target);
        if (cooked)
        {
            return cooked;
        }
        why = cooked.error().message;
    }

    core::log("assets", core::LogLevel::Warning,
              "texture {} {} : chargée depuis la source (lancer levain_cook)",
              stem->filename().string(), why);
    auto image = loadTexture(registry, models, texture);
    if (!image)
    {
        return std::unexpected(image.error());
    }
    return textureDataOf(buildMipChain(std::move(*image), encoding));
}

} // namespace levain::assets
