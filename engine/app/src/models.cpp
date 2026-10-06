#include "levain/app/models.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <expected>
#include <format>
#include <string>

#include "levain/core/profile.hpp"

namespace levain::app
{

namespace
{

using Clock = std::chrono::steady_clock;

/// Les niveaux d'une texture chargée par `assets`, cuite ou non, pour `render`.
std::vector<render::TextureLevel> textureLevelsOf(const assets::TextureData& data)
{
    std::vector<render::TextureLevel> levels;
    levels.reserve(data.mips.size());
    for (const assets::TextureMip& mip : data.mips)
    {
        levels.push_back({.width = mip.width, .height = mip.height, .bytes = mip.bytes});
    }
    return levels;
}

/// Le format NVRHI d'une texture. `linear` : des données (rugosité-métal, normal map), que le GPU
/// lit telles quelles au lieu de les convertir depuis le sRGB.
// ponytail: les données sont cuites comme les couleurs : leurs mips sont moyennés en sRGB, et ceux
// des normal maps ne sont pas renormalisés. L'espace de couleur dans le .meta et des mips linéaires
// si un artefact se voit de loin.
nvrhi::Format nvrhiFormatOf(assets::TextureFormat format, bool linear)
{
    if (format == assets::TextureFormat::Bc7Srgb)
    {
        return linear ? nvrhi::Format::BC7_UNORM : nvrhi::Format::BC7_UNORM_SRGB;
    }
    return linear ? nvrhi::Format::RGBA8_UNORM : nvrhi::Format::SRGBA8_UNORM;
}

/// La pose d'un modèle skinné à `seconds` : son animateur s'il en a un, sinon son clip en boucle.
void poseModel(ModelGpu& model, const animation::AnimationSet& set,
               const animation::CharacterMotion& motion, double seconds)
{
    if (model.animator)
    {
        const animation::AnimatorLayers layers =
            animation::advanceAnimator(set, model.animatorClips, *model.animator, motion,
                                       static_cast<float>(seconds) - model.lastSeconds);
        animation::sampleBlend(set, layers, model.pose);
        return;
    }
    animation::samplePose(set, model.clip, static_cast<float>(seconds), model.pose);
}

} // namespace

std::vector<render::TextureLevel> textureLevelsOf(const std::vector<assets::Image>& mips)
{
    std::vector<render::TextureLevel> levels;
    levels.reserve(mips.size());
    for (const assets::Image& mip : mips)
    {
        levels.push_back({.width = mip.width,
                          .height = mip.height,
                          .bytes = std::as_bytes(std::span{mip.rgba})});
    }
    return levels;
}

assets::TextureFormat textureTargetOf(nvrhi::IDevice& device)
{
    return render::supportsSampledFormat(device, nvrhi::Format::BC7_UNORM_SRGB)
               ? assets::TextureFormat::Bc7Srgb
               : assets::TextureFormat::Rgba8Srgb;
}

core::Result<UploadedTexture> uploadTexture(nvrhi::IDevice& device,
                                            nvrhi::ICommandList& commandList,
                                            const assets::AssetRegistry& registry,
                                            const assets::ModelCache& models, TextureKey key,
                                            assets::TextureFormat target)
{
    const auto [ref, linear] = key;
    auto data = assets::loadTextureData(registry, models, ref, target,
                                        linear ? assets::ImageEncoding::Linear
                                               : assets::ImageEncoding::Srgb);
    if (!data)
    {
        return std::unexpected(data.error());
    }
    // Le nom du fichier source, pour retrouver la texture dans une capture RenderDoc
    // (tools/renderdoc-mips.py). NVRHI le recopie.
    const std::string name =
        assets::pathOf(registry, ref.asset).value_or("glTF").filename().string();
    UploadedTexture texture{.handle = render::createTexture(device, commandList,
                                                            textureLevelsOf(*data), name.c_str(),
                                                            nvrhiFormatOf(data->format, linear)),
                            .bytes = 0};
    for (const assets::TextureMip& mip : data->mips)
    {
        texture.bytes += mip.bytes.size();
    }
    return texture;
}

core::Result<ModelGpu>
uploadModel(nvrhi::IDevice& device, nvrhi::ICommandList& commandList, const assets::Model& model,
            const assets::AssetRegistry& registry, const assets::ModelCache& models,
            const render::SkinningPass& skinning, std::uint32_t skinJointCount)
{
    const assets::TextureFormat target = textureTargetOf(device);
    ModelGpu gpu;
    std::vector<render::MeshVertex> vertices;
    for (const assets::ModelMesh& mesh : model.meshes)
    {
        std::vector<ModelPrimitiveGpu>& primitives = gpu.meshes.emplace_back();
        for (const assets::MeshPrimitive& primitive : mesh.primitives)
        {
            // Le matériau porte sa couleur de base (MaterialConstants) : le sommet reste blanc.
            const std::optional<glm::vec3> baseColor =
                primitive.material ? std::optional{glm::vec3{1.0f}} : std::nullopt;
            vertices.clear();
            for (const assets::ModelVertex& vertex : primitive.vertices)
            {
                vertices.push_back({.position = vertex.position,
                                    .normal = vertex.normal,
                                    .tangent = vertex.tangent,
                                    .color = baseColor.value_or(vertex.normal * 0.5f + 0.5f),
                                    .uv = vertex.uv});
            }
            if (primitive.joints.empty())
            {
                primitives.push_back(
                    {.mesh = render::createMesh(device, commandList, vertices, primitive.indices),
                     .material = primitive.material,
                     .skin = std::nullopt});
                continue;
            }
            // Skinné : les mêmes sommets, avec leurs os et leurs poids, que le compute déformera.
            std::vector<render::SkinnedVertex> skinned;
            skinned.reserve(vertices.size());
            for (std::size_t v = 0; v < vertices.size(); ++v)
            {
                skinned.push_back({.position = vertices[v].position,
                                   .normal = vertices[v].normal,
                                   .tangent = vertices[v].tangent,
                                   .color = vertices[v].color,
                                   .uv = vertices[v].uv,
                                   .joints = primitive.joints[v],
                                   .weights = primitive.weights[v]});
            }
            render::SkinnedMesh skin = render::createSkinnedMesh(
                device, commandList, skinning, skinned, primitive.indices, skinJointCount);
            primitives.push_back(
                {.mesh = skin.skinned, .material = primitive.material, .skin = std::move(skin)});
        }
    }
    for (const assets::ModelMaterial& material : model.materials)
    {
        const std::array<std::pair<std::optional<assets::AssetRef>, bool>, 3> slots{{
            {material.baseColorTexture, false},
            {material.metallicRoughnessTexture, true},
            {material.normalTexture, true},
        }};
        for (const auto& [ref, linear] : slots)
        {
            if (!ref || gpu.textures.contains({*ref, linear}))
            {
                continue;
            }
            auto texture =
                uploadTexture(device, commandList, registry, models, {*ref, linear}, target);
            if (!texture)
            {
                return std::unexpected(texture.error());
            }
            gpu.textures.emplace(TextureKey{*ref, linear}, texture->handle);
            gpu.textureBytes += texture->bytes;
        }
    }
    gpu.defaults = render::createMaterialDefaults(device, commandList);
    return gpu;
}

void submitAbandonedUpload(nvrhi::IDevice& device, nvrhi::ICommandList& commandList)
{
    commandList.close();
    device.executeCommandList(&commandList);
}

void bindModelMaterials(nvrhi::IDevice& device, nvrhi::ICommandList& commandList,
                        const render::MeshPass& pass, nvrhi::ISampler& sampler,
                        const assets::Model& model, ModelGpu& gpu)
{
    const auto textureOf = [&gpu](const std::optional<assets::AssetRef>& ref,
                                  bool linear) -> nvrhi::ITexture*
    { return ref ? gpu.textures.at({*ref, linear}).Get() : nullptr; };
    for (const assets::ModelMaterial& material : model.materials)
    {
        const render::MaterialConstants constants{
            .baseColorFactor = material.baseColorFactor,
            .metallicFactor = material.metallicFactor,
            .roughnessFactor = material.roughnessFactor,
            .normalScale = material.normalScale,
            .padding = 0.0f,
        };
        const render::MaterialTextures textures{
            .baseColor = textureOf(material.baseColorTexture, false),
            .metallicRoughness = textureOf(material.metallicRoughnessTexture, true),
            .normal = textureOf(material.normalTexture, true),
        };
        gpu.materials.push_back(
            render::createMaterialBindings(device, commandList, pass, constants,
                                           render::withDefaults(textures, gpu.defaults), sampler));
    }
}

bool isSkinned(const assets::Model& model)
{
    return std::ranges::any_of(model.meshes,
                               [](const assets::ModelMesh& mesh)
                               {
                                   return std::ranges::any_of(mesh.primitives,
                                                              [](const assets::MeshPrimitive& p)
                                                              { return !p.joints.empty(); });
                               });
}

core::Result<std::size_t> clipIndexOf(const animation::AnimationSet& set,
                                      const std::optional<std::string>& name)
{
    if (set.clips.empty())
    {
        return core::makeError(core::ErrorCode::InvalidData, "modèle skinné sans clip");
    }
    if (!name)
    {
        return 0;
    }
    std::string available;
    for (std::size_t clip = 0; clip < set.clips.size(); ++clip)
    {
        if (set.clips[clip].name == *name)
        {
            return clip;
        }
        available += (clip == 0 ? "" : ", ") + set.clips[clip].name;
    }
    return core::makeError(core::ErrorCode::InvalidData,
                           std::format("clip « {} » inconnu ; clips : {}", *name, available));
}

float maxJointSpeedOf(const animation::Pose& before, const animation::Pose& after, float seconds)
{
    float fastest = 0.0f;
    for (std::size_t joint = 0; joint < std::min(before.joints.size(), after.joints.size());
         ++joint)
    {
        fastest = std::max(fastest, glm::distance(glm::vec3(before.joints[joint][3]),
                                                  glm::vec3(after.joints[joint][3])) /
                                        seconds);
    }
    return fastest;
}

SkinningState createSkinningState(nvrhi::IDevice& device)
{
    return {.timer = render::createGpuTimer(device), .cost = {}};
}

void animateModels(nvrhi::IDevice& device, nvrhi::ICommandList& commandList,
                   std::map<assets::AssetId, ModelGpu>& models,
                   const render::SkinningPass& skinning, SkinningState& state,
                   const MotionOf& motionOf, double seconds)
{
    LEVAIN_PROFILE_SCOPE();
    const Clock::time_point start = Clock::now();
    std::optional<std::optional<double>> gpuMs; ///< Vide tant qu'aucun modèle n'est animé.
    SkinningCost& cost = state.cost;
    for (auto& [id, model] : models)
    {
        if (!model.animation)
        {
            continue;
        }
        if (!gpuMs)
        {
            gpuMs = render::beginGpuTimer(device, commandList, state.timer);
        }
        const animation::Pose before = model.pose;
        // Le mouvement ne compte que pour un animateur : un clip seul joue en boucle.
        const animation::CharacterMotion motion =
            model.animator ? motionOf(id, model) : animation::CharacterMotion{};
        poseModel(model, *model.animation, motion, seconds);
        if (!before.joints.empty() && seconds > model.lastSeconds)
        {
            cost.maxJointSpeed =
                std::max(cost.maxJointSpeed,
                         maxJointSpeedOf(before, model.pose,
                                         static_cast<float>(seconds) - model.lastSeconds));
        }
        model.lastSeconds = static_cast<float>(seconds);
        animation::skinningMatrices(*model.animation, model.pose, model.skinMatrices);
        for (const std::vector<ModelPrimitiveGpu>& mesh : model.meshes)
        {
            for (const ModelPrimitiveGpu& primitive : mesh)
            {
                if (primitive.skin)
                {
                    render::skinMesh(commandList, skinning, *primitive.skin, model.skinMatrices);
                }
            }
        }
    }
    if (!gpuMs)
    {
        return;
    }
    render::endGpuTimer(commandList, state.timer);
    cost.cpuMs += std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    ++cost.frames;
    if (*gpuMs)
    {
        cost.gpuMs += **gpuMs;
        ++cost.gpuSamples;
    }
}

} // namespace levain::app
