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
#include "levain/render/gpu_timer.hpp"
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
    /// Un appel de la fonction dans l'image, chronométré sur le GPU : un seul pour les étapes de la
    /// caméra, un par cascade pour les ombres. Chacun a sa moyenne : une lecture manquée ne fausse
    /// que la sienne, pas le découpage de l'image.
    struct TimedCall
    {
        GpuTimer timer;
        GpuTimeAverage average;
        double lastMs = 0.0; ///< Sa dernière mesure, pour la courbe de Tracy.
    };

    struct Entry
    {
        std::string name;
        StageFunction function;
        /// « ombres/terrain » et « GPU ombres/terrain » : le nom de sa zone et de sa courbe dans
        /// Tracy. Internés, jamais libérés : Tracy reconnaît une courbe à l'adresse de son nom.
        const char* label = nullptr;
        const char* plotName = nullptr;
        std::vector<TimedCall> calls; ///< Créés au premier appel, avec `timeFunctions` seulement.
    };

    std::array<std::vector<Entry>, RenderStageNames.size()> entries;
    /// Chronométrer chaque fonction sur le GPU (#295). Désactivé par défaut : NVRHI referme la
    /// passe de rendu à chaque requête de temps, et la mesure coûte alors ce qu'elle mesure (+20 %
    /// sur les ombres de la vallée, engine/render/README.md).
    bool timeFunctions = false;
};

/// Inscrit `function` à `stage`, après celles qui y sont déjà : c'est l'ordre où elles dessinent.
void addStageFunction(RenderStages& stages, RenderStage stage, std::string name,
                      StageFunction function);

/// Appelle les fonctions de `stage`, dans l'ordre de leur inscription, chacune chronométrée sur
/// le GPU et dans Tracy.
void runStage(RenderStages& stages, RenderStage stage, const StageContext& context);

/// Ajoute à Tracy la dernière image de chaque fonction, une courbe par fonction.
void plotStageTimes(const RenderStages& stages);

/// « ombres/terrain 0,30 ms, opaques/herbe 2,10 ms » : le temps GPU moyen d'une image, fonction
/// par fonction, la somme des moyennes de ses appels ; celles qui n'ont pas encore de mesure n'y
/// sont pas.
[[nodiscard]] std::string describeStageTimes(const RenderStages& stages);

/// « ombres : démo, terrain ; opaques : démo ; transparents : aucune » : l'ordre réel de l'image.
[[nodiscard]] std::string describeStages(const RenderStages& stages);

} // namespace levain::render
