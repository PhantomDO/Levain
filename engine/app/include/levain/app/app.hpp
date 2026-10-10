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

#include "levain/app/camera.hpp"
#include "levain/app/models.hpp"
#include "levain/app/ui_layer.hpp"
#include "levain/assets/asset_ref.hpp"
#include "levain/assets/gltf.hpp"
#include "levain/assets/registry.hpp"
#include "levain/core/error.hpp"
#include "levain/gpu/device.hpp"
#include "levain/input/bindings.hpp"
#include "levain/input/state.hpp"
#include "levain/platform/input_script.hpp"
#include "levain/platform/process.hpp"
#include "levain/platform/window.hpp"
#include "levain/render/camera.hpp"
#include "levain/render/environment.hpp"
#include "levain/render/gpu_timer.hpp"
#include "levain/render/light_clusters.hpp"
#include "levain/render/mesh.hpp"
#include "levain/render/renderer.hpp"
#include "levain/render/skinning.hpp"
#include "levain/render/texture.hpp"
#include "levain/render/tonemap.hpp"
#include "levain/scene/components.hpp"
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

/// Le système où tourne ce programme : la cible du build, pas l'hôte de CMake, qui est Linux pour
/// un exe Windows compilé dans une distro. Un paramètre plutôt qu'un `#ifdef` dans la fonction : le
/// test couvre ainsi les deux depuis Linux.
enum class ExeSystem : std::uint8_t
{
    Linux,
    Windows,
};

/// Le système pour lequel ce programme est compilé.
inline constexpr ExeSystem CompiledSystem =
#ifdef _WIN32
    ExeSystem::Windows;
#else
    ExeSystem::Linux;
#endif

/// La distro WSL où un exe Windows a été configuré, et le winsysroot de ce shell (ADR-0035) : des
/// propriétés de l'arbre de build, que `levain_app` reçoit à la compilation, pas du programme.
/// Vides hors d'une distro, et pour l'exe d'une CI.
struct WslBuild
{
    std::string distro;
    std::string winsysroot;
};

/// La cible CMake qui compile les shaders (`cmake/LevainShaders.cmake`).
inline constexpr std::string_view ShaderTarget = "levain_shaders";

/// Une commande à lancer, et les variables qu'elle ajoute à l'environnement du programme.
struct ShaderReloadCommand
{
    std::vector<std::string> arguments;
    std::vector<levain::platform::EnvironmentVariable> environment;
};

/// La commande qui recompile les shaders (ADR-0014), à lancer dans le dossier de build qui a
/// produit le programme :
/// - sous Linux, `cmake --build` directement ;
/// - sous Windows, `cmake` et `slangc` sont des programmes Linux qu'il ne sait pas lancer : si
///   l'exe a été compilé dans une distro, `wsl.exe` y relance `cmake --build`, dans le dossier de
///   build de la distro d'où l'exe vient (ADR-0035, décision 5) ;
/// - sous Windows, sans distro (l'exe de la CI), un refus (`Unsupported`) qui dit pourquoi : le
///   programme continue de tourner, sans recharger.
[[nodiscard]] levain::core::Result<ShaderReloadCommand>
shaderReloadCommand(const ShaderBuild& build, ExeSystem system, const WslBuild& wsl);

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
    /// `--steps N` : exactement un pas de simulation par image (aucun, à l'arrêt : `--steps` compte
    /// des images), puis l'arrêt après N. Ce que fait la simulation ne dépend plus de la machine.
    std::optional<int> steps;
    /// `--time S` : le temps de la scène, figé. Deux captures au même temps se comparent pixel par
    /// pixel.
    std::optional<double> frozenSeconds;
    std::optional<std::filesystem::path> capturePath; ///< `--capture f.png` : la dernière image.
    /// `--gpu vulkan|d3d12|webgpu` ; sans l'option, celui de la cible (`gpu::DefaultBackend`).
    nvrhi::GraphicsAPI api = gpu::DefaultBackend;
    /// Direct3D 12 seulement : le GPU, ou WARP (`gpu::Adapter`). Pas d'option : les tests le
    /// posent.
    gpu::Adapter adapter = gpu::Adapter::HighPerformance;
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
    /// `--ui on|off` : les panneaux de debug ouverts dès le départ, pour la CI et les captures.
    /// F1 les ouvre et les ferme (ADR-0032).
    bool showUiPanels = false;
    /// `--input-script f` : des événements écrits dans un fichier, rejoués image par image en plus
    /// des vrais (ADR-0036). `runApp` le lit avant d'ouvrir la fenêtre (`inputScriptOf`), et un
    /// script illisible ou vide l'arrête.
    std::optional<std::filesystem::path> inputScriptFile;
    /// L'API des tests : le script lui-même, que la boucle rejoue (`platform::addScriptedEvents`).
    /// Sans `--steps` ni `--seconds`, il mène la boucle, qui s'arrête après sa dernière image ;
    /// avec l'un d'eux, la première fin gagne, et un script que la boucle n'a pas fini de jouer
    /// fait échouer le programme (règle n°7). Sans script, l'input n'est pas touché. **Pour un
    /// résultat qui se reproduise**, `--steps N` avec N au moins la longueur du script : sans lui,
    /// le monde avance du temps réel de chaque image, qui change d'une machine à l'autre.
    std::optional<platform::InputScript> inputScript;
};

/// Le script d'input des réglages : celui du fichier `--input-script`, qui l'emporte, lu ici ;
/// sinon celui de l'API de test ; rien sans l'un ni l'autre. Un fichier illisible ou refusé, un
/// script vide (écrit à la main ou lu) sont des erreurs : `runApp` s'arrête avant d'ouvrir une
/// fenêtre plutôt que de jouer sans ce que le test attend (règle n°7).
[[nodiscard]] core::Result<std::optional<platform::InputScript>>
inputScriptOf(const AppSettings& settings);

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
    "[--seconds N] [--steps N] [--time secondes] [--capture fichier.png] "
    "[--gpu vulkan|d3d12|webgpu] "
    "[--sky fichier.hdr|none] [--sun x,y,z] [--exposure N] [--tonemap clip|aces|agx|neutral] "
    "[--anisotropy N] [--ui on|off] [--input-script fichier]";

struct App;

/// Les dessins d'une passe depuis le début de la boucle : soumis au GPU, écartés par le frustum
/// culling (#132), et les triangles soumis, instances comprises (#133).
struct DrawCount
{
    std::uint64_t drawn = 0;
    std::uint64_t culled = 0;
    std::uint64_t triangles = 0;
};

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
    /// Le mouvement que jouent les modèles qui ont une locomotion (`loadModel`). Sans, ils restent
    /// au repos.
    MotionOf motionOf;
    /// Après les pas de simulation, entre `ImGui::NewFrame` et `ImGui::Render` : les fenêtres
    /// ImGui du programme (ADR-0032), son HUD comme ses panneaux. Les panneaux du moteur, ouverts
    /// par F1, passent juste avant.
    std::function<void(App&)> ui{};
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
    /// **La simulation à l'arrêt** (ADR-0036, décision 1), que pose l'éditeur en mode Édition,
    /// lue avant `advanceWorld` : aucun pas, `RenderAlpha` à 1 (l'image montre le `Transform`
    /// tapé), l'accumulateur de `fixedStep` ne reçoit pas le temps de l'image, de sorte que le
    /// retour au jeu ne rejoue pas de rafale. `progress` tourne encore, et `--steps` compte des
    /// images : à l'arrêt, il ne joue aucun pas. Les squelettes s'arrêtent avec elle
    /// (`skinningState.clock`) ; l'eau, l'herbe et les matériaux gardent le temps de la scène.
    bool simulationPaused = false;
    /// La caméra du rendu de la dernière image, relue sur l'unique entité qui porte un
    /// `CameraLens` (camera.hpp) : le programme la lit, pour viser à la souris par exemple.
    render::Camera camera;

    /// Les modèles glTF, par GUID (ADR-0019) : le registre des chemins, les modèles en mémoire, et
    /// leur version GPU, déchargée avec eux quand plus aucune entité ne les utilise.
    assets::AssetRegistry registry;
    assets::ModelCache modelCache;
    std::map<assets::AssetId, ModelGpu> models;
    /// Ce que l'étape « modèles » dessine (`loadModel`) : toute entité qui porte un `MeshRef`, à sa
    /// matrice monde, une instance chacune.
    flecs::query<const assets::MeshRef, const scene::WorldTransform> modelParts;
    render::Instances modelInstance; ///< Une seule, à l'origine : la matrice monde place.
    /// Pour une primitive sans matériau : blanc, non métallique, assez rugueux.
    nvrhi::BindingSetHandle defaultMaterial;
    DrawCount modelsCamera;  ///< Les dessins des modèles, par la caméra.
    DrawCount modelsShadows; ///< Et par les quatre cascades d'ombres ensemble.
    /// Une entité à `MeshRef` dont le modèle n'a pas été chargé par `loadModel` : signalée une
    /// fois.
    bool warnedUnloadedModel = false;
    /// Les entités qui portent un `CameraLens` : une requête gardée, relue à chaque image.
    flecs::query<const CameraLens, const scene::WorldTransform> cameras;

    /// L'éclairage de l'image : le soleil (celui du ciel, ou celui des réglages), les lumières que
    /// le programme pose à chaque image, et le fond, là où rien n'est dessiné.
    render::Sun sun;
    std::vector<render::PointLight> lights;
    glm::vec4 background{0.55f, 0.32f, 0.14f, 1.0f}; ///< Une croûte de levain.

    /// Les liaisons et l'état de l'input, que `app` recopie à chaque image dans le singleton
    /// `PlayerInput` du monde (player_input.hpp), avec les appuis qu'aucun pas n'a encore vus.
    input::Bindings bindings;
    input::InputState input;

    render::GpuTimer frameTimer;     ///< Le temps GPU d'une image entière.
    render::GpuTimeAverage totalGpu; ///< Depuis le début de la boucle, journalisé à la fin.
    int frameCount = 0;

    /// **`App` possède la capture de la souris** (ADR-0032) : le programme pose ce qu'il veut,
    /// et la boucle seule capture, sauf quand les panneaux de debug sont ouverts. Le programme lit
    /// l'état réel dans `mouseCaptured`, celui de la fin de l'image précédente : la boucle capture
    /// après `FrameHooks::frame`.
    bool mouseCaptureWanted = false;
    bool mouseCaptured = false;
    /// ImGui et ses panneaux. Avant les points d'accroche, donc détruite après eux, et avec
    /// `App`, avant le device dont elle tient des ressources (ADR-0029).
    UiLayer ui;

    /// Le dernier champ, donc le premier détruit : l'état du programme part avant le reste.
    FrameHooks hooks;
};

/// Le monde d'une application, sans fenêtre ni GPU : la scène, les assets, les composants d'`app`
/// décrits (ADR-0034), et `PlayerInput` enregistré (`createApp` le pose ensuite). Appelée par
/// `createApp`, et par le test des composants décrits, qui voit ainsi le monde de l'application.
void prepareAppWorld(flecs::world& world);

/// Pose la scène du programme, `App` étant prêt, et rend ses points d'accroche. Elle **doit**
/// poser la caméra : une seule entité qui porte un `CameraLens` (camera.hpp) et un `Transform`.
/// Un échec arrête le programme avec son message.
using StartFunction = std::function<core::Result<FrameHooks>(App&)>;

/// Le programme entier : la fenêtre, le device, `start`, la boucle, les bilans et la capture. En
/// natif, rend le code de sortie du processus. Dans le navigateur, rend 0 aussitôt : le device
/// arrive plus tard, et la boucle tourne au rythme de l'écran (ADR-0023) ; un échec s'écrit dans
/// la console.
int runApp(const AppSettings& settings, const StartFunction& start);

} // namespace levain::app
