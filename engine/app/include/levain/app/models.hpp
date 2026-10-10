#pragma once

// Les modèles glTF sur le GPU (ADR-0029) : leurs meshes, leurs textures, leurs matériaux, et
// l'animation des modèles skinnés. Sortis du sandbox pour que tout programme du moteur s'en serve.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <nvrhi/nvrhi.h>

#include "levain/animation/animation_set.hpp"
#include "levain/animation/animator.hpp"
#include "levain/animation/pose.hpp"
#include "levain/assets/asset_id.hpp"
#include "levain/assets/asset_ref.hpp"
#include "levain/assets/cooked_texture.hpp"
#include "levain/assets/gltf.hpp"
#include "levain/assets/image.hpp"
#include "levain/assets/registry.hpp"
#include "levain/core/error.hpp"
#include "levain/render/gpu_timer.hpp"
#include "levain/render/mesh.hpp"
#include "levain/render/mesh_pass.hpp"
#include "levain/render/skinning.hpp"
#include "levain/render/texture.hpp"

namespace levain::app
{

/// Une texture sur le GPU : sa référence (ADR-0020), et vrai si ce sont des données
/// (rugosité-métal, normal map) et non une couleur. La même image servirait aux deux en deux
/// textures : le format sRGB ou UNORM se choisit à la création.
using TextureKey = std::pair<assets::AssetRef, bool>;

/// Une primitive d'un modèle importé, prête à dessiner.
struct ModelPrimitiveGpu
{
    render::Mesh mesh;
    std::optional<std::uint32_t> material; ///< Indice dans `ModelGpu::materials`.
    /// Un mesh skinné (ADR-0022) : `mesh` est alors son mesh déformé, que le compute réécrit à
    /// chaque image.
    std::optional<render::SkinnedMesh> skin;
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
    render::MaterialDefaults defaults;
    /// Les octets des textures en mémoire vidéo au chargement (critère de #92). Un hot-reload
    /// (ADR-0021) ne le met pas à jour.
    std::size_t textureBytes = 0;
    std::vector<nvrhi::BindingSetHandle> materials;
    /// Pour un modèle skinné : son squelette et ses clips, le clip joué, et la pose et les
    /// matrices du skinning de la dernière image, gardées pour ne pas réallouer.
    std::optional<animation::AnimationSet> animation;
    std::size_t clip = 0;
    /// Avec une locomotion : l'animateur, qui remplace le clip unique (#118).
    std::optional<animation::Animator> animator;
    animation::AnimatorClips animatorClips;
    float lastSeconds = 0.0f; ///< Le temps de l'image précédente : l'animateur et sa mesure.
    animation::Pose pose;
    std::vector<glm::mat4> skinMatrices;
};

/// Les niveaux de mip dans le format qu'attend render. Ils pointent dans `mips`, qui doit leur
/// survivre jusqu'à l'envoi.
[[nodiscard]] std::vector<render::TextureLevel>
textureLevelsOf(const std::vector<assets::Image>& mips);

/// Envoie meshes et textures, et enregistre l'envoi dans `commandList`. La couleur d'un sommet est
/// la couleur de base de son matériau ; sans matériau, c'est sa normale ramenée dans [0, 1], qui
/// rend les formes lisibles sans éclairage (M5.1).
[[nodiscard]] core::Result<ModelGpu>
uploadModel(nvrhi::IDevice& device, nvrhi::ICommandList& commandList, const assets::Model& model,
            const assets::AssetRegistry& registry, const assets::ModelCache& models,
            const render::SkinningPass& skinning, std::uint32_t skinJointCount);

/// Abandonne un envoi en cours : `commandList`, ouverte, est fermée et soumise quand même. Juste
/// détruite, elle fuirait jusqu'à `vkDestroyDevice` avec tout ce qu'elle a enregistré : `open()`
/// l'inscrit dans les ressources de son propre command buffer (NVRHI, vulkan-commandlist.cpp), un
/// cycle que seule la file rompt, quand elle retire le command buffer soumis.
void submitAbandonedUpload(nvrhi::IDevice& device, nvrhi::ICommandList& commandList);

/// Un binding set par matériau du modèle : ses facteurs, et ses textures ou celles par défaut. Les
/// constantes s'envoient par `commandList`.
void bindModelMaterials(nvrhi::IDevice& device, nvrhi::ICommandList& commandList,
                        const render::MeshPass& pass, nvrhi::ISampler& sampler,
                        const assets::Model& model, ModelGpu& gpu);

/// Vrai si l'un des meshes du modèle est skinné : il faut alors son squelette (ADR-0022).
[[nodiscard]] bool isSkinned(const assets::Model& model);

/// L'indice du clip `name`, ou du premier si aucun n'est demandé. Un nom inconnu est un échec, qui
/// liste les clips du modèle.
[[nodiscard]] core::Result<std::size_t> clipIndexOf(const animation::AnimationSet& set,
                                                    const std::optional<std::string>& name);

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
[[nodiscard]] float maxJointSpeedOf(const animation::Pose& before, const animation::Pose& after,
                                    float seconds);

/// Le mouvement que joue un modèle skinné qui a un animateur, à `seconds` (le temps des squelettes,
/// non celui de la scène) : celui d'un personnage, ou une vitesse de démonstration. L'appelant le
/// choisit, modèle par modèle.
using MotionOf = std::function<animation::CharacterMotion(const assets::AssetId&, double seconds)>;

/// L'horloge des squelettes (ADR-0036, décision 9) : le temps de la scène, moins celui qu'a duré
/// l'arrêt de la simulation. Les squelettes suivent la simulation : arrêtée, le renard ne marche
/// plus sur place ; l'eau, l'herbe et les matériaux gardent le temps de la scène.
struct AnimationClock
{
    /// Le temps de la scène passé à l'arrêt, depuis le début.
    double pausedSeconds = 0.0;
    /// Le temps de la scène à l'image précédente ; vide avant la première image, qui n'a rien
    /// avant elle à compter comme arrêté.
    std::optional<double> lastSceneSeconds;
};

/// Le temps des squelettes pour cette image : `sceneSeconds` moins le temps passé à l'arrêt, qui
/// ne s'allonge que d'une image où la simulation est arrêtée. Le temps de la scène que `--time`
/// fige ne change pas d'une image à l'autre, arrêt ou non : le temps des squelettes non plus, et
/// **aussi quand la première image est déjà à l'arrêt** (l'éditeur s'ouvre en Édition) : la
/// première image ne fait que noter le temps de la scène, elle n'y compte pas d'arrêt. À la
/// reprise, il repart d'où il s'était arrêté, sans saut.
[[nodiscard]] double advanceAnimationClock(AnimationClock& clock, double sceneSeconds,
                                           bool simulationPaused);

/// Ce que l'animation des modèles garde d'une image à l'autre : le minuteur GPU du skinning, seul
/// (le critère de coût de #117), la mesure, et l'horloge des squelettes.
struct SkinningState
{
    render::GpuTimer timer;
    SkinningCost cost;
    AnimationClock clock;
};

[[nodiscard]] SkinningState createSkinningState(nvrhi::IDevice& device);

/// Anime les modèles skinnés (ADR-0022) : la pose de leur clip à `seconds` (le temps des
/// squelettes, `advanceAnimationClock`, non celui de la scène), ou celle de leur
/// animateur selon `motionOf`, ses matrices, puis un dispatch par mesh skinné. À enregistrer avant
/// les dessins qui lisent les sommets déformés. Le minuteur s'enregistre dans `commandList`, et le
/// coût de l'image s'ajoute à `state.cost`.
void animateModels(nvrhi::IDevice& device, nvrhi::ICommandList& commandList,
                   std::map<assets::AssetId, ModelGpu>& models,
                   const render::SkinningPass& skinning, SkinningState& state,
                   const MotionOf& motionOf, double seconds);

} // namespace levain::app
