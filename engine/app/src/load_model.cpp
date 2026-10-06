#include "levain/app/load_model.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <utility>

#include "levain/animation/animation_set.hpp"
#include "levain/animation/animator.hpp"
#include "levain/app/models.hpp"
#include "levain/assets/asset_ref.hpp"
#include "levain/assets/registry.hpp"
#include "levain/core/log.hpp"

namespace levain::app
{

namespace
{

/// Ce qu'un modèle skinné joue : son squelette et ses clips, le clip en boucle, ou les clips de sa
/// locomotion, résolus par leurs noms.
struct ModelAnimation
{
    animation::AnimationSet set;
    std::size_t clip = 0;
    std::optional<animation::AnimatorClips> animatorClips;
};

/// Les clips de la locomotion, par leurs noms. Les autres états de l'animateur n'ont pas de clip :
/// ils gardent la locomotion.
core::Result<animation::AnimatorClips> animatorClipsOf(const animation::AnimationSet& set,
                                                       const LocomotionClips& locomotion)
{
    std::array<std::size_t, 3> indices{};
    for (std::size_t i = 0; i < indices.size(); ++i)
    {
        auto clip = clipIndexOf(set, locomotion.names[i]);
        if (!clip)
        {
            return std::unexpected(clip.error());
        }
        indices[i] = *clip;
    }
    return animation::AnimatorClips{.ground = {.idle = indices[0],
                                               .walk = indices[1],
                                               .run = indices[2],
                                               .walkSpeed = locomotion.walkSpeed,
                                               .runSpeed = locomotion.runSpeed},
                                    .jump = std::nullopt,
                                    .fall = std::nullopt,
                                    .swim = std::nullopt,
                                    .glide = std::nullopt};
}

/// Le squelette et les clips d'un modèle skinné (ADR-0022), relus dans son glTF, et ce qu'il
/// joue. Le journal le dit, en une ligne.
// ponytail: squelette et clips viennent toujours de la source ; leur cuisson dans .cooked/
// (ADR-0022) viendra quand leur lecture pèsera au chargement (0,55 ms pour Fox).
core::Result<ModelAnimation> animationOf(const std::filesystem::path& gltf, const ModelLoad& load)
{
    auto set = animation::importAnimationSet(gltf);
    if (!set)
    {
        return std::unexpected(set.error());
    }
    auto clip = clipIndexOf(*set, load.clip);
    if (!clip)
    {
        return std::unexpected(clip.error());
    }
    ModelAnimation result{.set = std::move(*set), .clip = *clip, .animatorClips = std::nullopt};
    if (load.locomotion)
    {
        auto clips = animatorClipsOf(result.set, *load.locomotion);
        if (!clips)
        {
            return std::unexpected(clips.error());
        }
        result.animatorClips = *clips;
        core::log("app", core::LogLevel::Info,
                  "modèle skinné : {} os, locomotion « {},{},{} », marche à {} et course à {}",
                  result.set.jointNames.size(), load.locomotion->names[0],
                  load.locomotion->names[1], load.locomotion->names[2], load.locomotion->walkSpeed,
                  load.locomotion->runSpeed);
    }
    else
    {
        core::log("app", core::LogLevel::Info,
                  "modèle skinné : {} os, clip « {} » ({:.2f} s) joué en boucle",
                  result.set.jointNames.size(), result.set.clips[result.clip].name,
                  result.set.clips[result.clip].durationSeconds);
    }
    return result;
}

} // namespace

core::Result<LoadedModel> loadModel(App& app, const ModelLoad& load)
{
    using Clock = std::chrono::steady_clock;
    const Clock::time_point start = Clock::now();
    const auto id = assets::idOf(app.registry, load.path);
    if (!id)
    {
        return core::makeError(
            core::ErrorCode::InvalidData,
            std::format("{} : pas un asset d'une racine connue", load.path.string()));
    }
    if (app.models.contains(*id))
    {
        // Rangés par asset : un même modèle chargé deux fois n'aurait qu'un animateur.
        return core::makeError(core::ErrorCode::InvalidData,
                               std::format("{} chargé deux fois : un modèle ne se charge qu'une "
                                           "fois (ADR-0029)",
                                           load.path.string()));
    }
    auto read = assets::loadModel(app.modelCache, app.registry, *id);
    if (!read)
    {
        return std::unexpected(read.error());
    }
    const assets::Model& model = **read;
    std::optional<ModelAnimation> skeletal;
    if (isSkinned(model))
    {
        auto animated = animationOf(assets::pathOf(app.registry, *id).value_or(load.path), load);
        if (!animated)
        {
            return std::unexpected(animated.error());
        }
        skeletal = std::move(*animated);
    }

    nvrhi::IDevice& device = *app.gpu.nvrhi;
    const nvrhi::CommandListHandle upload = device.createCommandList();
    upload->open();
    const auto skinJoints =
        static_cast<std::uint32_t>(skeletal ? skeletal->set.skinJoints.size() : 0);
    auto uploaded =
        uploadModel(device, *upload, model, app.registry, app.modelCache, app.skinning, skinJoints);
    if (!uploaded)
    {
        submitAbandonedUpload(device, *upload);
        return std::unexpected(uploaded.error());
    }
    // Les matériaux avant la fermeture de l'envoi : leurs constantes passent par lui.
    bindModelMaterials(device, *upload, app.renderer.meshPass, *app.sampler, model, *uploaded);
    upload->close();
    device.executeCommandList(upload);
    if (skeletal)
    {
        uploaded->clip = skeletal->clip;
        if (skeletal->animatorClips)
        {
            uploaded->animatorClips = *skeletal->animatorClips;
            uploaded->animator = animation::Animator{};
        }
        uploaded->animation = std::move(skeletal->set);
    }
    core::log("app", core::LogLevel::Info,
              "modèle {} : {} meshes, {} matériaux, {} textures ({:.1f} Mo en mémoire vidéo), "
              "chargé en {:.0f} ms",
              load.name, model.meshes.size(), model.materials.size(), uploaded->textures.size(),
              static_cast<double>(uploaded->textureBytes) / (1024.0 * 1024.0),
              std::chrono::duration<double, std::milli>(Clock::now() - start).count());
    app.models.emplace(*id, std::move(*uploaded));
    const flecs::entity root =
        assets::instantiateModel(app.world, model, *id, load.name).set(load.placement);
    return LoadedModel{.root = root, .id = *id, .model = &model};
}

} // namespace levain::app
