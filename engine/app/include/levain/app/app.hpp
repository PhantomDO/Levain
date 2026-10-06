#pragma once

// Le cycle de vie d'un programme du moteur (ADR-0029) : la fenêtre, le device, le renderer, le
// monde et la boucle, native ou dans le navigateur. Le programme pose sa scène au démarrage, et
// se branche sur chaque image par des points d'accroche.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <flecs.h>
#include <glm/glm.hpp>
#include <nvrhi/nvrhi.h>

#include "levain/app/models.hpp"
#include "levain/assets/asset_ref.hpp"
#include "levain/assets/registry.hpp"
#include "levain/core/error.hpp"
#include "levain/gpu/device.hpp"
#include "levain/input/bindings.hpp"
#include "levain/input/state.hpp"
#include "levain/platform/window.hpp"
#include "levain/render/camera.hpp"
#include "levain/render/environment.hpp"
#include "levain/render/gpu_timer.hpp"
#include "levain/render/light_clusters.hpp"
#include "levain/render/renderer.hpp"
#include "levain/render/skinning.hpp"
#include "levain/render/texture.hpp"
#include "levain/render/tonemap.hpp"
#include "levain/scene/fixed_step.hpp"

namespace levain::app
{

/// Le build des shaders que relance leur hot-reload (ADR-0014) : la commande CMake, le dossier de
/// build qui a produit le programme, et les sources à surveiller. Vide, rien n'est surveillé : le
/// navigateur n'a ni sources ni build.
struct ShaderBuild
{
    std::string cmakeCommand;
    std::filesystem::path buildDir;
    std::filesystem::path sourceDir;
};

/// Ce que le programme règle avant que la boucle ne démarre : sa fenêtre, ses fichiers, et ce que
/// les options communes de la ligne de commande changent (`parseCommonOption`).
struct AppSettings
{
    std::string title = "Levain";
    int width = 1920; ///< En points ; un point vaut un pixel sur la machine de référence.
    int height = 1080;
    /// Les racines d'assets, scannées dans l'ordre (ADR-0019). La première est versionnée et
    /// obligatoire ; les suivantes, des assets téléchargés, sont lues si elles existent.
    std::vector<std::filesystem::path> assetRoots;
    std::filesystem::path bindingsFile; ///< Les liaisons d'entrée (ADR-0017).
    ShaderBuild shaderBuild;

    /// `--seconds N` : la durée de la boucle, sans limite par défaut. Comptée depuis la première
    /// image, pas depuis le lancement : en CI, le démarrage varie de 1 à plus de 10 s.
    double loopSeconds = std::numeric_limits<double>::infinity();
    /// `--steps N` : exactement un pas de simulation par image, puis l'arrêt après N. Ce que fait
    /// la simulation ne dépend plus de la machine.
    std::optional<int> steps;
    /// `--time S` : le temps de la scène, figé. Deux captures au même temps se comparent pixel par
    /// pixel.
    std::optional<double> frozenSeconds;
    std::optional<std::filesystem::path> capturePath;    ///< `--capture f.png` : la dernière image.
    nvrhi::GraphicsAPI api = nvrhi::GraphicsAPI::VULKAN; ///< `--gpu vulkan|webgpu`.
    /// `--sky f.hdr|none` : le ciel qui éclaire la scène ; sans, `defaultSky` s'il existe.
    std::optional<std::filesystem::path> sky;
    std::optional<std::filesystem::path> defaultSky;
    /// Le ciel tourné autour de la verticale ; et son soleil, extrait de l'HDRI pour devenir la
    /// lumière directionnelle qui jette les ombres, ou laissé dans l'éclairage ambiant : la scène
    /// telle que l'éclaire le glTF Sample Viewer.
    float skyTurnDegrees = 0.0f;
    bool extractSkySun = true;
    std::optional<glm::vec3> sunDirection; ///< `--sun x,y,z` : un soleil blanc d'intensité 1.
    float maxAnisotropy = 16.0f;           ///< `--anisotropy N` : le filtrage des modèles.
    render::TonemapSettings tonemap;       ///< `--exposure N`, `--tonemap clip|aces|agx|neutral`.
};

/// Ce que `parseCommonOption` a fait d'une option.
enum class OptionUse : std::uint8_t
{
    Taken,   ///< C'est une option commune, sa valeur est rangée dans les réglages.
    NotMine, ///< Une option du programme.
    Invalid, ///< Une option commune, avec une valeur qu'elle refuse.
};

/// Lit une option commune et sa valeur.
[[nodiscard]] OptionUse parseCommonOption(AppSettings& settings, std::string_view name,
                                          std::string_view value);

/// Un nombre strictement positif, et rien d'autre dans le texte. Vide sinon, NaN compris.
[[nodiscard]] std::optional<double> parsePositive(std::string_view text);

/// Trois nombres séparés par des virgules, « 1.5,-2,0 ». Vide si le texte n'en est pas.
[[nodiscard]] std::optional<glm::vec3> parseVector(std::string_view text);

/// L'usage des options communes, pour le message d'erreur d'un programme.
inline constexpr std::string_view CommonOptionsUsage =
    "[--seconds N] [--steps N] [--time secondes] [--capture fichier.png] [--gpu vulkan|webgpu] "
    "[--sky fichier.hdr|none] [--sun x,y,z] [--exposure N] [--tonemap clip|aces|agx|neutral] "
    "[--anisotropy N]";

struct App;

/// Ce que le programme branche sur chaque image, tout facultatif. Les captures de ces fonctions
/// gardent son état (ses meshes, ses textures) : `App` les détruit avant le renderer et le device,
/// dont cet état dépend.
struct FrameHooks
{
    /// Après l'input, avant les pas de simulation : ce que le joueur demande.
    std::function<void(App&)> frame;
    /// Avant le rendu, dans sa command list : ce que le programme envoie au GPU pour l'image (des
    /// instances, des poses), à `seconds`, le temps de la scène.
    std::function<void(App&, nvrhi::ICommandList&, double seconds)> record;
    /// À la fin de la boucle, avant la capture : ses bilans. `false` fait échouer le programme.
    std::function<bool(App&)> finish;
};

/// Ce que la boucle anime, et que le programme lit ou remplit. Ses champs ne bougent pas en
/// mémoire de toute la boucle : les fonctions d'étape du renderer peuvent les garder par
/// référence.
struct App
{
    platform::Window& window;
    gpu::GpuDevice& gpu;
    AppSettings settings;

    render::Renderer renderer;
    render::SkinningPass skinning;
    SkinningState skinningState;
    nvrhi::SamplerHandle sampler; ///< Celui des modèles, au filtrage des réglages.

    flecs::world world;
    scene::FixedStep fixedStep; ///< L'horloge de la simulation, 60 Hz (ADR-0016).
    /// La caméra du rendu : l'entité, que le programme pose au démarrage, et ce que le rendu en
    /// relit à chaque image, à sa matrice monde interpolée.
    flecs::entity cameraEntity{};
    render::Camera camera;

    /// Les modèles glTF, par GUID (ADR-0019) : le registre des chemins, les modèles en mémoire, et
    /// leur version GPU, déchargée avec eux quand plus aucune entité ne les utilise.
    assets::AssetRegistry registry;
    assets::ModelCache modelCache;
    std::map<assets::AssetId, ModelGpu> models;

    /// L'éclairage de l'image : le soleil (celui du ciel, ou celui des réglages), les lumières que
    /// le programme pose à chaque image, et le fond, là où rien n'est dessiné.
    render::Sun sun;
    std::vector<render::PointLight> lights;
    glm::vec4 background{0.55f, 0.32f, 0.14f, 1.0f}; ///< Une croûte de levain.

    input::Bindings bindings;
    input::InputState input;

    render::GpuTimer frameTimer;     ///< Le temps GPU d'une image entière.
    render::GpuTimeAverage totalGpu; ///< Depuis le début de la boucle, journalisé à la fin.
    int frameCount = 0;

    /// Le dernier champ, donc le premier détruit : l'état du programme part avant le reste.
    FrameHooks hooks;
};

/// Pose la scène du programme, `App` étant prêt, et rend ses points d'accroche. Elle **doit**
/// poser la caméra : `App::cameraEntity`, une entité qui porte un `Transform`, et `App::camera`,
/// son champ et ses plans. Un échec arrête le programme avec son message.
using StartFunction = std::function<core::Result<FrameHooks>(App&)>;

/// Le programme entier : la fenêtre, le device, `start`, la boucle, les bilans et la capture. En
/// natif, rend le code de sortie du processus. Dans le navigateur, rend 0 aussitôt : le device
/// arrive plus tard, et la boucle tourne au rythme de l'écran (ADR-0023) ; un échec s'écrit dans
/// la console.
int runApp(const AppSettings& settings, const StartFunction& start);

} // namespace levain::app
