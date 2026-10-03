#pragma once

// Le registre des étapes de l'image (ADR-0025) : le renderer appelle, à des moments fixes de
// l'image, les fonctions que l'application y a inscrites. Les dessins du sandbox s'y inscrivent
// comme ceux d'un plugin (le terrain, M5.6) : le renderer ne connaît aucun objet de la scène.

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>
#include <nvrhi/nvrhi.h>

#include "levain/render/culling.hpp"
#include "levain/render/frame.hpp"
#include "levain/render/shadows.hpp"

namespace levain::render
{

/// Les étapes, dans l'ordre de l'image.
enum class RenderStage : std::uint8_t
{
    /// Une fois par cascade d'ombres, avec la vue de la cascade : ce qui projette une ombre.
    ShadowCasters,
    /// Les surfaces opaques, avant le ciel, qui ne remplit que ce qui reste.
    Opaque,
    /// Ce qui se mélange à ce qui est derrière, après le ciel (l'eau, M5.7).
    Transparent,
};

inline constexpr std::array<std::string_view, 3> RenderStageNames{"ombres", "opaques",
                                                                  "transparents"};

/// Ce qu'une fonction d'étape reçoit : où dessiner, et ce que voit la vue de l'étape.
struct StageContext
{
    nvrhi::IDevice& device;
    nvrhi::ICommandList& commandList;
    /// L'image HDR et sa profondeur ; l'atlas des ombres pour `ShadowCasters`.
    nvrhi::IFramebuffer& target;
    const FrameBindings& frame;
    /// La caméra, ou le soleil de la cascade pour `ShadowCasters`.
    glm::mat4 viewProjection{1.0f};
    /// La position de la caméra, dans toutes les étapes : un niveau de détail choisi par elle reste
    /// le même dans l'image et dans ses ombres.
    glm::vec3 cameraPosition{0.0f};
    Frustum frustum;
    const ShadowPass& shadows;
    std::uint32_t cascade = 0;    ///< Pour `ShadowCasters` seulement.
    const Cascade* cascadeView{}; ///< Idem ; nul dans les autres étapes.
    double seconds = 0.0;         ///< Le temps de la scène, pour ce qui s'anime.
};

using StageFunction = std::function<void(const StageContext&)>;

/// Les fonctions inscrites, étape par étape, dans l'ordre de leur inscription.
struct RenderStages
{
    struct Entry
    {
        std::string name;
        StageFunction function;
    };

    std::array<std::vector<Entry>, RenderStageNames.size()> entries;
};

/// Inscrit `function` à `stage`, après celles qui y sont déjà : c'est l'ordre où elles dessinent.
void addStageFunction(RenderStages& stages, RenderStage stage, std::string name,
                      StageFunction function);

/// Appelle les fonctions de `stage`, dans l'ordre de leur inscription.
void runStage(const RenderStages& stages, RenderStage stage, const StageContext& context);

/// « ombres : démo, terrain ; opaques : démo ; transparents : aucune » : l'ordre réel de l'image.
[[nodiscard]] std::string describeStages(const RenderStages& stages);

} // namespace levain::render
