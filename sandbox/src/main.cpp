#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <expected>
#include <filesystem>
#include <format>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <print>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif
#include <flecs.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "shader_reload.hpp"

#include "levain/animation/animation_set.hpp"
#include "levain/animation/animator.hpp"
#include "levain/animation/pose.hpp"
#include "levain/assets/asset_ref.hpp"
#include "levain/assets/gltf.hpp"
#include "levain/assets/image.hpp"
#include "levain/assets/registry.hpp"
#include "levain/core/assert.hpp"
#include "levain/core/frame_time.hpp"
#include "levain/core/log.hpp"
#include "levain/core/profile.hpp"
#include "levain/core/version.hpp"
#include "levain/gpu/device.hpp"
#include "levain/input/bindings.hpp"
#include "levain/input/state.hpp"
#include "levain/platform/window.hpp"
#include "levain/render/camera.hpp"
#include "levain/render/culling.hpp"
#include "levain/render/gpu_timer.hpp"
#include "levain/render/light_clusters.hpp"
#include "levain/render/mesh.hpp"
#include "levain/render/mesh_pass.hpp"
#include "levain/render/readback.hpp"
#include "levain/render/renderer.hpp"
#include "levain/render/shadows.hpp"
#include "levain/render/skinning.hpp"
#include "levain/render/sky.hpp"
#include "levain/render/texture.hpp"
#include "levain/render/tonemap.hpp"
#include "levain/scene/camera_control.hpp"
#include "levain/scene/components.hpp"
#include "levain/scene/fixed_step.hpp"
#include "levain/scene/scene.hpp"
#include "levain/scene/transform.hpp"

namespace
{

using Clock = std::chrono::steady_clock;

/// Durée sur laquelle le frame time du titre est résumé.
constexpr double FrameTimePeriodSeconds = 1.0;

/// Validation en Debug seulement (règle n°4) : elle coûte cher, et c'est là qu'on développe.
constexpr bool EnableValidation = LEVAIN_ASSERTIONS_ENABLED != 0;

struct LoopState
{
    bool isRunning = true;
    bool isVisible = true;
};

void applyWindowEvent(LoopState& state, const levain::platform::WindowEvent& event)
{
    using levain::platform::WindowEventType;

    switch (event.type)
    {
    case WindowEventType::CloseRequested:
        state.isRunning = false;
        break;
    // Les deux arrivent en double : sous Wayland, SDL renvoie EXPOSED après chaque
    // redimensionnement. On ne journalise donc que les changements d'état.
    case WindowEventType::Hidden:
        if (state.isVisible)
        {
            levain::core::log("sandbox", levain::core::LogLevel::Info, "masquée : boucle en pause");
        }
        state.isVisible = false;
        break;
    case WindowEventType::Shown:
        if (!state.isVisible)
        {
            levain::core::log("sandbox", levain::core::LogLevel::Info, "visible : boucle relancée");
        }
        state.isVisible = true;
        break;
    case WindowEventType::Resized:
        // M1.2 : c'est ici que la swapchain sera recréée à la nouvelle taille.
        levain::core::log("sandbox", levain::core::LogLevel::Info, "redimensionnée : {} × {} px",
                          event.pixelSize.width, event.pixelSize.height);
        break;
    }
}

double secondsBetween(Clock::time_point start, Clock::time_point end)
{
    return std::chrono::duration<double>(end - start).count();
}

/// Temps GPU moyen sur une période : somme et nombre des mesures reçues.
using levain::render::averageOf;
using levain::render::GpuTimeAverage;

/// Les dessins d'une passe depuis le début de la boucle : soumis au GPU, écartés par le frustum
/// culling (#132), et les triangles soumis, instances comprises (#133).
struct DrawCount
{
    std::uint64_t drawn = 0;
    std::uint64_t culled = 0;
    std::uint64_t triangles = 0;
};

std::string describeFrameTimes(const levain::core::FrameTimeSummary& summary, double gpuMs)
{
    // Tirets ASCII : setWindowTitle refuse le reste (voir window.hpp). averageMs n'est jamais
    // nul, un résumé couvre au moins FrameTimePeriodSeconds.
    return std::format(
        "Levain - {:.3f} ms (min {:.3f}, max {:.3f}) - {:.0f} images/s - GPU {:.3f} ms",
        summary.averageMs, summary.minMs, summary.maxMs, 1000.0 / summary.averageMs, gpuMs);
}

/// Ce que le rendu dessine comme cube : un tag, vide, posé sur les entités de la grille. Demander
/// plutôt « les enfants de grid » coûterait 212 µs par frame au lieu de 8 : une requête
/// `(ChildOf, parent)` combinée à un composant ne se résout pas table par table quand la hiérarchie
/// est rangée dans `flecs::Parent` (manuel des hiérarchies de flecs, « Query performance »).
struct Cube
{
};

/// Le critère de M2.1 : 10 000 cubes instanciés, en une grille de 100 × 100.
constexpr int GridSide = 100;
constexpr float GridSpacing = 1.5f;

/// Le sol : 1000 unités de côté, pour qu'il file jusqu'à l'horizon, et une case du damier par unité
/// (la texture a 8 cases de côté).
constexpr float GroundSize = 1000.0f;
constexpr float GroundTextureRepeat = GroundSize / 8.0f;

/// Une texture sur le GPU : sa référence (ADR-0020), et vrai si ce sont des données
/// (rugosité-métal, normal map) et non une couleur. La même image servirait aux deux en deux
/// textures : le format sRGB ou UNORM se choisit à la création.
using TextureKey = std::pair<levain::assets::AssetRef, bool>;

/// Une primitive d'un modèle importé, prête à dessiner.
struct ModelPrimitiveGpu
{
    levain::render::Mesh mesh;
    std::optional<std::uint32_t> material; ///< Indice dans `ModelGpu::materials`.
    /// Un mesh skinné (ADR-0022) : `mesh` est alors son mesh déformé, que le compute réécrit à
    /// chaque image.
    std::optional<levain::render::SkinnedMesh> skin;
};

/// Un modèle glTF sur le GPU. Les meshes sont dans l'ordre de `Model::meshes` : c'est ce qui donne
/// un sens au sous-indice d'un `MeshRef` (ADR-0019).
struct ModelGpu
{
    std::vector<std::vector<ModelPrimitiveGpu>> meshes;
    /// Une par texture distincte (`TextureKey`) : une texture partagée par deux matériaux n'est
    /// chargée qu'une fois.
    std::map<TextureKey, nvrhi::TextureHandle> textures;
    /// Pour les textures qu'un matériau n'a pas, que ses facteurs règlent seuls.
    levain::render::MaterialDefaults defaults;
    /// Les octets des textures en mémoire vidéo au chargement (critère de #92). Un hot-reload
    /// (ADR-0021) ne le met pas à jour.
    std::size_t textureBytes = 0;
    std::vector<nvrhi::BindingSetHandle> materials;
    /// Pour un modèle skinné : son squelette et ses clips, le clip joué, et la pose et les
    /// matrices du skinning de la dernière image, gardées pour ne pas réallouer.
    std::optional<levain::animation::AnimationSet> animation;
    std::size_t clip = 0;
    /// Avec `--locomotion` : l'animateur, qui remplace le clip unique (#118).
    std::optional<levain::animation::Animator> animator;
    levain::animation::AnimatorClips animatorClips;
    float lastSeconds = 0.0f; ///< Le temps de l'image précédente : l'animateur et sa mesure.
    levain::animation::Pose pose;
    std::vector<glm::mat4> skinMatrices;
};

/// Le coût du skinning (critère de #117), en moyenne sur la boucle : l'échantillonnage et les
/// matrices côté CPU, le compute côté GPU.
struct SkinningCost
{
    double cpuMs = 0.0;
    std::size_t frames = 0;
    double gpuMs = 0.0;
    std::size_t gpuSamples = 0;
    /// La plus grande vitesse d'un os d'une image à l'autre, en unités du modèle par seconde. Un
    /// saut de pose (critère de M4.5) la ferait bondir au-dessus de celle du clip le plus rapide.
    float maxJointSpeed = 0.0f;
};

/// La plus grande vitesse d'un os entre deux poses séparées de `seconds`.
float maxJointSpeedOf(const levain::animation::Pose& before, const levain::animation::Pose& after,
                      float seconds)
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

/// Ce que dessine le sandbox en M2.2 : une grille de cubes texturés qui tournent, sur un sol qui
/// file jusqu'à l'horizon, où se voit le filtrage anisotrope.
struct DemoScene
{
    flecs::world world; ///< Les cubes, une entité chacun (M3.1), enfants de « grid » (M3.2).
    levain::scene::FixedStep fixedStep; ///< L'horloge de la simulation, 60 Hz (M3.3).
    flecs::query<const levain::scene::WorldTransform> cubes;
    std::vector<glm::vec3>
        cubePositions; ///< Relevées à chaque frame, gardées pour ne pas réallouer.
    /// L'image et l'ordre de ses passes (ADR-0025) ; la démo s'inscrit dans ses étapes
    /// (`addDemoStages`).
    levain::render::Renderer renderer;
    levain::render::SkinningPass skinning;
    levain::render::Sun sun; ///< Celui du ciel, ou celui de la démo sans HDRI.
    levain::render::TonemapSettings tonemapSettings;
    levain::render::Mesh cube;
    levain::render::Instances grid;
    levain::render::Mesh ground;
    levain::render::Instances groundInstance; ///< Une seule, sous les cubes.
    /// Les modèles glTF (M4.1), par GUID (ADR-0019) : le registre des chemins, les modèles en
    /// mémoire, et leur version GPU, déchargée avec eux quand plus aucune entité ne les utilise.
    levain::assets::AssetRegistry registry;
    levain::assets::ModelCache modelCache;
    std::map<levain::assets::AssetId, ModelGpu> models;
    levain::render::Instances modelInstance; ///< Une seule, à l'origine : la matrice monde place.
    flecs::query<const levain::assets::MeshRef, const levain::scene::WorldTransform> modelParts;
    nvrhi::TextureHandle checker;
    nvrhi::SamplerHandle sampler; ///< Gardé pour refaire les binding sets au hot-reload.
    nvrhi::BindingSetHandle material;
    flecs::entity cameraEntity; ///< Transform + FpsController : la caméra libre (M3.4).
    levain::render::Camera camera;
    /// La grille de cubes, le sol et les lumières de couleur ; absents de `--view khronos`.
    bool demoProps = true;
    levain::render::GpuTimer gpuTimer;
    levain::render::GpuTimer skinningTimer; ///< Le seul skinning : le critère de coût de #117.
    SkinningCost skinningCost;
    DrawCount cameraCulling;
    DrawCount shadowCulling; ///< Les quatre cascades ensemble.
};

/// Les cubes de la grille : une entité chacun, nommée par sa colonne et sa rangée
/// (« grid.cube_50_50 » au centre) pour la retrouver dans l'explorer, et **enfant** d'une entité
/// « grid » qu'on peut déplacer d'un bloc (M3.2). Leur position est donc dans le repère de la
/// grille, et leur vitesse, nulle au départ, est celle que l'explorer peut changer.
///
/// La hiérarchie passe par le composant `flecs::Parent` et jamais par `child_of` : une entité ne
/// peut pas avoir les deux, et le système des matrices monde ne voit que le premier (ADR-0015).
void spawnCubeGrid(flecs::world& world)
{
    const flecs::entity grid = world.entity("grid").set(levain::scene::Transform{});
    const float half = static_cast<float>(GridSide - 1) * GridSpacing / 2.0f;
    for (int z = 0; z < GridSide; ++z)
    {
        for (int x = 0; x < GridSide; ++x)
        {
            const glm::vec3 position{static_cast<float>(x) * GridSpacing - half, 0.0f,
                                     static_cast<float>(z) * GridSpacing - half};
            world.entity(flecs::Parent{grid}, std::format("cube_{}_{}", x, z).c_str())
                .set(levain::scene::Transform{.position = position})
                .set(levain::scene::Velocity{})
                .add<Cube>();
        }
    }
}

/// La caméra du rendu, relue sur l'entité : sa **matrice monde** porte la position et le regard
/// déjà interpolés entre deux pas de simulation (ADR-0016). Lire le `Transform` ferait saccader le
/// regard dès que le rendu va plus vite que la simulation.
void updateRenderCamera(levain::render::Camera& camera, const flecs::entity& cameraEntity)
{
    const glm::mat4& world = cameraEntity.get<levain::scene::WorldTransform>().matrix;
    camera.position = glm::vec3(world[3]);
    camera.target = camera.position + glm::vec3(glm::mat3(world) * glm::vec3{0.0f, 0.0f, -1.0f});
}

/// Les intentions du joueur, lues dans les axes et les actions du fichier de liaisons. Le sandbox
/// est le seul à connaître les deux côtés : `engine/scene` ignore l'existence de l'input, et
/// `engine/input` ne sait rien des caméras (SPECS §7).
struct CameraActions
{
    int moveRight = 0;
    int moveForward = 0;
    int moveUp = 0;
    int lookRight = 0;
    int lookUp = 0;
    int lookEnable = 0;
    int sprint = 0;
};

/// Les indices des actions et des axes dont la caméra a besoin, résolus **une fois**. Un nom absent
/// du fichier est une erreur de chargement : sans ça, la caméra ne répondrait jamais à cette
/// commande, sans que rien ne le dise (règle n°7).
[[nodiscard]] levain::core::Result<CameraActions>
cameraActionsOf(const levain::input::Bindings& bindings)
{
    CameraActions actions;
    const std::array<std::pair<const char*, int*>, 5> axes{{{"move_right", &actions.moveRight},
                                                            {"move_forward", &actions.moveForward},
                                                            {"move_up", &actions.moveUp},
                                                            {"look_right", &actions.lookRight},
                                                            {"look_up", &actions.lookUp}}};
    for (const auto& [name, index] : axes)
    {
        const auto found = levain::input::axisIndex(bindings, name);
        if (!found)
        {
            return levain::core::makeError(levain::core::ErrorCode::InvalidData,
                                           std::format("axe « {} » absent de input.cfg", name));
        }
        *index = found.value_or(-1);
    }
    const std::array<std::pair<const char*, int*>, 2> buttons{
        {{"look_enable", &actions.lookEnable}, {"sprint", &actions.sprint}}};
    for (const auto& [name, index] : buttons)
    {
        const auto found = levain::input::actionIndex(bindings, name);
        if (!found)
        {
            return levain::core::makeError(levain::core::ErrorCode::InvalidData,
                                           std::format("action « {} » absente de input.cfg", name));
        }
        *index = found.value_or(-1);
    }
    return actions;
}

levain::scene::FpsInput fpsInputFrom(const levain::input::InputState& input,
                                     const CameraActions& actions)
{
    // Le regard à la souris ne compte que si son bouton est tenu ; à la manette, le stick suffit.
    const bool looking = levain::input::actionHeld(input, actions.lookEnable);
    const float lookRight = levain::input::axisValue(input, actions.lookRight);
    const float lookUp = levain::input::axisValue(input, actions.lookUp);
    return {.move = {levain::input::axisValue(input, actions.moveRight),
                     levain::input::axisValue(input, actions.moveForward)},
            .up = levain::input::axisValue(input, actions.moveUp),
            .look = looking ? glm::vec2{lookRight, lookUp} : glm::vec2{0.0f},
            .sprint = levain::input::actionHeld(input, actions.sprint)};
}

/// Les positions des cubes **dans le monde**, dans `positions`, que le rendu envoie ensuite au GPU.
/// La glu entre scene et render, qui ne se connaissent pas (SPECS §7). Lire `WorldTransform` et non
/// `Transform` : c'est ce qui fait suivre les cubes quand la grille bouge.
void gatherCubePositions(const flecs::query<const levain::scene::WorldTransform>& cubes,
                         std::vector<glm::vec3>& positions)
{
    positions.clear();
    cubes.each([&positions](const levain::scene::WorldTransform& transform)
               { positions.push_back(levain::scene::worldPosition(transform)); });
}

#ifdef LEVAIN_ENABLE_EXPLORER
/// L'explorer web de flecs (https://www.flecs.dev/explorer) lit et modifie le monde par l'addon
/// REST, sur le port 27750. Debug seulement, et **sur la boucle locale** : par défaut, flecs écoute
/// sur toutes les interfaces, et son API distante sait aussi supprimer des entités et exécuter des
/// scripts (https://www.flecs.dev/flecs/FlecsRemoteApi.html).
void enableExplorerOnLoopback(flecs::world& world)
{
    world.import<flecs::stats>(); // les statistiques de l'onglet « Stats » de l'explorer
    // ipaddr doit venir de l'allocateur de flecs : EcsRest en prend la propriété et le libère à la
    // destruction du monde (src/addons/rest.c, ECS_DTOR(EcsRest)). Une chaîne statique finissait en
    // « double free » à la sortie du sandbox.
    world.set<flecs::Rest>(
        {.port = ECS_REST_DEFAULT_PORT, .ipaddr = ecs_os_strdup("127.0.0.1"), .impl = nullptr});
    levain::core::log("sandbox", levain::core::LogLevel::Info,
                      "explorer : https://www.flecs.dev/explorer (REST sur 127.0.0.1:{})",
                      ECS_REST_DEFAULT_PORT);
}
#endif

/// Les niveaux de mip dans le format qu'attend render. Ils pointent dans `mips`, qui doit leur
/// survivre jusqu'à l'envoi.
std::vector<levain::render::TextureLevel>
textureLevelsOf(const std::vector<levain::assets::Image>& mips)
{
    std::vector<levain::render::TextureLevel> levels;
    levels.reserve(mips.size());
    for (const levain::assets::Image& mip : mips)
    {
        levels.push_back({.width = mip.width,
                          .height = mip.height,
                          .bytes = std::as_bytes(std::span{mip.rgba})});
    }
    return levels;
}

/// Les niveaux d'une texture chargée par `assets`, cuite ou non, pour `render`.
std::vector<levain::render::TextureLevel> textureLevelsOf(const levain::assets::TextureData& data)
{
    std::vector<levain::render::TextureLevel> levels;
    levels.reserve(data.mips.size());
    for (const levain::assets::TextureMip& mip : data.mips)
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
nvrhi::Format nvrhiFormatOf(levain::assets::TextureFormat format, bool linear)
{
    if (format == levain::assets::TextureFormat::Bc7Srgb)
    {
        return linear ? nvrhi::Format::BC7_UNORM : nvrhi::Format::BC7_UNORM_SRGB;
    }
    return linear ? nvrhi::Format::RGBA8_UNORM : nvrhi::Format::SRGBA8_UNORM;
}

/// Le format où charger les textures cuites : le BC7 si le GPU l'échantillonne (tous les GPU de
/// PC), le RGBA8 sinon.
levain::assets::TextureFormat textureTargetOf(nvrhi::IDevice& device)
{
    return levain::render::supportsSampledFormat(device, nvrhi::Format::BC7_UNORM_SRGB)
               ? levain::assets::TextureFormat::Bc7Srgb
               : levain::assets::TextureFormat::Rgba8Srgb;
}

struct UploadedTexture
{
    nvrhi::TextureHandle handle;
    std::size_t bytes = 0; ///< En mémoire vidéo, mips comprises.
};

/// Charge la texture `key`, cuite si possible (ADR-0020) : le cache BC7 se copie tel quel, sinon
/// la source. Son envoi est enregistré dans `commandList`.
levain::core::Result<UploadedTexture> uploadTexture(nvrhi::IDevice& device,
                                                    nvrhi::ICommandList& commandList,
                                                    const levain::assets::AssetRegistry& registry,
                                                    const levain::assets::ModelCache& models,
                                                    TextureKey key,
                                                    levain::assets::TextureFormat target)
{
    const auto [ref, linear] = key;
    auto data = levain::assets::loadTextureData(registry, models, ref, target);
    if (!data)
    {
        return std::unexpected(data.error());
    }
    // Le nom du fichier source, pour retrouver la texture dans une capture RenderDoc
    // (tools/renderdoc-mips.py). NVRHI le recopie.
    const std::string name =
        levain::assets::pathOf(registry, ref.asset).value_or("glTF").filename().string();
    UploadedTexture texture{
        .handle = levain::render::createTexture(device, commandList, textureLevelsOf(*data),
                                                name.c_str(), nvrhiFormatOf(data->format, linear)),
        .bytes = 0};
    for (const levain::assets::TextureMip& mip : data->mips)
    {
        texture.bytes += mip.bytes.size();
    }
    return texture;
}

/// Envoie meshes et textures, et enregistre l'envoi dans `commandList`. La couleur d'un sommet est
/// la couleur de base de son matériau ; sans matériau, c'est sa normale ramenée dans [0, 1], qui
/// rend les formes lisibles sans éclairage (M5.1).
levain::core::Result<ModelGpu> uploadModel(nvrhi::IDevice& device, nvrhi::ICommandList& commandList,
                                           const levain::assets::Model& model,
                                           const levain::assets::AssetRegistry& registry,
                                           const levain::assets::ModelCache& models,
                                           const levain::render::SkinningPass& skinning,
                                           std::uint32_t skinJointCount)
{
    const levain::assets::TextureFormat target = textureTargetOf(device);
    ModelGpu gpu;
    std::vector<levain::render::MeshVertex> vertices;
    for (const levain::assets::ModelMesh& mesh : model.meshes)
    {
        std::vector<ModelPrimitiveGpu>& primitives = gpu.meshes.emplace_back();
        for (const levain::assets::MeshPrimitive& primitive : mesh.primitives)
        {
            // Le matériau porte sa couleur de base (MaterialConstants) : le sommet reste blanc.
            const std::optional<glm::vec3> baseColor =
                primitive.material ? std::optional{glm::vec3{1.0f}} : std::nullopt;
            vertices.clear();
            for (const levain::assets::ModelVertex& vertex : primitive.vertices)
            {
                vertices.push_back({.position = vertex.position,
                                    .normal = vertex.normal,
                                    .tangent = vertex.tangent,
                                    .color = baseColor.value_or(vertex.normal * 0.5f + 0.5f),
                                    .uv = vertex.uv});
            }
            if (primitive.joints.empty())
            {
                primitives.push_back({.mesh = levain::render::createMesh(
                                          device, commandList, vertices, primitive.indices),
                                      .material = primitive.material,
                                      .skin = std::nullopt});
                continue;
            }
            // Skinné : les mêmes sommets, avec leurs os et leurs poids, que le compute déformera.
            std::vector<levain::render::SkinnedVertex> skinned;
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
            levain::render::SkinnedMesh skin = levain::render::createSkinnedMesh(
                device, commandList, skinning, skinned, primitive.indices, skinJointCount);
            primitives.push_back(
                {.mesh = skin.skinned, .material = primitive.material, .skin = std::move(skin)});
        }
    }
    for (const levain::assets::ModelMaterial& material : model.materials)
    {
        const std::array<std::pair<std::optional<levain::assets::AssetRef>, bool>, 3> slots{{
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
    gpu.defaults = levain::render::createMaterialDefaults(device, commandList);
    return gpu;
}

/// Abandonne un envoi en cours : `commandList`, ouverte, est fermée et soumise quand même. Juste
/// détruite, elle fuirait jusqu'à `vkDestroyDevice` avec tout ce qu'elle a enregistré : `open()`
/// l'inscrit dans les ressources de son propre command buffer (NVRHI, vulkan-commandlist.cpp), un
/// cycle que seule la file rompt, quand elle retire le command buffer soumis.
void submitAbandonedUpload(nvrhi::IDevice& device, nvrhi::ICommandList& commandList)
{
    commandList.close();
    device.executeCommandList(&commandList);
}

/// Un binding set par matériau du modèle : ses facteurs, et ses textures ou celles par défaut. Les
/// constantes s'envoient par `commandList`.
void bindModelMaterials(nvrhi::IDevice& device, nvrhi::ICommandList& commandList,
                        const levain::render::MeshPass& pass, nvrhi::ISampler& sampler,
                        const levain::assets::Model& model, ModelGpu& gpu)
{
    const auto textureOf = [&gpu](const std::optional<levain::assets::AssetRef>& ref,
                                  bool linear) -> nvrhi::ITexture*
    { return ref ? gpu.textures.at({*ref, linear}).Get() : nullptr; };
    for (const levain::assets::ModelMaterial& material : model.materials)
    {
        const levain::render::MaterialConstants constants{
            .baseColorFactor = material.baseColorFactor,
            .metallicFactor = material.metallicFactor,
            .roughnessFactor = material.roughnessFactor,
            .normalScale = material.normalScale,
            .padding = 0.0f,
        };
        const levain::render::MaterialTextures textures{
            .baseColor = textureOf(material.baseColorTexture, false),
            .metallicRoughness = textureOf(material.metallicRoughnessTexture, true),
            .normal = textureOf(material.normalTexture, true),
        };
        gpu.materials.push_back(levain::render::createMaterialBindings(
            device, commandList, pass, constants,
            levain::render::withDefaults(textures, gpu.defaults), sampler));
    }
}

/// Où poser le modèle de `--model` : devant la caméra de départ, sur le sol, et tourné de trois
/// quarts pour montrer deux faces. `scale` vaut 2 par défaut, pour qu'un objet de quelques mètres
/// se voie de loin ; `--model-scale` le change pour un modèle d'une autre unité (Fox mesure une
/// centaine d'unités).
levain::scene::Transform modelPlacement(float scale)
{
    return {.position = {96.0f, -1.0f, 104.0f},
            .rotation = glm::angleAxis(glm::radians(-50.0f), glm::vec3{0.0f, 1.0f, 0.0f}),
            .scale = glm::vec3{scale}};
}

/// Vrai si l'un des meshes du modèle est skinné : il faut alors son squelette (ADR-0022).
bool isSkinned(const levain::assets::Model& model)
{
    return std::ranges::any_of(model.meshes,
                               [](const levain::assets::ModelMesh& mesh)
                               {
                                   return std::ranges::any_of(
                                       mesh.primitives, [](const levain::assets::MeshPrimitive& p)
                                       { return !p.joints.empty(); });
                               });
}

/// L'indice du clip `name`, ou du premier si aucun n'est demandé. Un nom inconnu est un échec, qui
/// liste les clips du modèle.
levain::core::Result<std::size_t> clipIndexOf(const levain::animation::AnimationSet& set,
                                              const std::optional<std::string>& name)
{
    if (set.clips.empty())
    {
        return levain::core::makeError(levain::core::ErrorCode::InvalidData,
                                       "modèle skinné sans clip");
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
    return levain::core::makeError(
        levain::core::ErrorCode::InvalidData,
        std::format("clip « {} » inconnu ; clips : {}", *name, available));
}

/// La vitesse du renard de démonstration (#118) : du repos à la course, puis retour, en 8 s. Les
/// vitesses de la locomotion valent 1 (marche) et 3 (course), dans une unité arbitraire : celles
/// de Fox ne sont pas connues.
constexpr float DemoWalkSpeed = 1.0f;
constexpr float DemoRunSpeed = 3.0f;

float demoSpeedAt(double seconds)
{
    constexpr double Period = 8.0;
    return DemoRunSpeed *
           static_cast<float>(0.5 - 0.5 * std::cos(2.0 * glm::pi<double>() * seconds / Period));
}

/// Les clips de `--locomotion` (« repos,marche,course », par leurs noms), pour l'animateur. Les
/// autres états n'ont pas de clip : ils gardent la locomotion.
levain::core::Result<levain::animation::AnimatorClips>
locomotionClipsOf(const levain::animation::AnimationSet& set, std::string_view names)
{
    std::array<std::size_t, 3> indices{};
    std::size_t start = 0;
    for (std::size_t& index : indices)
    {
        const std::size_t comma = names.find(',', start);
        const std::string name{names.substr(start, comma - start)};
        if (name.empty())
        {
            return levain::core::makeError(levain::core::ErrorCode::InvalidData,
                                           "--locomotion attend trois clips : repos,marche,course");
        }
        auto clip = clipIndexOf(set, name);
        if (!clip)
        {
            return std::unexpected(clip.error());
        }
        index = *clip;
        start = comma == std::string_view::npos ? names.size() : comma + 1;
    }
    return levain::animation::AnimatorClips{.ground = {.idle = indices[0],
                                                       .walk = indices[1],
                                                       .run = indices[2],
                                                       .walkSpeed = DemoWalkSpeed,
                                                       .runSpeed = DemoRunSpeed},
                                            .jump = std::nullopt,
                                            .fall = std::nullopt,
                                            .swim = std::nullopt,
                                            .glide = std::nullopt};
}

/// Ce que le scan des assets a changé sur le disque (ADR-0019) : les .meta créés et rattachés sont
/// à versionner, les orphelins à regarder.
void logScanReport(const levain::assets::ScanReport& report)
{
    for (const auto& created : report.created)
    {
        levain::core::log("assets", levain::core::LogLevel::Info, "nouveau .meta : {}",
                          created.string());
    }
    for (const auto& reattached : report.reattached)
    {
        levain::core::log("assets", levain::core::LogLevel::Info,
                          "renommé hors du moteur, GUID conservé : {}", reattached.string());
    }
    for (const auto& orphan : report.orphans)
    {
        levain::core::log("assets", levain::core::LogLevel::Warning,
                          ".meta orphelin, asset disparu : {}", orphan.string());
    }
}

/// Le soleil de la démo : haut, de biais, légèrement chaud.
constexpr levain::render::Sun DemoSun{
    .direction = {-0.7f, 0.45f, 0.5f}, .color = {1.0f, 0.95f, 0.85f}, .intensity = 3.0f};

/// `--sky none` : ni HDRI, ni ambiance.
constexpr std::string_view NoSky = "none";

/// Le ciel qui éclaire la scène, et son soleil.
struct Sky
{
    levain::render::Environment environment;
    levain::render::Sun sun;
};

/// Tourne le ciel de `degrees` autour de la verticale : chaque ligne de l'image équirectangulaire
/// glisse d'autant de colonnes, la longitude faisant le tour de l'image.
void turnSkyAroundUp(levain::assets::HdrImage& image, float degrees)
{
    const auto columns = static_cast<std::ptrdiff_t>(
        std::lround(static_cast<double>(image.width) * degrees / 360.0));
    const auto rowLength = static_cast<std::ptrdiff_t>(image.width) * 4;
    for (auto row = image.rgba.begin(); row != image.rgba.end(); row += rowLength)
    {
        std::rotate(row, row + rowLength - (columns * 4), row + rowLength);
    }
}

/// Le ciel de `--sky`, ou sans HDRI un ciel uniforme et sombre, l'ambiance d'avant l'IBL, sous le
/// soleil de la démo. Le soleil de l'HDRI en est retiré, pour devenir celui de la scène, qui jette
/// les ombres. Le temps de calcul est donné : il se paie à chaque chargement.
///
/// `khronosView` : le ciel tel que l'éclaire le glTF Sample Viewer. Son soleil reste dans l'IBL,
/// sans lumière directionnelle, et le ciel est tourné de 90° (sa rotation par défaut, « +Z »).
levain::core::Result<Sky> loadSky(nvrhi::IDevice& device,
                                  const std::optional<std::filesystem::path>& skyPath,
                                  bool khronosView)
{
    if (!skyPath || skyPath->native() == NoSky)
    {
        // `--sky none` : aucune lumière du ciel, pas même l'ambiance (#125, le viewer sans IBL).
        auto uniform =
            levain::render::createUniformEnvironment(device, glm::vec3{skyPath ? 0.0f : 0.1f});
        if (!uniform)
        {
            return std::unexpected(uniform.error());
        }
        return Sky{.environment = std::move(*uniform), .sun = DemoSun};
    }
    auto image = levain::assets::loadHdrImage(*skyPath);
    if (!image)
    {
        return std::unexpected(image.error());
    }
    std::optional<levain::render::Sun> sun;
    if (khronosView)
    {
        turnSkyAroundUp(*image, 90.0f);
        sun = levain::render::Sun{.direction = {0.0f, 1.0f, 0.0f}, .color{1.0f}, .intensity = 0.0f};
    }
    else
    {
        sun = levain::render::extractSun(image->width, image->height, image->rgba);
    }
    if (khronosView)
    {
        levain::core::log("sandbox", levain::core::LogLevel::Info,
                          "vue du glTF Sample Viewer : le ciel seul éclaire, tourné de 90°");
    }
    else if (sun)
    {
        levain::core::log("sandbox", levain::core::LogLevel::Info,
                          "soleil de l'HDRI : direction ({:.2f}, {:.2f}, {:.2f}), intensité {:.2f}",
                          sun->direction.x, sun->direction.y, sun->direction.z, sun->intensity);
    }
    else
    {
        levain::core::log("sandbox", levain::core::LogLevel::Warning,
                          "pas de soleil dans l'HDRI : le soleil de la démo éclaire la scène");
    }
    const auto start = std::chrono::steady_clock::now();
    auto environment = levain::render::createEnvironment(
        device, {.width = image->width, .height = image->height, .rgba = image->rgba});
    device.waitForIdle();
    const std::chrono::duration<double, std::milli> elapsed =
        std::chrono::steady_clock::now() - start;
#ifdef __EMSCRIPTEN__
    // Le navigateur ne laisse pas attendre le GPU (waitForIdle n'y fait rien) : le temps ne compte
    // que l'enregistrement des passes, pas leur calcul.
    constexpr std::string_view Measured = "enregistré";
#else
    constexpr std::string_view Measured = "calculé";
#endif
    levain::core::log("sandbox", levain::core::LogLevel::Info,
                      "ciel : {} ({} × {}), environnement {} en {:.1f} ms",
                      skyPath->filename().string(), image->width, image->height, Measured,
                      elapsed.count());
    if (!environment)
    {
        return std::unexpected(environment.error());
    }
    return Sky{.environment = std::move(*environment), .sun = sun.value_or(DemoSun)};
}

/// Le champ vertical de la caméra du glTF Sample Viewer (PerspectiveCamera.yfov).
constexpr float KhronosViewerFovDegrees = 45.0f;

/// Crée la passe des meshes et envoie au GPU le cube, la grille, le sol, la texture du damier, le
/// modèle de `--model` et le ciel de `--sky`.
levain::core::Result<DemoScene>
createDemoScene(levain::gpu::GpuDevice& gpu, const levain::render::SamplerSettings& sampler,
                const std::optional<std::filesystem::path>& modelPath,
                const std::optional<std::string>& clipName,
                const std::optional<std::string>& locomotion, float modelScale,
                const std::optional<std::filesystem::path>& skyPath, bool khronosView,
                std::optional<glm::vec3> cameraPosition, std::optional<glm::vec3> sunDirection)
{
    // Le modèle de `--model`, par son GUID : le dossier qui le contient est scanné (ADR-0019), ce
    // qui lui donne un .meta s'il n'en avait pas.
    levain::assets::AssetRegistry registry;
    levain::assets::ModelCache modelCache;
    const levain::assets::Model* model = nullptr;
    levain::assets::AssetId modelId;
    // Les trois temps du chargement (le critère de M4.3) : le scan des racines, qui hache tous les
    // assets ; la lecture du modèle, cuit ou source ; ses textures, leurs mips et l'envoi au GPU.
    const Clock::time_point loadStart = Clock::now();
    Clock::time_point modelStart = loadStart;
    Clock::time_point modelEnd = loadStart;
    // La racine d'assets du sandbox, versionnée : ses .meta se commitent avec les fichiers, et la
    // CI refuse un asset qui n'a pas le sien (tests/check_asset_metas.cmake).
    auto dataReport = levain::assets::scanAssets(LEVAIN_DATA_DIR, registry);
    if (!dataReport)
    {
        return std::unexpected(dataReport.error());
    }
    logScanReport(*dataReport);
    // La seconde racine : les assets de test téléchargés (tools/fetch-assets.sh), s'ils sont là.
    // Les fichiers cuits de chaque racine sont dans son `.cooked/` (ADR-0020), là où levain_cook
    // les écrit.
    const std::filesystem::path testAssets{LEVAIN_TEST_ASSETS_DIR};
    if (std::filesystem::is_directory(testAssets))
    {
        auto report = levain::assets::scanAssets(testAssets, registry);
        if (!report)
        {
            return std::unexpected(report.error());
        }
        logScanReport(*report);
    }
    if (modelPath)
    {
        const auto id = levain::assets::idOf(registry, *modelPath);
        if (!id)
        {
            return levain::core::makeError(
                levain::core::ErrorCode::InvalidData,
                std::format("{} : pas un asset d'une racine connue (data/, assets-cache/)",
                            modelPath->string()));
        }
        modelStart = Clock::now();
        auto loaded = levain::assets::loadModel(modelCache, registry, *id);
        if (!loaded)
        {
            return std::unexpected(loaded.error());
        }
        model = *loaded;
        modelId = *id;
        modelEnd = Clock::now();
    }

    auto image = levain::assets::loadImage(LEVAIN_DATA_DIR "/textures/checker.png");
    if (!image)
    {
        return std::unexpected(image.error());
    }
    const std::vector<levain::assets::Image> mips =
        levain::assets::buildMipChain(std::move(*image));

    // Un modèle skinné anime son squelette (ADR-0022) : la passerelle relit son glTF.
    // ponytail: squelette et clips viennent toujours de la source ; leur cuisson dans .cooked/
    // (ADR-0022) viendra quand leur lecture pèsera au chargement (0,55 ms pour Fox).
    std::optional<levain::animation::AnimationSet> animation;
    std::size_t clip = 0;
    std::optional<levain::animation::AnimatorClips> animatorClips;
    if (model != nullptr && isSkinned(*model))
    {
        auto set = levain::animation::importAnimationSet(
            levain::assets::pathOf(registry, modelId).value_or(*modelPath));
        if (!set)
        {
            return std::unexpected(set.error());
        }
        auto index = clipIndexOf(*set, clipName);
        if (!index)
        {
            return std::unexpected(index.error());
        }
        clip = *index;
        // Les noms de --locomotion se vérifient ici, avant tout travail GPU : un échec plus tard
        // aurait envoyé meshes et textures pour rien.
        if (locomotion)
        {
            auto clips = locomotionClipsOf(*set, *locomotion);
            if (!clips)
            {
                return std::unexpected(clips.error());
            }
            animatorClips = *clips;
            levain::core::log("sandbox", levain::core::LogLevel::Info,
                              "modèle skinné : {} os, locomotion « {} », vitesse de 0 à {} et "
                              "retour en 8 s",
                              set->jointNames.size(), *locomotion, DemoRunSpeed);
        }
        else
        {
            levain::core::log("sandbox", levain::core::LogLevel::Info,
                              "modèle skinné : {} os, clip « {} » ({:.2f} s) joué en boucle",
                              set->jointNames.size(), set->clips[clip].name,
                              set->clips[clip].durationSeconds);
        }
        animation = std::move(*set);
    }

    auto skinning = levain::render::createSkinningPass(*gpu.nvrhi);
    if (!skinning)
    {
        return std::unexpected(skinning.error());
    }
    auto sky = loadSky(*gpu.nvrhi, skyPath, khronosView);
    if (!sky)
    {
        return std::unexpected(sky.error());
    }
    // Le ciel en fond, avec une HDRI seulement : sans elle, le fond reste la croûte de levain.
    auto renderer = levain::render::createRenderer(*gpu.nvrhi, levain::gpu::swapchainFormat(gpu),
                                                   std::move(sky->environment),
                                                   skyPath && skyPath->native() != NoSky);
    if (!renderer)
    {
        return std::unexpected(renderer.error());
    }

    flecs::world world;
    world.import<levain::scene::SceneModule>();
    world.import<levain::assets::AssetsModule>();
#ifdef LEVAIN_ENABLE_EXPLORER
    enableExplorerOnLoopback(world);
#endif
    spawnCubeGrid(world);
    if (model != nullptr)
    {
        levain::assets::instantiateModel(world, *model, modelId, "model")
            .set(khronosView ? levain::scene::Transform{} : modelPlacement(modelScale));
    }
    // La caméra est une entité comme les autres : basse, sur le côté de la grille, et visant loin
    // devant. Elle porte son état précédent pour que le rendu l'interpole entre deux pas de
    // simulation (ADR-0016) — sans quoi le regard avancerait par saccades de 16 ms.
    const flecs::entity cameraEntity =
        world.entity("camera")
            .set(levain::scene::Transform{.position = {100.0f, 8.0f, 120.0f}})
            // 10,3° de lacet et 10,1° sous l'horizon : exactement le regard des milestones
            // précédents, qui visait le point {92, 0, 76}.
            .set(levain::scene::FpsController{.yawDegrees = 10.3f, .pitchDegrees = -10.1f})
            .add<levain::scene::PreviousTransform>();
    // Les cubes, et rien d'autre : ni la grille, qui n'est qu'un point d'accroche, ni le sol.
    flecs::query<const levain::scene::WorldTransform> cubes =
        world.query_builder<const levain::scene::WorldTransform>("cubes").with<Cube>().build();
    levain::scene::FixedStep fixedStep;
    if (cameraPosition)
    {
        // Face à −Z : le regard du glTF Sample Viewer à l'ouverture d'un modèle.
        cameraEntity.set(levain::scene::Transform{.position = *cameraPosition})
            .set(levain::scene::FpsController{.yawDegrees = 0.0f, .pitchDegrees = 0.0f});
    }
    levain::scene::advanceWorld(world, fixedStep,
                                0.0f); // les matrices monde, avant le premier envoi
    std::vector<glm::vec3> cubePositions;
    gatherCubePositions(cubes, cubePositions);

    const nvrhi::CommandListHandle upload = gpu.nvrhi->createCommandList();
    upload->open();
    levain::render::Mesh cube = levain::render::createCube(*gpu.nvrhi, *upload);
    levain::render::Instances grid =
        levain::render::createInstances(*gpu.nvrhi, *upload, cubePositions);
    levain::render::Mesh ground =
        levain::render::createPlane(*gpu.nvrhi, *upload, GroundSize, GroundTextureRepeat);
    // Juste sous les cubes, qui tournent sur eux-mêmes : leur demi-diagonale fait 0,87.
    const std::array<glm::vec3, 1> groundOffset{glm::vec3{0.0f, -1.0f, 0.0f}};
    levain::render::Instances groundInstance =
        levain::render::createInstances(*gpu.nvrhi, *upload, groundOffset);
    nvrhi::TextureHandle checker =
        levain::render::createTexture(*gpu.nvrhi, *upload, textureLevelsOf(mips), "checker");
    const Clock::time_point uploadStart = Clock::now();
    std::map<levain::assets::AssetId, ModelGpu> models;
    if (model != nullptr)
    {
        const auto skinJoints =
            static_cast<std::uint32_t>(animation ? animation->skinJoints.size() : 0);
        auto uploaded =
            uploadModel(*gpu.nvrhi, *upload, *model, registry, modelCache, *skinning, skinJoints);
        if (!uploaded)
        {
            submitAbandonedUpload(*gpu.nvrhi, *upload);
            return std::unexpected(uploaded.error());
        }
        if (animatorClips)
        {
            uploaded->animatorClips = *animatorClips;
            uploaded->animator = levain::animation::Animator{};
        }
        uploaded->animation = std::move(animation);
        uploaded->clip = clip;
        models.emplace(modelId, std::move(*uploaded));
    }
    const std::array<glm::vec3, 1> origin{glm::vec3{0.0f}};
    levain::render::Instances modelInstance =
        levain::render::createInstances(*gpu.nvrhi, *upload, origin);
    // Les matériaux avant la fermeture de l'envoi : leurs constantes passent par lui. Le damier des
    // cubes et du sol : non métallique, assez rugueux.
    nvrhi::SamplerHandle samplerHandle = levain::render::createSampler(*gpu.nvrhi, sampler);
    nvrhi::BindingSetHandle material = levain::render::createMaterialBindings(
        *gpu.nvrhi, *upload, renderer->meshPass,
        {.baseColorFactor = glm::vec4{1.0f},
         .metallicFactor = 0.0f,
         .roughnessFactor = 0.8f,
         .normalScale = 1.0f,
         .padding = 0.0f},
        levain::render::withDefaults({.baseColor = checker},
                                     levain::render::createMaterialDefaults(*gpu.nvrhi, *upload)),
        *samplerHandle);
    if (model != nullptr)
    {
        bindModelMaterials(*gpu.nvrhi, *upload, renderer->meshPass, *samplerHandle, *model,
                           models.at(modelId));
    }
    upload->close();
    gpu.nvrhi->executeCommandList(upload);
    if (model != nullptr)
    {
        levain::core::log(
            "sandbox", levain::core::LogLevel::Info,
            "modèle : {} meshes, {} matériaux, {} textures ({:.1f} Mo en mémoire vidéo) ; scan "
            "{:.0f} ms, modèle {:.1f} ms, textures, mips et envoi {:.0f} ms",
            model->meshes.size(), model->materials.size(), models.at(modelId).textures.size(),
            static_cast<double>(models.at(modelId).textureBytes) / (1024.0 * 1024.0),
            secondsBetween(loadStart, modelStart) * 1000.0,
            secondsBetween(modelStart, modelEnd) * 1000.0,
            secondsBetween(uploadStart, Clock::now()) * 1000.0);
    }

    // La grille occupe la gauche de l'image, le sol file jusqu'à l'horizon à droite, de plus en
    // plus de biais : c'est là que le filtrage trilinéaire seul le rend flou. Position et regard
    // sont ceux de l'entité, et le joueur peut les changer.
    const levain::render::Camera camera{
        .position = {},
        .target = {},
        .verticalFovRadians = glm::radians(khronosView ? KhronosViewerFovDegrees : 60.0f),
        .nearPlane = 0.5f,
        .farPlane = 1000.0f};
    // Avant le return : `.world = std::move(world)` vide `world` avant les champs suivants.
    flecs::query<const levain::assets::MeshRef, const levain::scene::WorldTransform> modelParts =
        world.query<const levain::assets::MeshRef, const levain::scene::WorldTransform>();
    return DemoScene{.world = std::move(world),
                     .fixedStep = fixedStep,
                     .cubes = std::move(cubes),
                     .cubePositions = std::move(cubePositions),
                     .renderer = std::move(*renderer),
                     .skinning = std::move(*skinning),
                     .sun = sunDirection ? levain::render::Sun{.direction = *sunDirection,
                                                               .color = glm::vec3{1.0f},
                                                               .intensity = 1.0f}
                                         : sky->sun,
                     .tonemapSettings = {},
                     .cube = std::move(cube),
                     .grid = std::move(grid),
                     .ground = std::move(ground),
                     .groundInstance = std::move(groundInstance),
                     .registry = std::move(registry),
                     .modelCache = std::move(modelCache),
                     .models = std::move(models),
                     .modelInstance = std::move(modelInstance),
                     .modelParts = std::move(modelParts),
                     .checker = std::move(checker),
                     .sampler = std::move(samplerHandle),
                     .material = std::move(material),
                     .cameraEntity = cameraEntity,
                     .camera = camera,
                     .demoProps = !khronosView,
                     .gpuTimer = levain::render::createGpuTimer(*gpu.nvrhi),
                     .skinningTimer = levain::render::createGpuTimer(*gpu.nvrhi),
                     .skinningCost = {},
                     .cameraCulling = {},
                     .shadowCulling = {}};
}

/// Huit lumières de couleur qui tournent autour du modèle (`modelPlacement`), un tour en 12 s :
/// de quoi voir le forward+ éclairer le modèle et le sol.
std::vector<levain::render::PointLight> demoLightsAt(double seconds)
{
    constexpr std::array<glm::vec3, 4> Colors{
        {{1.0f, 0.3f, 0.2f}, {0.2f, 1.0f, 0.4f}, {0.3f, 0.5f, 1.0f}, {1.0f, 0.8f, 0.2f}}};
    const glm::vec3 center = modelPlacement(1.0f).position;
    std::vector<levain::render::PointLight> lights;
    lights.reserve(8);
    for (std::size_t i = 0; i < 8; ++i)
    {
        const float angle = (static_cast<float>(seconds) * glm::two_pi<float>() / 12.0f) +
                            (static_cast<float>(i) * glm::two_pi<float>() / 8.0f);
        lights.push_back(
            {.position = center + glm::vec3{5.0f * std::cos(angle), 1.5f, 5.0f * std::sin(angle)},
             .range = 8.0f,
             .color = Colors[i % Colors.size()],
             .intensity = 12.0f});
    }
    return lights;
}

/// Un tour toutes les 6 secondes, autour d'un axe incliné pour montrer trois faces à la fois.
glm::mat4 cubeRotation(double seconds)
{
    const float angle = static_cast<float>(seconds) * glm::two_pi<float>() / 6.0f;
    return glm::rotate(glm::mat4{1.0f}, angle, glm::vec3{1.0f, 1.0f, 0.0f});
}

/// La pose d'un modèle skinné à `seconds` : son animateur s'il en a un, sinon son clip en boucle.
void poseModel(ModelGpu& model, const levain::animation::AnimationSet& set, double seconds)
{
    if (model.animator)
    {
        const levain::animation::AnimatorLayers layers = levain::animation::advanceAnimator(
            set, model.animatorClips, *model.animator, {.speed = demoSpeedAt(seconds)},
            static_cast<float>(seconds) - model.lastSeconds);
        levain::animation::sampleBlend(set, layers, model.pose);
        return;
    }
    levain::animation::samplePose(set, model.clip, static_cast<float>(seconds), model.pose);
}

/// Anime les modèles skinnés (ADR-0022) : la pose de leur clip à `seconds`, ses matrices, puis un
/// dispatch par mesh skinné. À enregistrer avant les dessins qui lisent les sommets déformés. Le
/// coût est relevé dans `scene.skinningCost`.
void animateModels(nvrhi::IDevice& device, nvrhi::ICommandList& commandList, DemoScene& scene,
                   double seconds)
{
    const Clock::time_point start = Clock::now();
    std::optional<std::optional<double>> gpuMs; ///< Vide tant qu'aucun modèle n'est animé.
    for (auto& [id, model] : scene.models)
    {
        if (!model.animation)
        {
            continue;
        }
        if (!gpuMs)
        {
            gpuMs = levain::render::beginGpuTimer(device, commandList, scene.skinningTimer);
        }
        const levain::animation::Pose before = model.pose;
        poseModel(model, *model.animation, seconds);
        if (!before.joints.empty() && seconds > model.lastSeconds)
        {
            scene.skinningCost.maxJointSpeed =
                std::max(scene.skinningCost.maxJointSpeed,
                         maxJointSpeedOf(before, model.pose,
                                         static_cast<float>(seconds) - model.lastSeconds));
        }
        model.lastSeconds = static_cast<float>(seconds);
        levain::animation::skinningMatrices(*model.animation, model.pose, model.skinMatrices);
        for (const std::vector<ModelPrimitiveGpu>& mesh : model.meshes)
        {
            for (const ModelPrimitiveGpu& primitive : mesh)
            {
                if (primitive.skin)
                {
                    levain::render::skinMesh(commandList, scene.skinning, *primitive.skin,
                                             model.skinMatrices);
                }
            }
        }
    }
    if (!gpuMs)
    {
        return;
    }
    levain::render::endGpuTimer(commandList, scene.skinningTimer);
    scene.skinningCost.cpuMs += secondsBetween(start, Clock::now()) * 1000.0;
    ++scene.skinningCost.frames;
    if (*gpuMs)
    {
        scene.skinningCost.gpuMs += **gpuMs;
        ++scene.skinningCost.gpuSamples;
    }
}

/// Ce que l'image dessine : les cubes, le sol, et le modèle glTF nœud par nœud, chaque primitive
/// avec son matériau et sa matrice monde. `draw(mesh, instances, matériau, modèle)` est appelé pour
/// chacun de ceux qui touchent `frustum` : par la passe d'ombres, puis par la passe des meshes. Les
/// autres sont comptés dans `count`, sans être soumis au GPU.
template <typename Draw>
void forEachDraw(DemoScene& scene, double seconds, const levain::render::Frustum& frustum,
                 DrawCount& count, Draw&& draw)
{
    const auto drawIfVisible = [&](const levain::render::Mesh& mesh,
                                   const levain::render::Instances& instances,
                                   nvrhi::IBindingSet& material, const glm::mat4& model)
    {
        if (const auto box = levain::render::worldBoundsOf(mesh, instances, model);
            box && levain::render::isOutside(frustum, *box))
        {
            ++count.culled;
            return;
        }
        ++count.drawn;
        count.triangles += std::uint64_t{mesh.indexCount / 3} * std::max(instances.count, 1u);
        draw(mesh, instances, material, model);
    };
    if (scene.demoProps)
    {
        drawIfVisible(scene.cube, scene.grid, *scene.material, cubeRotation(seconds));
        drawIfVisible(scene.ground, scene.groundInstance, *scene.material, glm::mat4{1.0f});
    }
    scene.modelParts.each(
        [&](const levain::assets::MeshRef& part, const levain::scene::WorldTransform& world)
        {
            const ModelGpu& model = scene.models.at(part.mesh.asset);
            for (const ModelPrimitiveGpu& primitive : model.meshes[part.mesh.sub])
            {
                drawIfVisible(primitive.mesh, scene.modelInstance,
                              primitive.material ? *model.materials[*primitive.material]
                                                 : *scene.material,
                              world.matrix);
            }
        });
}

/// Inscrit les dessins de la démo dans les étapes du renderer (ADR-0025), comme le ferait un plugin
/// : les cubes, le sol et les modèles projettent leur ombre, puis se dessinent parmi les opaques.
/// La scène doit être à sa place définitive : les fonctions la gardent par référence.
void addDemoStages(DemoScene& scene)
{
    using levain::render::RenderStage;
    using levain::render::StageContext;
    levain::render::addStageFunction(
        scene.renderer.stages, RenderStage::ShadowCasters, "démo",
        [&scene](const StageContext& context)
        {
            forEachDraw(scene, context.seconds, context.frustum, scene.shadowCulling,
                        [&](const levain::render::Mesh& mesh,
                            const levain::render::Instances& instances, nvrhi::IBindingSet&,
                            const glm::mat4& model)
                        {
                            levain::render::drawShadowCaster(context.commandList, context.shadows,
                                                             context.cascade, *context.cascadeView,
                                                             mesh, instances, model);
                        });
        });
    levain::render::addStageFunction(
        scene.renderer.stages, RenderStage::Opaque, "démo",
        [&scene](const StageContext& context)
        {
            forEachDraw(scene, context.seconds, context.frustum, scene.cameraCulling,
                        [&](const levain::render::Mesh& mesh,
                            const levain::render::Instances& instances,
                            nvrhi::IBindingSet& material, const glm::mat4& model)
                        {
                            levain::render::drawMesh(
                                context.commandList, scene.renderer.meshPass, context.frame,
                                context.target, mesh, instances, material,
                                {.viewProjection = context.viewProjection, .model = model});
                        });
        });
    levain::core::log("sandbox", levain::core::LogLevel::Info, "étapes du rendu : {}",
                      levain::render::describeStages(scene.renderer.stages));
}

/// Efface l'image de la swapchain et son depth buffer, y dessine la grille, et la présente. Rend le
/// temps GPU d'une frame précédente, dès qu'il est lisible. Avec `capture`, l'image est aussi
/// copiée pour être relue (`render::readBack`).
std::optional<double> renderFrame(levain::gpu::GpuDevice& gpu,
                                  const levain::platform::Window& window, DemoScene& scene,
                                  nvrhi::ICommandList& commandList, double seconds,
                                  nvrhi::StagingTextureHandle* capture = nullptr)
{
    nvrhi::ITexture* backBuffer = levain::gpu::beginFrame(gpu, window);
    if (backBuffer == nullptr)
    {
        return std::nullopt;
    }

    std::optional<double> gpuMs;

    {
        // Le travail CPU d'une frame, hors attente de l'écran (critère de M1.3,
        // tools/tracy-capture.sh).
        LEVAIN_PROFILE_SCOPE_NAMED("commandes");

        commandList.open();
        gpuMs = levain::render::beginGpuTimer(*gpu.nvrhi, commandList, scene.gpuTimer);
        animateModels(*gpu.nvrhi, commandList, scene, seconds);
        // Le renderer dessine ce que contient le monde : les positions du tour qui vient de finir.
        gatherCubePositions(scene.cubes, scene.cubePositions);
        levain::render::updateInstances(commandList, scene.grid, scene.cubePositions);
        const std::vector<levain::render::PointLight> lights =
            scene.demoProps ? demoLightsAt(seconds) : std::vector<levain::render::PointLight>{};
        levain::render::renderFrame(*gpu.nvrhi, commandList, scene.renderer,
                                    {.camera = scene.camera,
                                     .sun = scene.sun,
                                     .environmentIntensity = 1.0f,
                                     .lights = lights,
                                     .tonemap = scene.tonemapSettings,
                                     // Une croûte de levain, là où rien n'est dessiné.
                                     .background = {0.55f, 0.32f, 0.14f, 1.0f},
                                     .seconds = seconds},
                                    *backBuffer);
        if (capture != nullptr)
        {
            *capture = levain::render::copyForReadback(*gpu.nvrhi, commandList, *backBuffer);
        }
        levain::render::endGpuTimer(commandList, scene.gpuTimer);
        commandList.close();
        gpu.nvrhi->executeCommandList(&commandList);
    }

    LEVAIN_PROFILE_SCOPE_NAMED("présentation");
    levain::gpu::presentFrame(gpu);
    return gpuMs;
}

/// Le hot-reload des textures (ADR-0021) : les fichiers du registre, relus une fois par période.
struct TextureReload
{
    levain::assets::AssetWatch watch;
    Clock::time_point nextCheck;
};

/// Comme les shaders : au plus 100 ms entre l'enregistrement d'une texture et sa détection.
constexpr std::chrono::milliseconds TextureCheckPeriod{100};

TextureReload startTextureReload(const levain::assets::AssetRegistry& registry)
{
    return TextureReload{.watch = levain::assets::watchAssets(registry),
                         .nextCheck = Clock::now() + TextureCheckPeriod};
}

/// Le temps écoulé depuis la dernière modification de `file`. `file_clock` : la même horloge que
/// les dates de modification, sans conversion.
double millisecondsSinceWrite(const std::filesystem::path& file)
{
    std::error_code error;
    const auto age = std::filesystem::file_time_type::clock::now() -
                     std::filesystem::last_write_time(file, error);
    return std::chrono::duration<double, std::milli>(age).count();
}

/// Recharge les textures modifiées sur le disque, depuis leur source puisque leur fichier cuit est
/// périmé, et refait les binding sets des modèles qui les utilisent. Une texture qui ne se charge
/// pas (fichier invalide, ou à moitié écrit) reste en place, et l'erreur va dans le log.
void reloadChangedTextures(TextureReload& reload, nvrhi::IDevice& device,
                           levain::assets::AssetRegistry& registry,
                           const levain::assets::ModelCache& modelCache,
                           std::map<levain::assets::AssetId, ModelGpu>& models,
                           const levain::render::MeshPass& meshPass, nvrhi::ISampler& sampler)
{
    const Clock::time_point now = Clock::now();
    if (now < reload.nextCheck)
    {
        return;
    }
    reload.nextCheck = now + TextureCheckPeriod;
    const std::vector<levain::assets::AssetId> changed =
        levain::assets::takeChangedAssets(registry, reload.watch);
    if (changed.empty())
    {
        return;
    }

    const nvrhi::CommandListHandle commandList = device.createCommandList();
    commandList->open();
    std::set<levain::assets::AssetId> touchedModels;
    for (const levain::assets::AssetId& id : changed)
    {
        const std::filesystem::path file = registry.entries.at(id).file;
        // Une texture dans son propre fichier est le sous-asset 0 de son GUID (ADR-0020).
        const levain::assets::AssetRef ref{.asset = id, .sub = 0};
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
            levain::core::log("assets", levain::core::LogLevel::Info,
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
                levain::core::log("assets", levain::core::LogLevel::Error,
                                  "{} ; l'ancienne texture reste", texture.error().message);
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
        levain::core::log(
            "assets", levain::core::LogLevel::Info,
            "{} rechargée en {:.0f} ms, {:.0f} ms après son écriture", file.filename().string(),
            secondsBetween(loadStart, Clock::now()) * 1000.0, millisecondsSinceWrite(file));
    }
    // Un binding set désigne ses textures : ceux des modèles touchés sont refaits, avant la
    // fermeture de l'envoi, qui porte leurs constantes. NVRHI garde les anciens, et l'ancienne
    // texture, tant qu'une image en vol s'en sert.
    for (const levain::assets::AssetId& modelId : touchedModels)
    {
        ModelGpu& gpu = models.at(modelId);
        gpu.materials.clear();
        bindModelMaterials(device, *commandList, meshPass, sampler, modelCache.models.at(modelId),
                           gpu);
    }
    commandList->close();
    device.executeCommandList(commandList);
}

struct SandboxOptions
{
    /// La durée de la boucle, sans limite par défaut. Comptée depuis le premier tour de boucle, pas
    /// depuis le lancement : en CI, le démarrage varie de 1 à plus de 10 s selon la charge du
    /// runner (lavapipe), et un délai extérieur tombait parfois avant la première frame.
    double loopSeconds = std::numeric_limits<double>::infinity();
    /// Le filtrage anisotrope du damier ; 1 le désactive (trilinéaire seul).
    float maxAnisotropy = 16.0f;
    /// L'exposition du tonemapping (M5.2) : 2 éclaire d'un diaphragme.
    float exposure = 1.0f;
    /// La courbe du tonemapping : `--tonemap clip|aces|agx|neutral`.
    levain::render::Tonemapper tonemapper = levain::render::Tonemapper::Agx;
    /// Un glTF à afficher devant la caméra (M4.1).
    std::optional<std::filesystem::path> modelPath;
    /// Le clip que joue un modèle skinné, par son nom ; le premier par défaut.
    std::optional<std::string> clipName;
    /// Les clips du repos, de la marche et de la course, séparés par des virgules : le modèle
    /// passe de l'un à l'autre selon une vitesse de démonstration (#118).
    std::optional<std::string> locomotion;
    /// L'échelle du modèle (voir `modelPlacement`).
    float modelScale = 2.0f;
    /// Le temps de la scène, figé : les cubes et les animations s'arrêtent à cet instant. Deux
    /// captures prises avec le même `--time` se comparent pixel par pixel (d'un build, d'un
    /// shader ou d'un backend à l'autre).
    std::optional<double> frozenSeconds;
    /// Où écrire une capture de la dernière image, en PNG. Avec `--seconds`, c'est ce qui montre un
    /// rendu à distance, sans écran ni capture du bureau.
    std::optional<std::filesystem::path> capturePath;
    /// `--view khronos` : la scène telle que l'ouvre le glTF Sample Viewer, pour s'y comparer
    /// (#125, #131, tools/khronos-compare.sh). Le modèle seul, à l'origine, sous sa caméra et son
    /// ciel ; ni cubes, ni sol, ni lumières de la démo.
    bool khronosView = false;
    /// `--camera x,y,z` : la caméra à cette position, face à −Z. Celle du glTF Sample Viewer, que
    /// tools/khronos-compare.sh relit dans sa page.
    std::optional<glm::vec3> cameraPosition;
    /// `--sun x,y,z` : un soleil blanc d'intensité 1, venant de cette direction. La lumière
    /// principale du glTF Sample Viewer sans IBL (#125).
    std::optional<glm::vec3> sunDirection;
    /// L'HDRI qui éclaire la scène (M5.4) ; sans, `LEVAIN_DEFAULT_SKY` s'il a été téléchargé.
    std::optional<std::filesystem::path> skyPath;
    /// `--gpu webgpu` : le backend WebGPU sur Dawn, hors écran, pour le vérifier sans navigateur
    /// (ADR-0023).
    nvrhi::GraphicsAPI api = nvrhi::GraphicsAPI::VULKAN;
};

/// Un nombre strictement positif, écrit en entier. Vide sinon, NaN compris.
std::optional<double> parsePositive(std::string_view text)
{
    double value = 0.0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || !(value > 0.0))
    {
        return std::nullopt;
    }
    return value;
}

/// Trois nombres séparés par des virgules, « 1.5,-2,0 ». Vide si le texte n'en est pas.
std::optional<glm::vec3> parseVector(std::string_view text)
{
    glm::vec3 vector{0.0f};
    const char* cursor = text.data();
    const char* const end = text.data() + text.size();
    for (int axis = 0; axis < 3; ++axis)
    {
        const auto [next, error] = std::from_chars(cursor, end, vector[axis]);
        const bool last = axis == 2;
        if (error != std::errc{} || (last ? next != end : (next == end || *next != ',')))
        {
            return std::nullopt;
        }
        cursor = next + 1;
    }
    return vector;
}

/// `[--seconds N] [--anisotropy N] [--capture fichier.png] [--model fichier.gltf]`, dans n'importe
/// quel ordre. Vide si les arguments sont invalides.
std::optional<SandboxOptions> parseOptions(std::span<char* const> arguments)
{
    SandboxOptions options;
    for (std::size_t i = 1; i < arguments.size(); i += 2)
    {
        const std::string_view name{arguments[i]};
        if (i + 1 >= arguments.size())
        {
            return std::nullopt;
        }
        if (name == "--clip" || name == "--locomotion")
        {
            (name == "--clip" ? options.clipName : options.locomotion) =
                std::string{arguments[i + 1]};
            continue;
        }
        if (name == "--tonemap")
        {
            const std::string_view tonemapper{arguments[i + 1]};
            constexpr std::array<std::pair<std::string_view, levain::render::Tonemapper>, 4>
                Tonemappers{{{"clip", levain::render::Tonemapper::Clip},
                             {"aces", levain::render::Tonemapper::Aces},
                             {"agx", levain::render::Tonemapper::Agx},
                             {"neutral", levain::render::Tonemapper::KhronosPbrNeutral}}};
            const auto found =
                std::ranges::find(Tonemappers, tonemapper,
                                  &std::pair<std::string_view, levain::render::Tonemapper>::first);
            if (found == Tonemappers.end())
            {
                return std::nullopt;
            }
            options.tonemapper = found->second;
            continue;
        }
        if (name == "--camera" || name == "--sun")
        {
            std::optional<glm::vec3>& vector =
                name == "--camera" ? options.cameraPosition : options.sunDirection;
            vector = parseVector(arguments[i + 1]);
            if (!vector)
            {
                return std::nullopt;
            }
            continue;
        }
        if (name == "--view")
        {
            const std::string_view view{arguments[i + 1]};
            if (view != "khronos" && view != "demo")
            {
                return std::nullopt;
            }
            options.khronosView = view == "khronos";
            continue;
        }
        if (name == "--gpu")
        {
            const std::string_view api{arguments[i + 1]};
            if (api != "vulkan" && api != "webgpu")
            {
                return std::nullopt;
            }
            options.api = api == "webgpu" ? nvrhi::GraphicsAPI::WEBGPU : nvrhi::GraphicsAPI::VULKAN;
            continue;
        }
        if (name == "--capture" || name == "--model" || name == "--sky")
        {
            (name == "--capture" ? options.capturePath
             : name == "--model" ? options.modelPath
                                 : options.skyPath) = std::filesystem::path{arguments[i + 1]};
            continue;
        }
        const std::optional<double> value = parsePositive(arguments[i + 1]);
        if (!value)
        {
            return std::nullopt;
        }
        if (name == "--seconds")
        {
            options.loopSeconds = *value;
        }
        else if (name == "--exposure")
        {
            options.exposure = static_cast<float>(*value);
        }
        else if (name == "--anisotropy")
        {
            options.maxAnisotropy = static_cast<float>(*value);
        }
        else if (name == "--time")
        {
            options.frozenSeconds = value;
        }
        else if (name == "--model-scale")
        {
            options.modelScale = static_cast<float>(*value);
        }
        else
        {
            return std::nullopt;
        }
    }
    return options;
}

/// Rend une dernière image et l'écrit en PNG. Un échec est bruyant (règle n°7) : une capture
/// demandée et absente ferait croire à une image qui n'existe pas.
bool captureFrame(levain::gpu::GpuDevice& gpu, const levain::platform::Window& window,
                  DemoScene& scene, nvrhi::ICommandList& commandList, double seconds,
                  const std::filesystem::path& path)
{
    nvrhi::StagingTextureHandle staging;
    // std::addressof et non « & » : le RefCountPtr de NVRHI surcharge l'opérateur & (il rend
    // l'adresse du pointeur brut, comme les ComPtr de COM).
    static_cast<void>(
        renderFrame(gpu, window, scene, commandList, seconds, std::addressof(staging)));
    if (!staging)
    {
        levain::core::log("sandbox", levain::core::LogLevel::Error,
                          "capture impossible : aucune image rendue (fenêtre masquée ?)");
        return false;
    }
    auto image = levain::render::readBack(*gpu.nvrhi, *staging);
    auto saved = image ? levain::assets::savePng(path, image->width, image->height, image->rgba)
                       : std::unexpected(image.error());
    if (!saved)
    {
        levain::core::log("sandbox", levain::core::LogLevel::Error, "capture : {}",
                          saved.error().message);
        return false;
    }
    levain::core::log("sandbox", levain::core::LogLevel::Info, "capture : {} ({} × {})",
                      path.string(), image->width, image->height);
    return true;
}

/// Ce que la boucle anime et lit, créé une fois le device là.
struct Sandbox
{
    DemoScene scene;
    levain::input::Bindings bindings;
    CameraActions actions;
};

/// Le ciel par défaut, Kloofendal (sandbox/CMakeLists.txt), s'il a été téléchargé : sans lui, un
/// ciel uniforme, et la ligne du journal dit pourquoi.
std::optional<std::filesystem::path> defaultSky()
{
    std::error_code error;
    if (std::filesystem::exists(LEVAIN_DEFAULT_SKY, error))
    {
        return std::filesystem::path{LEVAIN_DEFAULT_SKY};
    }
    levain::core::log("sandbox", levain::core::LogLevel::Info,
                      "pas de ciel : {} absent (tools/fetch-assets.sh), ambiance uniforme",
                      LEVAIN_DEFAULT_SKY);
    return std::nullopt;
}

levain::core::Result<Sandbox> createSandbox(levain::gpu::GpuDevice& gpu,
                                            const SandboxOptions& options)
{
    const levain::render::SamplerSettings sampler{.maxAnisotropy = options.maxAnisotropy};
    levain::core::log("sandbox", levain::core::LogLevel::Info, "filtrage anisotrope : {}",
                      levain::render::clampAnisotropy(sampler.maxAnisotropy));
    auto scene =
        createDemoScene(gpu, sampler, options.modelPath, options.clipName, options.locomotion,
                        options.modelScale, options.skyPath ? options.skyPath : defaultSky(),
                        options.khronosView, options.cameraPosition, options.sunDirection);
    if (!scene)
    {
        return std::unexpected{std::move(scene.error())};
    }
    scene->tonemapSettings = {.exposure = options.exposure, .tonemapper = options.tonemapper};

    // Les liaisons d'entrée : changer une touche dans data/input.cfg ne demande aucune
    // recompilation (ADR-0017). Un nom inconnu échoue ici, avec son numéro de ligne.
    auto bindings = levain::input::loadBindings(LEVAIN_DATA_DIR "/input.cfg");
    if (!bindings)
    {
        return std::unexpected{std::move(bindings.error())};
    }
    auto actions = cameraActionsOf(*bindings);
    if (!actions)
    {
        return std::unexpected{std::move(actions.error())};
    }

    levain::core::log("sandbox", levain::core::LogLevel::Info,
                      "liaisons : {} actions et {} axes (data/input.cfg) ; clic droit pour "
                      "regarder, ZQSD ou WASD pour avancer",
                      bindings->actions.size(), bindings->axes.size());
    return Sandbox{
        .scene = std::move(*scene), .bindings = std::move(*bindings), .actions = *actions};
}

/// Ce que la boucle garde d'une image à l'autre. Une image est une fonction (`runFrame`) : en
/// natif, la boucle l'appelle ; dans le navigateur, c'est lui, à chaque image (ADR-0023, point 3).
struct Loop
{
    levain::platform::Window& window;
    levain::gpu::GpuDevice& gpu;
    DemoScene& scene;
    const levain::input::Bindings& bindings;
    const CameraActions& actions;
    double loopSeconds;
    std::optional<double> frozenSeconds;

    nvrhi::CommandListHandle commandList;
    LoopState state;
    levain::core::FrameTimeAccumulator frameTimes;
    GpuTimeAverage periodGpu; ///< Depuis la dernière mise à jour du titre.
    GpuTimeAverage totalGpu;  ///< Depuis le début de la boucle, journalisé à la fin.
    Clock::time_point loopStart;
    Clock::time_point previousFrameEnd;
    int frameCount = 0;
    ShaderReload shaderReload;
    TextureReload textureReload;
    nvrhi::FramebufferInfo sceneTarget;
    levain::input::InputState input;
    bool mouseCaptured = false;
    /// La durée de l'image précédente. La première n'en a pas : un pas de simulation, pour
    /// démarrer.
    double lastFrameSeconds;
};

Loop startLoop(levain::platform::Window& window, levain::gpu::GpuDevice& gpu, Sandbox& sandbox,
               double loopSeconds, std::optional<double> frozenSeconds)
{
    DemoScene& scene = sandbox.scene;
    addDemoStages(scene);
    const Clock::time_point now = Clock::now();
    return Loop{.window = window,
                .gpu = gpu,
                .scene = scene,
                .bindings = sandbox.bindings,
                .actions = sandbox.actions,
                .loopSeconds = loopSeconds,
                .frozenSeconds = frozenSeconds,
                .commandList = gpu.nvrhi->createCommandList(),
                .state = {},
                .frameTimes = {},
                .periodGpu = {},
                .totalGpu = {},
                .loopStart = now,
                .previousFrameEnd = now,
                .frameCount = 0,
                .shaderReload = startShaderReload(),
                .textureReload = startTextureReload(scene.registry),
                .sceneTarget = levain::render::sceneTargetInfo(),
                .input = levain::input::makeInputState(sandbox.bindings),
                .mouseCaptured = false,
                .lastFrameSeconds = scene.fixedStep.stepSeconds};
}

/// Le temps de la scène : celui de la boucle, ou celui de --time, figé.
double sceneSecondsOf(const Loop& loop)
{
    return loop.frozenSeconds.value_or(secondsBetween(loop.loopStart, Clock::now()));
}

/// Une image de la boucle ; `false` quand elle s'arrête (fenêtre fermée, --seconds écoulées).
bool runFrame(Loop& loop)
{
    if (!loop.state.isRunning || secondsBetween(loop.loopStart, Clock::now()) >= loop.loopSeconds)
    {
        return false;
    }
    DemoScene& scene = loop.scene;

    if (!loop.state.isVisible)
    {
#ifdef __EMSCRIPTEN__
        // Le navigateur n'appelle plus un onglet caché : rien à attendre, et rien ne s'y attend.
        return true;
#endif
        // Pas au-delà de --seconds : masquée sans événement (bureau verrouillé), la boucle
        // dormirait sinon indéfiniment. L'infini par défaut attend sans limite.
        const double remainingSeconds =
            loop.loopSeconds - secondsBetween(loop.loopStart, Clock::now());
        for (const auto& event : levain::platform::waitEvents(loop.window, remainingSeconds).window)
        {
            applyWindowEvent(loop.state, event);
        }

        // Le temps passé masquée n'est pas une frame. Sans cette remise à l'heure, la
        // première frame après la restauration durerait toute la minimisation, et le
        // maximum affiché serait de plusieurs secondes.
        loop.previousFrameEnd = Clock::now();
        return true;
    }

    {
        LEVAIN_PROFILE_SCOPE_NAMED("événements");

        const levain::platform::Events events = levain::platform::pollEvents(loop.window);
        for (const auto& event : events.window)
        {
            applyWindowEvent(loop.state, event);
        }
        levain::input::updateInput(loop.input, loop.bindings, events.input,
                                   static_cast<float>(loop.lastFrameSeconds));

        // La souris ne se capture que pendant le regard : sinon on ne pourrait plus rien
        // faire d'autre de la fenêtre.
        const bool looking = levain::input::actionHeld(loop.input, loop.actions.lookEnable);
        if (looking != loop.mouseCaptured)
        {
            levain::platform::setMouseCaptured(loop.window, looking);
            loop.mouseCaptured = looking;
        }
        // Ce que le joueur demande, posé pour le prochain pas de simulation.
        scene.world.set<levain::scene::FpsInput>(fpsInputFrom(loop.input, loop.actions));
    }

    reloadChangedShaders(loop.shaderReload, *loop.gpu.nvrhi, loop.sceneTarget,
                         scene.renderer.meshPass);
    reloadChangedTextures(loop.textureReload, *loop.gpu.nvrhi, scene.registry, scene.modelCache,
                          scene.models, scene.renderer.meshPass, *scene.sampler);

    {
        // Un tour du monde : les pas de simulation que la dernière image a mérités, puis une
        // passe de rendu qui interpole et compose les matrices monde (ADR-0016). La durée
        // passée est celle de l'image précédente : celle-ci n'est pas encore finie.
        LEVAIN_PROFILE_SCOPE_NAMED("monde");
        levain::scene::advanceWorld(scene.world, scene.fixedStep,
                                    static_cast<float>(loop.lastFrameSeconds));
        updateRenderCamera(scene.camera, scene.cameraEntity);
    }

    {
        LEVAIN_PROFILE_SCOPE_NAMED("rendu");
        if (const auto gpuMs =
                renderFrame(loop.gpu, loop.window, scene, *loop.commandList, sceneSecondsOf(loop)))
        {
            loop.periodGpu.totalMs += *gpuMs;
            ++loop.periodGpu.samples;
            loop.totalGpu.totalMs += *gpuMs;
            ++loop.totalGpu.samples;
        }
    }

    // Fin d'image : ce que plus aucune entité n'utilise se décharge, du CPU et du GPU
    // (ADR-0019). NVRHI garde vivantes les ressources qu'une command list en vol utilise
    // encore.
    for (const levain::assets::AssetId& unused : levain::assets::takeUnusedAssets(scene.world))
    {
        scene.models.erase(unused);
        scene.modelCache.models.erase(unused);
    }

    const Clock::time_point frameEnd = Clock::now();
    const double frameSeconds = secondsBetween(loop.previousFrameEnd, frameEnd);
    loop.previousFrameEnd = frameEnd;
    loop.lastFrameSeconds = frameSeconds;

    if (const auto summary =
            levain::core::recordFrame(loop.frameTimes, frameSeconds, FrameTimePeriodSeconds))
    {
        LEVAIN_PROFILE_SCOPE_NAMED("titre");
        levain::platform::setWindowTitle(loop.window,
                                         describeFrameTimes(*summary, averageOf(loop.periodGpu)));
        loop.periodGpu = {};
    }

    ++loop.frameCount;
    LEVAIN_PROFILE_FRAME();
    return true;
}

/// Le bilan de la boucle, puis la capture demandée ; `false` si elle a échoué.
bool finishLoop(Loop& loop, const std::optional<std::filesystem::path>& capturePath)
{
    // Lu par la CI, qui échoue si la boucle a tourné moins d'une seconde : un démarrage lent
    // (lavapipe, validation, sanitizers) peut sinon manger tout le délai sans que rien ne rougisse.
    levain::core::log(
        "sandbox", levain::core::LogLevel::Info,
        "boucle arrêtée après {:.1f} s et {} frames ; GPU : {:.3f} ms en moyenne sur {} mesures",
        secondsBetween(loop.loopStart, Clock::now()), loop.frameCount, averageOf(loop.totalGpu),
        loop.totalGpu.samples);
    // Le critère de M5.3 : le temps GPU de la passe d'ombres, quatre cascades.
    const auto& passTimes = loop.scene.renderer.passTimes;
    const GpuTimeAverage& shadowGpu = passTimes[1]; // RendererPassNames : « ombres »
    levain::core::log("sandbox", levain::core::LogLevel::Info,
                      "ombres : {:.3f} ms GPU en moyenne sur {} mesures", averageOf(shadowGpu),
                      shadowGpu.samples);
    // Le critère de #133 : le temps GPU de chaque passe. Une étape où rien n'est inscrit n'est pas
    // chronométrée, et n'apparaît pas.
    std::string passes;
    for (std::size_t pass = 0; pass < levain::render::RendererPassNames.size(); ++pass)
    {
        if (passTimes[pass].samples > 0)
        {
            passes +=
                std::format("{}{} {:.3f} ms", passes.empty() ? "" : ", ",
                            levain::render::RendererPassNames[pass], averageOf(passTimes[pass]));
        }
    }
    levain::core::log("sandbox", levain::core::LogLevel::Info, "passes, GPU en moyenne : {}",
                      passes);
    // Le critère de #132 : ce que le frustum culling épargne au GPU, par image.
    const auto perFrame = [&loop](std::uint64_t count)
    { return static_cast<double>(count) / std::max(loop.frameCount, 1); };
    const DrawCount& camera = loop.scene.cameraCulling;
    const DrawCount& shadows = loop.scene.shadowCulling;
    levain::core::log(
        "sandbox", levain::core::LogLevel::Info,
        "culling, par image : caméra {:.1f} dessins écartés sur {:.1f}, ombres {:.1f} "
        "sur {:.1f} (4 cascades)",
        perFrame(camera.culled), perFrame(camera.culled + camera.drawn), perFrame(shadows.culled),
        perFrame(shadows.culled + shadows.drawn));
    levain::core::log("sandbox", levain::core::LogLevel::Info,
                      "dessins, par image : caméra {:.1f} appels et {:.0f} triangles, ombres "
                      "{:.1f} appels et {:.0f} triangles",
                      perFrame(camera.drawn), perFrame(camera.triangles), perFrame(shadows.drawn),
                      perFrame(shadows.triangles));
    const SkinningCost& skinning = loop.scene.skinningCost;
    if (skinning.frames > 0)
    {
        levain::core::log("sandbox", levain::core::LogLevel::Info,
                          "skinning : {:.1f} µs CPU sur {} images, {:.1f} µs GPU sur {} mesures, "
                          "en moyenne ; os le plus rapide : {:.0f} unités/s",
                          skinning.cpuMs * 1000.0 / static_cast<double>(skinning.frames),
                          skinning.frames,
                          skinning.gpuSamples > 0
                              ? skinning.gpuMs * 1000.0 / static_cast<double>(skinning.gpuSamples)
                              : 0.0,
                          skinning.gpuSamples, skinning.maxJointSpeed);
    }

    return !capturePath || captureFrame(loop.gpu, loop.window, loop.scene, *loop.commandList,
                                        sceneSecondsOf(loop), *capturePath);
}

#ifdef __EMSCRIPTEN__
/// Dans le navigateur, main rend la main avant que le device n'arrive (ADR-0023, point 2) : ce que
/// la boucle utilise vit ici, jusqu'à la fermeture de l'onglet.
struct WebSandbox
{
    SandboxOptions options;
    std::optional<levain::platform::Window> window;
    std::optional<levain::gpu::GpuDevice> gpu;
    std::optional<Sandbox> sandbox;
    std::optional<Loop> loop;
};

WebSandbox& webSandbox()
{
    static WebSandbox instance;
    return instance;
}

void runWebFrame()
{
    WebSandbox& web = webSandbox();
    if (!runFrame(*web.loop))
    {
        std::ignore = finishLoop(*web.loop, std::nullopt);
        emscripten_cancel_main_loop();
    }
}

/// La suite de main, quand le navigateur a donné le device.
void startWebSandbox(levain::core::Result<levain::gpu::GpuDevice> gpu)
{
    WebSandbox& web = webSandbox();
    if (!gpu)
    {
        levain::core::log("sandbox", levain::core::LogLevel::Critical, "{}", gpu.error().message);
        return;
    }
    web.gpu.emplace(std::move(*gpu));
    auto sandbox = createSandbox(*web.gpu, web.options);
    if (!sandbox)
    {
        levain::core::log("sandbox", levain::core::LogLevel::Critical, "{}",
                          sandbox.error().message);
        return;
    }
    web.sandbox.emplace(std::move(*sandbox));
    web.loop.emplace(startLoop(*web.window, *web.gpu, *web.sandbox, web.options.loopSeconds,
                               web.options.frozenSeconds));
    // 0 : au rythme de requestAnimationFrame, celui de l'écran.
    emscripten_set_main_loop(runWebFrame, 0, false);
}
#endif

} // namespace

int main(int argc, char** argv)
{
    // std::print et std::format peuvent lever : format_error sur une chaîne de format
    // invalide, system_error si l'écriture échoue. On rattrape au sommet (ADR-0008).
    try
    {
        const std::optional<SandboxOptions> options =
            parseOptions(std::span{argv, static_cast<std::size_t>(argc)});
        if (!options)
        {
            std::println(stderr,
                         "usage : levain_sandbox [--seconds N] [--anisotropy N] [--capture "
                         "fichier.png] [--model fichier.gltf [--clip nom | --locomotion "
                         "repos,marche,course] "
                         "[--model-scale N]] [--time secondes] [--gpu vulkan|webgpu] "
                         "[--exposure N] [--tonemap clip|aces|agx|neutral] [--sky "
                         "fichier.hdr|none] [--view demo|khronos] [--camera x,y,z] [--sun x,y,z]");
            return 2;
        }

        std::print("Levain {} — {} — __cplusplus {}\n", levain::core::version(),
                   levain::core::toolchain(), __cplusplus);

        // 1920 × 1080 : la résolution du critère de M2.1. En points ; un point vaut un pixel sur la
        // machine de référence, et le log « redimensionnée » donne les pixels réels.
        auto window = levain::platform::createWindow("Levain", 1920, 1080);
        if (!window)
        {
            levain::core::log("sandbox", levain::core::LogLevel::Critical, "{}",
                              window.error().message);
            return 1;
        }

#ifdef __EMSCRIPTEN__
        WebSandbox& web = webSandbox();
        web.options = *options;
        web.window.emplace(std::move(*window));
        levain::gpu::requestGpuDevice(*web.window, {.enableValidation = EnableValidation},
                                      startWebSandbox);
#else
        // Déclaré après window, gpu sera détruit avant elle : la surface Vulkan doit disparaître
        // avant la fenêtre SDL qui la porte.
        const Clock::time_point deviceStart = Clock::now();
        auto gpu = levain::gpu::createGpuDevice(
            *window, {.enableValidation = EnableValidation, .api = options->api});
        if (!gpu)
        {
            levain::core::log("sandbox", levain::core::LogLevel::Critical, "{}",
                              gpu.error().message);
            return 1;
        }
        levain::core::log("sandbox", levain::core::LogLevel::Info, "device créé en {:.1f} ms",
                          secondsBetween(deviceStart, Clock::now()) * 1000.0);

        auto sandbox = createSandbox(*gpu, *options);
        if (!sandbox)
        {
            levain::core::log("sandbox", levain::core::LogLevel::Critical, "{}",
                              sandbox.error().message);
            return 1;
        }

        Loop loop =
            startLoop(*window, *gpu, *sandbox, options->loopSeconds, options->frozenSeconds);
        while (runFrame(loop))
        {
        }
        if (!finishLoop(loop, options->capturePath))
        {
            return 1;
        }
        levain::core::log("sandbox", levain::core::LogLevel::Info, "fenêtre fermée");
#endif
    }
    catch (const std::exception& e)
    {
        std::fputs(e.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }

    return 0;
}
