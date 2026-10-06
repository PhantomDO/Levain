#include "levain/app/texture_reload.hpp"

#include <algorithm>
#include <filesystem>
#include <set>
#include <system_error>
#include <vector>

#include "model_textures.hpp"

#include "levain/core/log.hpp"

namespace levain::app
{

namespace
{

using Clock = std::chrono::steady_clock;

/// Comme les shaders : au plus 100 ms entre l'enregistrement d'une texture et sa détection.
constexpr std::chrono::milliseconds TextureCheckPeriod{100};

/// Le temps écoulé depuis la dernière modification de `file`. `file_clock` : la même horloge que
/// les dates de modification, sans conversion.
double millisecondsSinceWrite(const std::filesystem::path& file)
{
    std::error_code error;
    const auto age = std::filesystem::file_time_type::clock::now() -
                     std::filesystem::last_write_time(file, error);
    return std::chrono::duration<double, std::milli>(age).count();
}

double millisecondsSince(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

} // namespace

TextureReload startTextureReload(const assets::AssetRegistry& registry)
{
    return TextureReload{.watch = assets::watchAssets(registry),
                         .nextCheck = Clock::now() + TextureCheckPeriod};
}

void reloadChangedTextures(TextureReload& reload, nvrhi::IDevice& device,
                           assets::AssetRegistry& registry, const assets::ModelCache& modelCache,
                           std::map<assets::AssetId, ModelGpu>& models,
                           const render::MeshPass& meshPass, nvrhi::ISampler& sampler)
{
    const Clock::time_point now = Clock::now();
    if (now < reload.nextCheck)
    {
        return;
    }
    reload.nextCheck = now + TextureCheckPeriod;
    const std::vector<assets::AssetId> changed = assets::takeChangedAssets(registry, reload.watch);
    if (changed.empty())
    {
        return;
    }

    const nvrhi::CommandListHandle commandList = device.createCommandList();
    commandList->open();
    std::set<assets::AssetId> touchedModels;
    for (const assets::AssetId& id : changed)
    {
        const std::filesystem::path file = registry.entries.at(id).file;
        // Une texture dans son propre fichier est le sous-asset 0 de son GUID (ADR-0020).
        const assets::AssetRef ref{.asset = id, .sub = 0};
        // Ses versions en usage : couleur, données, ou les deux (TextureKey).
        std::vector<bool> versions;
        for (const bool linear : {false, true})
        {
            if (std::ranges::any_of(models, [&](const auto& model)
                                    { return model.second.textures.contains({ref, linear}); }))
            {
                versions.push_back(linear);
            }
        }
        if (versions.empty())
        {
            core::log("assets", core::LogLevel::Info,
                      "{} modifié : aucune texture en usage, rien à recharger (seules les "
                      "textures se rechargent à chaud, ADR-0021)",
                      file.filename().string());
            continue;
        }
        const Clock::time_point loadStart = Clock::now();
        bool loaded = true;
        for (const bool linear : versions)
        {
            auto texture = uploadTexture(device, *commandList, registry, modelCache, {ref, linear},
                                         textureTargetOf(device));
            if (!texture)
            {
                core::log("assets", core::LogLevel::Error, "{} ; l'ancienne texture reste",
                          texture.error().message);
                loaded = false;
                break;
            }
            for (auto& [modelId, gpu] : models)
            {
                if (auto slot = gpu.textures.find({ref, linear}); slot != gpu.textures.end())
                {
                    slot->second = texture->handle;
                    touchedModels.insert(modelId);
                }
            }
        }
        if (!loaded)
        {
            continue;
        }
        // Le critère de M4.4 : l'âge du fichier quand la texture est prête, visible à l'image
        // suivante.
        core::log("assets", core::LogLevel::Info,
                  "{} rechargée en {:.0f} ms, {:.0f} ms après son écriture",
                  file.filename().string(), millisecondsSince(loadStart),
                  millisecondsSinceWrite(file));
    }
    // Un binding set désigne ses textures : ceux des modèles touchés sont refaits, avant la
    // fermeture de l'envoi, qui porte leurs constantes. NVRHI garde les anciens, et l'ancienne
    // texture, tant qu'une image en vol s'en sert.
    for (const assets::AssetId& modelId : touchedModels)
    {
        ModelGpu& gpu = models.at(modelId);
        gpu.materials.clear();
        bindModelMaterials(device, *commandList, meshPass, sampler, modelCache.models.at(modelId),
                           gpu);
    }
    commandList->close();
    device.executeCommandList(commandList);
}

} // namespace levain::app
