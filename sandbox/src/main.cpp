#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <expected>
#include <filesystem>
#include <format>
#include <map>
#include <memory>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <flecs.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "character_demo.hpp"
#include "crates.hpp"
#include "lake_shore.hpp"

#include "levain/animation/animator.hpp"
#include "levain/app/app.hpp"
#include "levain/app/camera.hpp"
#include "levain/app/load_model.hpp"
#include "levain/app/models.hpp"
#include "levain/assets/asset_ref.hpp"
#include "levain/assets/collision.hpp"
#include "levain/assets/gltf.hpp"
#include "levain/assets/image.hpp"
#include "levain/assets/registry.hpp"
#include "levain/character/walk.hpp"
#include "levain/core/log.hpp"
#include "levain/core/version.hpp"
#include "levain/gpu/device.hpp"
#include "levain/grass/grass_pass.hpp"
#include "levain/input/bindings.hpp"
#include "levain/input/state.hpp"
#include "levain/physics/character.hpp"
#include "levain/physics/components.hpp"
#include "levain/physics/outlines.hpp"
#include "levain/physics/physics.hpp"
#include "levain/physics/physics_world.hpp"
#include "levain/physics/queries.hpp"
#include "levain/platform/window.hpp"
#include "levain/render/camera.hpp"
#include "levain/render/culling.hpp"
#include "levain/render/debug_lines.hpp"
#include "levain/render/light_clusters.hpp"
#include "levain/render/mesh.hpp"
#include "levain/render/mesh_pass.hpp"
#include "levain/render/renderer.hpp"
#include "levain/render/shadows.hpp"
#include "levain/render/skinning.hpp"
#include "levain/render/texture.hpp"
#include "levain/render/tonemap.hpp"
#include "levain/scene/camera_control.hpp"
#include "levain/scene/components.hpp"
#include "levain/scene/fixed_step.hpp"
#include "levain/scene/scene.hpp"
#include "levain/scene/transform.hpp"
#include "levain/terrain/collision.hpp"
#include "levain/terrain/heightmap.hpp"
#include "levain/terrain/terrain_pass.hpp"
#include "levain/water/water_pass.hpp"

namespace
{

using Clock = std::chrono::steady_clock;

double secondsBetween(Clock::time_point start, Clock::time_point end)
{
    return std::chrono::duration<double>(end - start).count();
}

// Les modèles sur le GPU, dans le module app (ADR-0029).
using levain::app::textureLevelsOf;

using levain::app::DrawCount;

/// Les dessins de la démo et ceux des modèles (`app`) : ce que comptent les bilans de #132 et #133.
DrawCount operator+(const DrawCount& first, const DrawCount& second)
{
    return {.drawn = first.drawn + second.drawn,
            .culled = first.culled + second.culled,
            .triangles = first.triangles + second.triangles};
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
    int select = 0; ///< Le clic qui sélectionne un corps (M6.2).
    int jump = 0;   ///< Le saut du personnage de `--view character` (M6.3).
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
    const std::array<std::pair<const char*, int*>, 4> buttons{{{"look_enable", &actions.lookEnable},
                                                               {"sprint", &actions.sprint},
                                                               {"select", &actions.select},
                                                               {"jump", &actions.jump}}};
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

/// Ce que le joueur demande au personnage (M6.3) : la direction des axes de déplacement, tournée
/// selon la caméra qui le suit, la course, et le saut. `--walk` remplace le clavier, pour la CI. Le
/// saut s'ajoute à celui qui attend encore : la marche le consomme au pas suivant, et une image
/// sans pas de simulation ne doit pas le perdre.
void walkInputFrom(levain::character::WalkInput& walk, const levain::input::InputState& input,
                   const CameraActions& actions, const std::optional<glm::vec2>& scripted)
{
    walk.direction = scripted.value_or(
        levain::sandbox::walkDirectionOf(levain::input::axisValue(input, actions.moveRight),
                                         levain::input::axisValue(input, actions.moveForward)));
    walk.run = levain::input::actionHeld(input, actions.sprint);
    walk.jump = walk.jump || levain::input::actionPressed(input, actions.jump);
}

/// Les poses des cubes **dans le monde**, dans `poses`, que le rendu envoie ensuite au GPU. La glu
/// entre scene et render, qui ne se connaissent pas (SPECS §7). Lire `WorldTransform` et non
/// `Transform` : c'est ce qui fait suivre les cubes quand la grille bouge, et ce qui donne aux
/// caisses de la physique leur pose interpolée entre deux pas (ADR-0016).
///
/// La rotation de chacun seulement si `eachTurns` : les cubes de la démo tournent tous ensemble,
/// par la matrice du modèle, et la lire coûterait pour rien 0,08 ms par image en Release, 4,2 ms en
/// Debug, sur 10 000 cubes (mesuré le 04/10/2026).
void gatherCubePoses(const flecs::query<const levain::scene::WorldTransform>& cubes, bool eachTurns,
                     std::vector<levain::render::InstancePose>& poses)
{
    poses.clear();
    cubes.each(
        [&poses, eachTurns](const levain::scene::WorldTransform& transform)
        {
            poses.push_back({.position = levain::scene::worldPosition(transform),
                             .rotation = eachTurns ? levain::scene::worldRotation(transform)
                                                   : glm::quat{1.0f, 0.0f, 0.0f, 0.0f}});
        });
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

/// Les clips de `--locomotion` (« repos,marche,course », par leurs noms), et les vitesses
/// auxquelles le modèle les joue.
levain::core::Result<levain::app::LocomotionClips> locomotionOf(std::string_view names,
                                                                float walkSpeed, float runSpeed)
{
    levain::app::LocomotionClips locomotion{
        .names = {}, .walkSpeed = walkSpeed, .runSpeed = runSpeed};
    std::size_t start = 0;
    for (std::string& name : locomotion.names)
    {
        const std::size_t comma = names.find(',', start);
        name = std::string{names.substr(start, comma - start)};
        if (name.empty())
        {
            return levain::core::makeError(levain::core::ErrorCode::InvalidData,
                                           "--locomotion attend trois clips : repos,marche,course");
        }
        start = comma == std::string_view::npos ? names.size() : comma + 1;
    }
    return locomotion;
}

/// Ce que montre le sandbox : la démo (cubes, sol, modèle), la vue du glTF Sample Viewer (#125,
/// #131), le terrain (M5.6), les caisses de la physique (M6.1), ou le renard qu'on dirige, dans
/// Sponza ou dans la vallée (M6.3).
enum class SandboxView : std::uint8_t
{
    Demo,
    Khronos,
    Terrain,
    Physics,
    Character,
    Hike,
};

/// La vallée de M5.6 est là : seule (`terrain`), ou sous les pas du renard (`hike`).
bool showsValley(SandboxView view)
{
    return view == SandboxView::Terrain || view == SandboxView::Hike;
}

/// Un joueur, le renard, que la caméra suit : dans Sponza (`character`) ou dans la vallée (`hike`).
bool hasPlayer(SandboxView view)
{
    return view == SandboxView::Character || view == SandboxView::Hike;
}

/// Les cubes sont des corps qui tournent : leur rotation se dessine, dès la première image.
bool cubesTurn(SandboxView view)
{
    return view == SandboxView::Physics || showsValley(view) || view == SandboxView::Character;
}

/// Le champ vertical de la caméra du glTF Sample Viewer (PerspectiveCamera.yfov).
constexpr float KhronosViewerFovDegrees = 45.0f;

/// Les options propres au sandbox ; les options communes sont dans `levain::app::AppSettings`.
struct SandboxOptions
{
    /// Un glTF à afficher devant la caméra (M4.1).
    std::optional<std::filesystem::path> modelPath;
    /// Le clip que joue un modèle skinné, par son nom ; le premier par défaut.
    std::optional<std::string> clipName;
    /// Les clips du repos, de la marche et de la course, séparés par des virgules : le modèle
    /// passe de l'un à l'autre selon une vitesse de démonstration (#118).
    std::optional<std::string> locomotion;
    /// L'échelle du modèle (voir `modelPlacement`).
    float modelScale = 2.0f;
    /// `--view khronos` : la scène telle que l'ouvre le glTF Sample Viewer, pour s'y comparer
    /// (#125, #131, tools/khronos-compare.sh). Le modèle seul, à l'origine, sous sa caméra et son
    /// ciel ; ni cubes, ni sol, ni lumières de la démo. `--view terrain` : la vallée de M5.6.
    /// `--view physics` : 1 000 caisses qui tombent sur le sol de la démo (M6.1). `--view
    /// character` et `--view hike` : le renard qu'on dirige, dans Sponza ou dans la vallée (M6.3).
    SandboxView view = SandboxView::Demo;
    /// `--camera x,y,z` : la caméra à cette position, face à −Z. Celle du glTF Sample Viewer, que
    /// tools/khronos-compare.sh relit dans sa page.
    std::optional<glm::vec3> cameraPosition;
    /// `--look lacet,tangage` : où regarde la caméra, en degrés. 0,0 regarde vers −Z, à
    /// l'horizontale. Pour cadrer une capture sans bouger la souris.
    std::optional<glm::vec2> cameraLook;
    /// `--pick x,y` : à la fin de la boucle, avant la capture, sélectionne ce que vise ce pixel,
    /// compté depuis le coin haut gauche. Le clic de la souris, sans souris : pour la CI et les
    /// captures à distance (critère de M6.2).
    std::optional<glm::vec2> pickPixel;
    /// `--walk x,z` : la direction que suit le personnage de `--view character` ou `hike`, au lieu
    /// du clavier.
    std::optional<glm::vec2> walk;
};

/// Ce que montre le sandbox, en plus de ce qu'`app` dessine : une grille de cubes texturés qui
/// tournent, sur un sol qui file jusqu'à l'horizon, où se voit le filtrage anisotrope (M2.2), et
/// ce que chaque vue y ajoute. Les points d'accroche le gardent (`FrameHooks`).
struct DemoScene
{
    levain::app::App& app;
    SandboxOptions options;
    CameraActions actions;
    /// Les cubes, une entité chacun (M3.1), enfants de « grid » (M3.2).
    flecs::query<const levain::scene::WorldTransform> cubes;
    /// Relevées à chaque frame, gardées pour ne pas réallouer.
    std::vector<levain::render::InstancePose> cubePoses;
    levain::render::Mesh cube;
    levain::render::Instances grid;
    levain::render::Mesh ground;
    levain::render::Instances groundInstance; ///< Une seule, sous les cubes.
    nvrhi::TextureHandle checker;
    nvrhi::BindingSetHandle material;
    /// Le personnage de `--view character` (M6.3) : la caméra le suit, le clavier le mène, son
    /// mouvement anime le renard. Vide dans les autres vues.
    flecs::entity player{};
    /// Le modèle qui joue le mouvement du joueur, le renard ; les autres jouent la démo. Un seul :
    /// la vue `character` n'en demande pas d'autre (`modelRequestsOf`).
    std::optional<levain::assets::AssetId> playerModel;
    /// La grille de cubes, le sol et les lumières de couleur ; absents des autres vues.
    bool demoProps = true;
    /// Les cubes tournent tous ensemble sur eux-mêmes (la démo), ou chacun selon sa physique.
    bool spinCubes = true;
    /// Chaque cube a sa propre rotation, à relire à chaque image : les caisses de la physique, et
    /// elles seules. Un drapeau à part, et non `!spinCubes` : la vue khronos garde ses 10 000 cubes
    /// sans les dessiner, et paierait la rotation pour rien.
    bool cubesTurn = false;
    /// Les cubes se dessinent : la grille de la démo, ou les caisses de la physique.
    bool drawCubes = true;
    /// La sélection à la souris (M6.2) : le corps visé par le dernier clic, et la passe qui dessine
    /// son contour. Vide tant que rien n'est sélectionné, ou sans physique.
    flecs::entity selected{};
    std::optional<levain::render::DebugLinesPass> debugLines{};
    /// La vallée de `--view terrain` (M5.6), et ce que ses dessins ont soumis et écarté.
    std::optional<levain::terrain::Heightmap> heightmap;
    std::optional<levain::terrain::TerrainPass> terrain;
    std::optional<levain::water::WaterPass> water; ///< Le lac de la vallée (M5.7).
    std::optional<levain::grass::GrassPass> grass; ///< Son herbe (M5.7).
    levain::grass::GrassStats grassStats;
    levain::terrain::TerrainStats terrainCamera;
    levain::terrain::TerrainStats terrainShadows;
    DrawCount cameraCulling;
    DrawCount shadowCulling; ///< Les quatre cascades ensemble.
};

/// Un modèle que la scène charge : son fichier, sa place, et pour un modèle skinné, le clip qu'il
/// joue ou sa locomotion (`--clip`, `--locomotion`).
struct ModelRequest
{
    std::filesystem::path path;
    levain::scene::Transform placement;
    std::optional<std::string> clip;
    std::optional<std::string> locomotion;
    std::string name = "model"; ///< Le nom de son entité racine.
    /// Sa collision est son maillage affiché, simplifié (ADR-0028) : le décor de `--view
    /// character`.
    bool collides = false;
    /// Enfant du personnage, à `placement` dans son repère : le renard de `--view character`.
    bool followsPlayer = false;
};

/// Charge un modèle par `app` (ADR-0029), avec sa locomotion s'il en a une : celle du renard à
/// la vitesse du joueur, les autres à la vitesse de la démonstration. Un fichier absent l'est le
/// plus souvent faute d'avoir téléchargé les assets de test : l'erreur le dit.
levain::core::Result<levain::app::LoadedModel> loadSandboxModel(levain::app::App& app,
                                                                const ModelRequest& request)
{
    std::error_code missing;
    if (!std::filesystem::exists(request.path, missing))
    {
        // Le cas assuré du paquet web pour Sponza, dont la licence interdit la redistribution.
        return levain::core::makeError(
            levain::core::ErrorCode::InvalidData,
            std::format("{} absent : lancer tools/fetch-assets.sh pour les assets de test "
                        "(Sponza n'est jamais dans le paquet web : sa licence interdit de la "
                        "redistribuer)",
                        request.path.string()));
    }
    levain::app::ModelLoad load{.path = request.path,
                                .placement = request.placement,
                                .name = request.name,
                                .clip = request.clip,
                                .locomotion = std::nullopt};
    if (request.locomotion)
    {
        // Le renard marche et court aux vitesses du personnage, en m/s : la foulée de Fox n'est pas
        // mesurée, ses pieds peuvent glisser un peu. Les autres, à celles de la démonstration.
        auto locomotion =
            request.followsPlayer
                ? locomotionOf(*request.locomotion, levain::sandbox::FoxWalker.walkSpeed,
                               levain::sandbox::FoxWalker.runSpeed)
                : locomotionOf(*request.locomotion, DemoWalkSpeed, DemoRunSpeed);
        if (!locomotion)
        {
            return std::unexpected(locomotion.error());
        }
        load.locomotion = *locomotion;
    }
    return levain::app::loadModel(app, load);
}

/// La collision d'un décor (ADR-0028) : son maillage affiché, sans feuillage, simplifié, dans un
/// corps statique à la place du modèle. Cuite par `levain_cook`, sinon simplifiée ici, et le
/// journal le dit.
void addDecorCollision(flecs::world& world, const levain::assets::AssetRegistry& registry,
                       const levain::app::LoadedModel& loaded, const ModelRequest& request)
{
    const Clock::time_point start = Clock::now();
    levain::assets::CollisionMesh collision =
        levain::assets::loadCollision(registry, loaded.id, *loaded.model);
    auto mesh = std::make_shared<levain::physics::TriangleMesh>();
    mesh->vertices = std::move(collision.vertices);
    mesh->indices = std::move(collision.indices);
    const levain::scene::Transform& placement = request.placement;
    // L'échelle dans les sommets : un corps de Jolt n'en a pas, sa pose n'est qu'une position et
    // une rotation. Sans elle, un décor agrandi aurait la collision de sa taille d'origine.
    for (glm::vec3& vertex : mesh->vertices)
    {
        vertex *= placement.scale;
    }
    levain::core::log("sandbox", levain::core::LogLevel::Info,
                      "collision de {} : {} triangles, chargés en {:.0f} ms", request.name,
                      mesh->indices.size() / 3, secondsBetween(start, Clock::now()) * 1000.0);
    world.entity(std::format("{}_collision", request.name).c_str())
        .set(levain::scene::Transform{.position = placement.position,
                                      .rotation = placement.rotation})
        .set(levain::physics::Collider{.shape = levain::physics::MeshShape{std::move(mesh)}});
}

/// Où le joueur commence, s'il y en a un : au fond de la tranchée de Sponza, ou sur le fond de la
/// vallée, à l'ouest du lac.
std::optional<glm::vec3> playerStartOf(SandboxView view,
                                       const std::optional<levain::terrain::Heightmap>& heightmap,
                                       const levain::terrain::ValleySettings& valley)
{
    if (view == SandboxView::Character)
    {
        return levain::sandbox::PlayerStart;
    }
    if (view == SandboxView::Hike && heightmap)
    {
        return levain::sandbox::hikeStartOf(*heightmap, valley);
    }
    return std::nullopt;
}

/// Place la caméra derrière le joueur (M6.3), par référence : un `set` remettrait son état
/// précédent à jour, et le rendu ne l'interpolerait plus entre deux pas (ADR-0016).
void followPlayer(flecs::entity camera, flecs::entity player)
{
    camera.get_mut<levain::scene::Transform>() =
        levain::sandbox::followCamera(player.get<levain::scene::Transform>().position);
}

/// Les modèles que la scène charge, selon les options : celui de `--model`, à sa place.
std::vector<ModelRequest> modelRequestsOf(const SandboxOptions& options)
{
    std::vector<ModelRequest> requests;
    const std::filesystem::path models{LEVAIN_TEST_ASSETS_DIR "/Models"};
    if (options.view == SandboxView::Character)
    {
        // Sponza à l'échelle 1, dans ses mètres : sa collision est simplifiée à 2 cm près dans le
        // monde (ADR-0028).
        requests.push_back({.path = models / "Sponza/glTF/Sponza.gltf",
                            .placement = {},
                            .clip = std::nullopt,
                            .locomotion = std::nullopt,
                            .name = "sponza",
                            .collides = true,
                            .followsPlayer = false});
    }
    if (hasPlayer(options.view))
    {
        // Le renard, enfant du joueur : 0,01, soit 1,55 m de long et 0,79 m de haut (Fox mesure
        // 155 × 79 unités ; le 0,05 de la démo de M4.5 en faisait un renard de 4 m), et un
        // demi-tour, son avant étant +z quand celui du personnage est −z.
        requests.push_back({.path = models / "Fox/glTF/Fox.gltf",
                            .placement = {.rotation = glm::angleAxis(glm::pi<float>(),
                                                                     glm::vec3{0.0f, 1.0f, 0.0f}),
                                          .scale = glm::vec3{0.01f}},
                            .clip = std::nullopt,
                            .locomotion = "Survey,Walk,Run",
                            .name = "fox",
                            .collides = false,
                            .followsPlayer = true});
        return requests;
    }
    if (options.modelPath)
    {
        requests.push_back({.path = *options.modelPath,
                            .placement = options.view == SandboxView::Khronos
                                             ? levain::scene::Transform{}
                                             : modelPlacement(options.modelScale),
                            .clip = options.clipName,
                            .locomotion = options.locomotion});
    }
    return requests;
}

/// Pose la scène de la vue : ses entités, ses modèles et ses passes, et envoie au GPU le cube, la
/// grille, le sol, la texture du damier et les modèles demandés.
levain::core::Result<std::shared_ptr<DemoScene>>
createDemoScene(levain::app::App& app, const SandboxOptions& options, const CameraActions& actions)
{
    const SandboxView view = options.view;
    nvrhi::IDevice& device = *app.gpu.nvrhi;
    auto image = levain::assets::loadImage(LEVAIN_DATA_DIR "/textures/checker.png");
    if (!image)
    {
        return std::unexpected(image.error());
    }
    const std::vector<levain::assets::Image> mips =
        levain::assets::buildMipChain(std::move(*image));

    // La vallée de --view terrain et hike, avant ses entités : sa physique en a besoin pour le sol
    // (M6.2).
    const levain::terrain::ValleySettings valley;
    std::optional<levain::terrain::Heightmap> heightmap;
    if (showsValley(view))
    {
        heightmap = levain::terrain::valleyOf(valley);
    }

    flecs::world& world = app.world;
    if (view == SandboxView::Physics)
    {
        world.import<levain::physics::PhysicsModule>();
        levain::sandbox::spawnCrates<Cube>(world, GroundSize);
    }
    else if (showsValley(view) && heightmap)
    {
        if (view == SandboxView::Hike)
        {
            world.import<levain::character::WalkModule>();
        }
        world.import<levain::physics::PhysicsModule>();
        levain::sandbox::spawnLakeShoreCrates<Cube>(world, *heightmap, valley);
    }
    else if (view == SandboxView::Character)
    {
        world.import<levain::character::WalkModule>();
        levain::sandbox::spawnCharacterDemo<Cube>(world);
    }
    else
    {
        spawnCubeGrid(world);
    }
    const std::optional<glm::vec3> start = playerStartOf(view, heightmap, valley);
    const flecs::entity player =
        start ? levain::sandbox::spawnPlayer(world, *start) : flecs::entity{};
    // Les modèles demandés, chacun par son GUID, dans les racines qu'`app` a scannées (ADR-0019),
    // envoyés au GPU et instanciés dans le monde par `app`.
    std::optional<levain::assets::AssetId> playerModel;
    for (const ModelRequest& request : modelRequestsOf(options))
    {
        auto loaded = loadSandboxModel(app, request);
        if (!loaded)
        {
            return std::unexpected(loaded.error());
        }
        if (request.followsPlayer && player)
        {
            // Par flecs::Parent, comme toute la hiérarchie (ADR-0015) : le modèle suit le joueur.
            loaded->root.set(flecs::Parent{player});
            playerModel = loaded->id;
        }
        if (request.collides)
        {
            addDecorCollision(world, app.registry, *loaded, request);
        }
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
    if (view == SandboxView::Physics)
    {
        // Devant le tas, un peu au-dessus : on voit les caisses tomber, puis s'étaler.
        cameraEntity.set(levain::scene::Transform{.position = {0.0f, 9.0f, 26.0f}})
            .set(levain::scene::FpsController{.yawDegrees = 0.0f, .pitchDegrees = -14.0f});
    }
    if (view == SandboxView::Terrain)
    {
        // Sur une crête, au coin de la vallée, le regard vers son fond.
        cameraEntity.set(levain::scene::Transform{.position = {30.0f, 140.0f, 480.0f}})
            .set(levain::scene::FpsController{.yawDegrees = -45.0f, .pitchDegrees = -22.0f});
    }
    if (player)
    {
        // La caméra suit le joueur (M6.3) : pas de regard libre, un système la place à chaque pas.
        cameraEntity
            .set(levain::sandbox::followCamera(player.get<levain::scene::Transform>().position))
            .remove<levain::scene::FpsController>();
        world.system("FollowPlayer")
            .kind<levain::scene::PostPhysics>()
            .run([cameraEntity, player](flecs::iter&) { followPlayer(cameraEntity, player); });
    }
    if (options.cameraPosition)
    {
        // Face à −Z : le regard du glTF Sample Viewer à l'ouverture d'un modèle.
        cameraEntity.set(levain::scene::Transform{.position = *options.cameraPosition})
            .set(levain::scene::FpsController{.yawDegrees = 0.0f, .pitchDegrees = 0.0f});
    }
    if (options.cameraLook)
    {
        // Le regard de --look, appliqué au premier pas de simulation par la caméra libre.
        auto& controller = cameraEntity.get_mut<levain::scene::FpsController>();
        controller.yawDegrees = options.cameraLook->x;
        controller.pitchDegrees = options.cameraLook->y;
    }
    // La caméra du rendu (ADR-0029) : la grille occupe la gauche de l'image, le sol file jusqu'à
    // l'horizon à droite, de plus en plus de biais : c'est là que le filtrage trilinéaire seul le
    // rend flou. Position et regard sont ceux de l'entité, et le joueur peut les changer.
    cameraEntity.set(levain::app::CameraLens{
        .verticalFovDegrees = view == SandboxView::Khronos ? KhronosViewerFovDegrees : 60.0f,
        .nearPlane = 0.5f,
        .farPlane = 1000.0f});
    levain::scene::advanceWorld(world, app.fixedStep,
                                0.0f); // les matrices monde, avant le premier envoi
    std::vector<levain::render::InstancePose> cubePoses;
    gatherCubePoses(cubes, cubesTurn(view), cubePoses);

    const nvrhi::CommandListHandle upload = device.createCommandList();
    upload->open();
    levain::render::Mesh cube = levain::render::createCube(device, *upload);
    levain::render::Instances grid = levain::render::createInstances(device, *upload, cubePoses);
    levain::render::Mesh ground =
        levain::render::createPlane(device, *upload, GroundSize, GroundTextureRepeat);
    // Juste sous les cubes, qui tournent sur eux-mêmes : leur demi-diagonale fait 0,87.
    const std::array<levain::render::InstancePose, 1> groundOffset{
        levain::render::InstancePose{.position = {0.0f, -1.0f, 0.0f}}};
    levain::render::Instances groundInstance =
        levain::render::createInstances(device, *upload, groundOffset);
    nvrhi::TextureHandle checker =
        levain::render::createTexture(device, *upload, textureLevelsOf(mips), "checker");
    // Les matériaux avant la fermeture de l'envoi : leurs constantes passent par lui. Le damier des
    // cubes et du sol : non métallique, assez rugueux.
    nvrhi::BindingSetHandle material = levain::render::createMaterialBindings(
        device, *upload, app.renderer.meshPass,
        {.baseColorFactor = glm::vec4{1.0f},
         .metallicFactor = 0.0f,
         .roughnessFactor = 0.8f,
         .normalScale = 1.0f,
         .padding = 0.0f},
        levain::render::withDefaults({.baseColor = checker},
                                     levain::render::createMaterialDefaults(device, *upload)),
        *app.sampler);
    std::optional<levain::terrain::TerrainPass> terrain;
    std::optional<levain::water::WaterPass> water;
    std::optional<levain::grass::GrassPass> grass;
    if (showsValley(view) && heightmap)
    {
        auto pass = levain::terrain::createTerrainPass(
            device, *upload, *heightmap, app.renderer.frame, app.renderer.shadows,
            std::filesystem::path{LEVAIN_TEST_ASSETS_DIR} / "Textures");
        if (!pass)
        {
            levain::app::submitAbandonedUpload(device, *upload);
            return std::unexpected(pass.error());
        }
        terrain = std::move(*pass);
        const levain::water::Lake lake{.center = valley.lakeCenter,
                                       .radius = valley.lakeRadius,
                                       .level = levain::sandbox::LakeLevel};
        auto lakePass = levain::water::createWaterPass(device, *upload, lake, *terrain, *heightmap,
                                                       app.renderer.frame);
        if (!lakePass)
        {
            levain::app::submitAbandonedUpload(device, *upload);
            return std::unexpected(lakePass.error());
        }
        water = std::move(*lakePass);
        auto grassPass = levain::grass::createGrassPass(
            device, *upload, *terrain, *heightmap, levain::sandbox::LakeLevel, app.renderer.frame);
        if (!grassPass)
        {
            levain::app::submitAbandonedUpload(device, *upload);
            return std::unexpected(grassPass.error());
        }
        grass = std::move(*grassPass);
    }
    upload->close();
    device.executeCommandList(upload);
    return std::make_shared<DemoScene>(
        DemoScene{.app = app,
                  .options = options,
                  .actions = actions,
                  .cubes = std::move(cubes),
                  .cubePoses = std::move(cubePoses),
                  .cube = std::move(cube),
                  .grid = std::move(grid),
                  .ground = std::move(ground),
                  .groundInstance = std::move(groundInstance),
                  .checker = std::move(checker),
                  .material = std::move(material),
                  .player = player,
                  .playerModel = playerModel,
                  .demoProps = view == SandboxView::Demo || view == SandboxView::Physics,
                  .spinCubes = view == SandboxView::Demo,
                  .cubesTurn = cubesTurn(view),
                  .drawCubes = view != SandboxView::Khronos,
                  .selected = {},
                  .debugLines = {},
                  .heightmap = std::move(heightmap),
                  .terrain = std::move(terrain),
                  .water = std::move(water),
                  .grass = std::move(grass),
                  .grassStats = {},
                  .terrainCamera = {},
                  .terrainShadows = {},
                  .cameraCulling = {},
                  .shadowCulling = {}});
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

/// Le mouvement que joue un modèle animé : celui du joueur pour le modèle qui le suit (M6.3),
/// sinon la vitesse de démonstration, du repos à la course et retour (#118).
levain::animation::CharacterMotion
motionToPlay(const DemoScene& scene, const levain::assets::AssetId& model, double seconds)
{
    if (scene.playerModel == model && scene.player)
    {
        return scene.player.get<levain::animation::CharacterMotion>();
    }
    return {.speed = demoSpeedAt(seconds)};
}

/// Ce que la démo dessine : les cubes et le sol ; les modèles glTF sont à `app`, dans l'étape
/// « modèles ». `draw(mesh, instances, matériau, modèle)` est appelé pour
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
    if (scene.drawCubes)
    {
        drawIfVisible(scene.cube, scene.grid, *scene.material,
                      scene.spinCubes ? cubeRotation(seconds) : glm::mat4{1.0f});
    }
    if (scene.demoProps)
    {
        drawIfVisible(scene.ground, scene.groundInstance, *scene.material, glm::mat4{1.0f});
    }
}

/// La sélection à la souris, le critère de M6.2 : le rayon de la caméra par le pixel visé, puis le
/// premier corps qu'il touche (ADR-0027). Le rayon voit le monde du dernier pas, pas la pose
/// interpolée qu'on voit : un écart de quelques centimètres sur un corps qui tombe. `false` si la
/// vue n'a pas de physique : il n'y avait rien à viser.
bool selectAt(DemoScene& scene, levain::platform::PixelSize size, glm::vec2 pixel)
{
    const auto* physics = scene.app.world.try_get<levain::physics::PhysicsWorld>();
    if (physics == nullptr || size.width <= 0 || size.height <= 0)
    {
        return false;
    }
    const auto width = static_cast<float>(size.width);
    const auto height = static_cast<float>(size.height);
    const levain::render::CameraRay ray = levain::render::rayThrough(
        scene.app.camera, width / height, levain::render::ndcOfPixel(pixel, width, height));
    const std::optional<levain::physics::RayHit> hit = levain::physics::raycast(
        *physics, {.origin = ray.origin, .direction = ray.direction, .maxDistance = ray.length});
    scene.selected = hit ? scene.app.world.entity(hit->entity) : flecs::entity{};
    levain::core::log("sandbox", levain::core::LogLevel::Info, "sélection : {} à {:.1f} m",
                      scene.selected ? scene.selected.path().c_str() : "rien",
                      hit ? hit->distance : 0.0f);
    return true;
}

/// Le contour du corps sélectionné, en jaune, à sa pose interpolée, celle qu'on voit.
void drawSelection(const DemoScene& scene, const levain::render::StageContext& context)
{
    if (!scene.debugLines || !scene.selected || !scene.selected.is_alive())
    {
        return;
    }
    const auto* collider = scene.selected.try_get<levain::physics::Collider>();
    const auto* world = scene.selected.try_get<levain::scene::WorldTransform>();
    if (collider == nullptr || world == nullptr)
    {
        return;
    }
    std::vector<levain::physics::Segment> segments;
    levain::physics::appendOutline(*collider,
                                   {.position = levain::scene::worldPosition(*world),
                                    .rotation = levain::scene::worldRotation(*world)},
                                   segments);
    std::vector<levain::render::DebugLine> lines;
    lines.reserve(segments.size());
    for (const levain::physics::Segment& segment : segments)
    {
        // Un jaune vif en lumière linéaire, avant le tonemapping.
        lines.push_back({.from = segment.from, .to = segment.to, .color = {6.0f, 5.0f, 0.0f}});
    }
    // Par-dessus ce qui est déjà dessiné : ses arêtes sont sur les faces mêmes du corps, et le test
    // de profondeur les rejetterait à égalité. Le terrain et l'herbe, inscrits après la démo dans
    // l'étape Opaque, le couvrent encore là où ils sont devant.
    levain::render::drawDebugLines(context.commandList, *scene.debugLines, context.target,
                                   context.viewProjection, lines,
                                   levain::render::DebugDepth::OnTop);
}

/// Inscrit les dessins de la démo dans les étapes du renderer (ADR-0025), comme le ferait un plugin
/// : les cubes, le sol et les modèles projettent leur ombre, puis se dessinent parmi les opaques.
/// La scène doit être à sa place définitive : les fonctions la gardent par référence.
void addDemoStages(DemoScene& scene)
{
    using levain::render::RenderStage;
    using levain::render::StageContext;
    levain::render::addStageFunction(
        scene.app.renderer.stages, RenderStage::ShadowCasters, "démo",
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
        scene.app.renderer.stages, RenderStage::Opaque, "démo",
        [&scene](const StageContext& context)
        {
            forEachDraw(scene, context.seconds, context.frustum, scene.cameraCulling,
                        [&](const levain::render::Mesh& mesh,
                            const levain::render::Instances& instances,
                            nvrhi::IBindingSet& material, const glm::mat4& model)
                        {
                            levain::render::drawMesh(
                                context.commandList, scene.app.renderer.meshPass, context.frame,
                                context.target, mesh, instances, material,
                                {.viewProjection = context.viewProjection, .model = model});
                        });
        });
    if (scene.debugLines)
    {
        levain::render::addStageFunction(scene.app.renderer.stages, RenderStage::Opaque,
                                         "sélection", [&scene](const StageContext& context)
                                         { drawSelection(scene, context); });
    }
    if (scene.terrain && scene.heightmap)
    {
        levain::terrain::addTerrainPasses(scene.app.renderer.stages, *scene.terrain,
                                          *scene.heightmap, scene.terrainCamera,
                                          scene.terrainShadows);
    }
    if (scene.grass)
    {
        levain::grass::addGrassPasses(scene.app.renderer.stages, *scene.grass, scene.grassStats);
    }
    if (scene.water)
    {
        levain::water::addWaterPasses(scene.app.renderer.stages, *scene.water);
    }
    levain::core::log("sandbox", levain::core::LogLevel::Info, "étapes du rendu : {}",
                      levain::render::describeStages(scene.app.renderer.stages));
}

/// Pourquoi ces options ne vont pas ensemble, ou rien. Avec le joueur, la caméra le suit et la
/// scène est fixée : `--look` planterait (la caméra n'a plus de regard libre), `--camera` et
/// `--model` seraient ignorés sans un mot.
std::optional<std::string_view> whyNotCompatible(const SandboxOptions& options)
{
    if (hasPlayer(options.view) && (options.cameraLook || options.cameraPosition))
    {
        return "--look et --camera : la caméra de cette vue suit le joueur";
    }
    if (hasPlayer(options.view) && (options.modelPath || options.clipName || options.locomotion))
    {
        return "--model, --clip et --locomotion : cette vue a ses propres modèles";
    }
    if (!hasPlayer(options.view) && options.walk)
    {
        return "--walk : cette vue n'a pas de joueur";
    }
    return std::nullopt;
}

/// Les options du sandbox et les options communes, dans n'importe quel ordre. Vide si les
/// arguments sont invalides.
std::optional<SandboxOptions> parseOptions(std::span<char* const> arguments,
                                           levain::app::AppSettings& settings)
{
    SandboxOptions options;
    for (std::size_t i = 1; i < arguments.size(); i += 2)
    {
        const std::string_view name{arguments[i]};
        if (i + 1 >= arguments.size())
        {
            return std::nullopt;
        }
        const std::string_view value{arguments[i + 1]};
        if (const levain::app::OptionUse use =
                levain::app::parseCommonOption(settings, name, value);
            use != levain::app::OptionUse::NotMine)
        {
            if (use == levain::app::OptionUse::Invalid)
            {
                return std::nullopt;
            }
            continue;
        }
        if (name == "--clip" || name == "--locomotion")
        {
            (name == "--clip" ? options.clipName : options.locomotion) = std::string{value};
            continue;
        }
        if (name == "--pick" || name == "--look" || name == "--walk")
        {
            const std::optional<glm::vec3> pair =
                levain::app::parseVector(std::string{value} + ",0");
            if (!pair)
            {
                return std::nullopt;
            }
            (name == "--pick"   ? options.pickPixel
             : name == "--look" ? options.cameraLook
                                : options.walk) = glm::vec2{pair->x, pair->y};
            continue;
        }
        if (name == "--camera")
        {
            options.cameraPosition = levain::app::parseVector(value);
            if (!options.cameraPosition)
            {
                return std::nullopt;
            }
            continue;
        }
        if (name == "--view")
        {
            if (value != "khronos" && value != "demo" && value != "terrain" && value != "physics" &&
                value != "character" && value != "hike")
            {
                return std::nullopt;
            }
            options.view = value == "khronos"     ? SandboxView::Khronos
                           : value == "terrain"   ? SandboxView::Terrain
                           : value == "physics"   ? SandboxView::Physics
                           : value == "character" ? SandboxView::Character
                           : value == "hike"      ? SandboxView::Hike
                                                  : SandboxView::Demo;
            continue;
        }
        if (name == "--model")
        {
            options.modelPath = std::filesystem::path{value};
            continue;
        }
        if (name == "--model-scale")
        {
            const std::optional<double> scale = levain::app::parsePositive(value);
            if (!scale)
            {
                return std::nullopt;
            }
            options.modelScale = static_cast<float>(*scale);
            continue;
        }
        return std::nullopt;
    }
    if (const std::optional<std::string_view> why = whyNotCompatible(options))
    {
        levain::core::log("sandbox", levain::core::LogLevel::Error, "{}", *why);
        return std::nullopt;
    }
    return options;
}

/// Le nom d'un état du sol, pour le journal.
std::string_view groundName(levain::physics::GroundState state)
{
    switch (state)
    {
    case levain::physics::GroundState::OnGround:
        return "au sol";
    case levain::physics::GroundState::OnSteepGround:
        return "sur une pente trop raide";
    case levain::physics::GroundState::NotSupported:
        return "sans appui";
    case levain::physics::GroundState::InAir:
        break;
    }
    return "en l'air";
}

void logPlayer(flecs::entity player)
{
    const glm::vec3 feet = player.get<levain::scene::Transform>().position;
    const levain::physics::CharacterState* state =
        player.try_get<levain::physics::CharacterState>();
    levain::core::log("sandbox", levain::core::LogLevel::Info,
                      "personnage : pieds à ({:.2f}, {:.2f}, {:.2f}), {}, {:.2f} m/s", feet.x,
                      feet.y, feet.z, state ? groundName(state->ground.state) : "sans personnage",
                      state ? glm::length(state->velocity) : 0.0f);
}

/// Les bilans du sandbox, à la fin de la boucle, avant la capture d'`app` ; `false` si `--pick` n'a
/// rien pu viser.
bool finishDemo(DemoScene& scene)
{
    // Demandé sur une vue sans physique, `--pick` ne vérifierait rien : la boucle échoue (règle
    // n°7). Avant la capture, que le contour de la sélection apparaisse dessus.
    if (scene.options.pickPixel &&
        !selectAt(scene, levain::platform::windowPixelSize(scene.app.window),
                  *scene.options.pickPixel))
    {
        levain::core::log("sandbox", levain::core::LogLevel::Error,
                          "--pick : cette vue n'a pas de physique, rien à sélectionner");
        return false;
    }
    // Lu par la CI (M6.3) : où sont les pieds du personnage, et sur quoi, à la fin de `--walk`.
    if (scene.player)
    {
        logPlayer(scene.player);
    }
    // Le critère de #132 : ce que le frustum culling épargne au GPU, par image.
    const auto perFrame = [&scene](std::uint64_t count)
    { return static_cast<double>(count) / std::max(scene.app.frameCount, 1); };
    const DrawCount camera = scene.cameraCulling + scene.app.modelsCamera;
    const DrawCount shadows = scene.shadowCulling + scene.app.modelsShadows;
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
    // La physique de `--view physics` (M6.1) : combien de corps, et où est la plus haute des
    // caisses. La plus haute part de 18,4 m (`HighestCrateStart`) ; retombées, aucune ne dépasse
    // quelques mètres. La CI le vérifie : un pas qui ne tournerait pas laisserait la grille en
    // l'air.
    if (const auto* physics = scene.app.world.try_get<levain::physics::PhysicsWorld>())
    {
        if (const flecs::entity lake = scene.app.world.lookup("lac"))
        {
            levain::core::log("sandbox", levain::core::LogLevel::Info,
                              "lac : {} caisses dans l'eau",
                              levain::physics::occupantsOf(scene.app.world, lake).size());
        }
        const float highest = levain::sandbox::highestCrate(scene.app.world);
        levain::core::log("sandbox", levain::core::LogLevel::Info,
                          "physique : {} corps ; la caisse la plus haute à y = {:.2f} m",
                          levain::physics::bodyCount(*physics), highest);
    }
    if (scene.terrain)
    {
        const levain::terrain::TerrainStats& camera = scene.terrainCamera;
        const levain::terrain::TerrainStats& shadows = scene.terrainShadows;
        levain::core::log("sandbox", levain::core::LogLevel::Info,
                          "terrain, par image : {:.1f} parcelles dessinées sur {:.1f} et {:.0f} "
                          "triangles, ombres {:.1f} sur {:.1f} (4 cascades) et {:.0f} triangles",
                          perFrame(camera.drawn), perFrame(camera.drawn + camera.culled),
                          perFrame(camera.triangles), perFrame(shadows.drawn),
                          perFrame(shadows.drawn + shadows.culled), perFrame(shadows.triangles));
    }
    if (scene.grass)
    {
        const levain::grass::GrassStats& grass = scene.grassStats;
        levain::core::log("sandbox", levain::core::LogLevel::Info,
                          "herbe, par image : {:.1f} parcelles et {:.0f} brins demandés",
                          perFrame(grass.patches), perFrame(grass.blades));
    }
    const levain::app::SkinningCost& skinning = scene.app.skinningState.cost;
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

    return true;
}

/// Ce que le joueur demande, à chaque image, avant les pas de simulation : la sélection au clic
/// gauche (M6.2), puis la marche du personnage s'il y en a un (M6.3), sinon la caméra libre.
void steerDemo(DemoScene& scene)
{
    levain::app::App& app = scene.app;
    // La souris ne se capture que pendant le regard : sinon on ne pourrait plus rien faire d'autre
    // de la fenêtre. `App` la capture (ADR-0032), sauf panneaux ouverts : on y regarde quand même.
    const bool looking = levain::input::actionHeld(app.input, scene.actions.lookEnable);
    app.mouseCaptureWanted = looking;
    if (!looking && levain::input::actionPressed(app.input, scene.actions.select))
    {
        const levain::platform::CursorPosition cursor =
            levain::platform::cursorPosition(app.window);
        selectAt(scene, levain::platform::windowPixelSize(app.window), {cursor.x, cursor.y});
    }
    if (scene.player)
    {
        walkInputFrom(scene.player.get_mut<levain::character::WalkInput>(), app.input,
                      scene.actions, scene.options.walk);
    }
    else
    {
        app.world.set<levain::scene::FpsInput>(fpsInputFrom(app.input, scene.actions));
    }
}

/// Ce que l'image envoie au GPU avant de dessiner : les instances des cubes, et les lumières de la
/// démo. Les poses des modèles animés sont à `app`, qui les demande à `motionToPlay`.
void recordDemo(DemoScene& scene, nvrhi::ICommandList& commandList, double seconds)
{
    levain::app::App& app = scene.app;
    // Le renderer dessine ce que contient le monde : les positions du tour qui vient de finir.
    gatherCubePoses(scene.cubes, scene.cubesTurn, scene.cubePoses);
    levain::render::updateInstances(commandList, scene.grid, scene.cubePoses);
    app.lights =
        scene.demoProps ? demoLightsAt(seconds) : std::vector<levain::render::PointLight>{};
}

/// La fonction de démarrage du sandbox (ADR-0029) : sa scène, ses étapes de rendu, et ses points
/// d'accroche, qui la gardent jusqu'à la fin.
levain::core::Result<levain::app::FrameHooks> startSandbox(levain::app::App& app,
                                                           const SandboxOptions& options)
{
    auto actions = cameraActionsOf(app.bindings);
    if (!actions)
    {
        return std::unexpected{std::move(actions.error())};
    }
    auto created = createDemoScene(app, options, *actions);
    if (!created)
    {
        return std::unexpected{std::move(created.error())};
    }
    const std::shared_ptr<DemoScene> scene = std::move(*created);
    if (app.world.has<levain::physics::PhysicsWorld>())
    {
        // Une vue avec physique : de quoi dessiner le contour de la sélection.
        auto lines =
            levain::render::createDebugLinesPass(*app.gpu.nvrhi, levain::render::sceneTargetInfo());
        if (lines)
        {
            scene->debugLines = std::move(*lines);
        }
        else
        {
            levain::core::log("sandbox", levain::core::LogLevel::Error,
                              "pas de lignes de debug : {}", lines.error().message);
        }
    }
    addDemoStages(*scene);
    levain::core::log("sandbox", levain::core::LogLevel::Info,
                      "clic droit pour regarder, ZQSD ou WASD pour avancer");
    return levain::app::FrameHooks{
        .frame = [scene](levain::app::App&) { steerDemo(*scene); },
        .record = [scene](levain::app::App&, nvrhi::ICommandList& commandList, double seconds)
        { recordDemo(*scene, commandList, seconds); },
        .finish = [scene](levain::app::App&) { return finishDemo(*scene); },
        .motionOf = [scene](const levain::assets::AssetId& id, double seconds)
        { return motionToPlay(*scene, id, seconds); }};
}

} // namespace

int main(int argc, char** argv)
{
    // std::print et std::format peuvent lever : format_error sur une chaîne de format
    // invalide, system_error si l'écriture échoue. On rattrape au sommet (ADR-0008).
    try
    {
        // Les chemins viennent de sandbox/CMakeLists.txt : le dépôt en natif, le système de
        // fichiers préchargé dans le navigateur, sans sources de shaders à surveiller.
        levain::app::AppSettings settings;
        settings.title = "Levain";
        settings.assetRoots = {LEVAIN_DATA_DIR, LEVAIN_TEST_ASSETS_DIR};
        settings.bindingsFile = LEVAIN_DATA_DIR "/input.cfg";
        settings.shaderBuild = {.cmakeCommand = LEVAIN_CMAKE_COMMAND,
                                .buildDir = LEVAIN_BUILD_DIR,
                                .sourceDir = LEVAIN_SHADER_SOURCE_DIR};
        settings.defaultSky = std::filesystem::path{LEVAIN_DEFAULT_SKY};
        const std::optional<SandboxOptions> options =
            parseOptions(std::span{argv, static_cast<std::size_t>(argc)}, settings);
        if (!options)
        {
            std::println(stderr,
                         "usage : levain_sandbox {} [--model fichier.gltf [--clip nom | "
                         "--locomotion repos,marche,course] [--model-scale N]] "
                         "[--view demo|khronos|terrain|physics|character|hike] [--camera x,y,z] "
                         "[--look lacet,tangage] [--pick x,y] [--walk x,z]",
                         levain::app::CommonOptionsUsage);
            return 2;
        }
        if (options->view == SandboxView::Khronos)
        {
            // Le ciel tel que l'éclaire le glTF Sample Viewer : son soleil reste dans l'IBL, sans
            // lumière directionnelle, et le ciel est tourné de 90° (sa rotation par défaut, « +Z
            // »).
            settings.skyTurnDegrees = 90.0f;
            settings.extractSkySun = false;
        }

        std::print("Levain {} — {} — __cplusplus {}\n", levain::core::version(),
                   levain::core::toolchain(), __cplusplus);
        return levain::app::runApp(settings, [options = *options](levain::app::App& app)
                                   { return startSandbox(app, options); });
    }
    catch (const std::exception& e)
    {
        std::fputs(e.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
}
