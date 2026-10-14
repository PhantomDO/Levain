#pragma once

// La scène rendue dans une texture, que l'UI montre (ADR-0036, décision 7) : la matière de la
// fenêtre « Vue » de l'éditeur (morceau 8, editor/view.hpp). Ici, la texture, sa taille qui ne se
// recrée qu'une fois stable, où va la scène à chaque image, et où un curseur la vise ; la fenêtre
// qui la montre n'y est pas.

#include <cstdint>
#include <optional>
#include <vector>

#include <imgui.h>
#include <nvrhi/nvrhi.h>

#include "levain/app/ui_layer.hpp"
#include "levain/ui/ui_pass.hpp"

namespace levain::app
{

/// Une taille en pixels : celle de la texture de la scène, ou celle que l'éditeur en demande.
struct SceneSize
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    /// Une Vue fermée, ou réduite à rien, demande 0 × 0 : aucune scène à rendre.
    [[nodiscard]] constexpr bool empty() const { return width == 0 || height == 0; }

    friend constexpr bool operator==(const SceneSize&, const SceneSize&) = default;
};

/// Où va la scène à cette image.
enum class SceneOutput : std::uint8_t
{
    /// Le rendu d'avant la Vue : la scène remplit l'image de la fenêtre, l'UI est dessinée
    /// par-dessus. Celui du sandbox, de *Rando*, et de l'éditeur panneaux fermés (F1).
    Window,
    /// La scène dans la texture de la Vue. La swapchain est effacée, et seule l'UI y est dessinée.
    Texture,
    /// La Vue est fermée (ou de taille nulle) : aucune scène n'est rendue, la swapchain est
    /// effacée et l'UI seule la couvre.
    Nothing,
};

/// Combien d'images de suite la taille demandée doit rester la même avant que la texture ne la
/// prenne. Pendant un glissé de bord de fenêtre, elle change à chaque image : sans cette attente,
/// l'image HDR et la profondeur du renderer (`ensureHdrTarget`, `ensureDepthTexture`) seraient
/// recréées à chaque image avec la texture. Deux images laissent un glissé finir sans qu'on ait
/// l'impression d'attendre (à 60 Hz, 33 ms).
inline constexpr int StableFramesBeforeResize = 2;

/// La taille demandée, suivie d'une image à l'autre pour savoir quand elle a cessé de bouger.
struct SizeSettling
{
    SceneSize last{};     ///< Ce que l'image d'avant a demandé.
    int framesAtLast = 0; ///< Combien d'images de suite, celle d'avant comprise.
};

/// **Quand recréer la texture** : `current` est la taille de celle qu'on a (vide sans texture),
/// `requested` ce que cette image demande. À appeler une fois par image qui demande une taille.
/// - sans texture, la première taille non vide la crée tout de suite : il n'y a pas d'ancienne
///   image à étirer en attendant ;
/// - une taille déjà celle de la texture ne recrée rien ;
/// - une autre n'est reprise qu'après `stableFrames` images de suite : en attendant, l'ancienne
///   image est montrée étirée à la taille demandée.
/// Une demande vide ne recrée jamais, mais compte : elle fait repartir l'attente.
[[nodiscard]] bool shouldRecreateScene(SizeSettling& settling, const SceneSize& current,
                                       const SceneSize& requested, int stableFrames);

/// Où va la scène, selon ce que l'éditeur a demandé dans `ui` cette image (`requested`, vide s'il
/// n'a rien demandé) et si une texture existe.
/// - rien demandé : `Window`, le rendu d'avant la Vue ;
/// - une taille nulle, ou aucune texture à rendre (sa création a échoué, déjà journalisée) :
///   `Nothing` ;
/// - sinon `Texture`.
[[nodiscard]] constexpr SceneOutput sceneOutputFor(const std::optional<SceneSize>& requested,
                                                   bool hasTexture)
{
    if (!requested)
    {
        return SceneOutput::Window;
    }
    return requested->empty() || !hasTexture ? SceneOutput::Nothing : SceneOutput::Texture;
}

/// Les drapeaux de l'espace d'ancrage des panneaux. Le centre laisse passer l'image et la souris
/// (`PassthruCentralNode`) tant que la scène est derrière, dans la fenêtre ; avec la scène dans
/// une texture, il n'y a plus rien derrière à laisser voir, et c'est la Vue qui l'occupera.
[[nodiscard]] constexpr ImGuiDockNodeFlags dockspaceFlagsFor(SceneOutput output)
{
    return output == SceneOutput::Window ? ImGuiDockNodeFlags_PassthruCentralNode
                                         : ImGuiDockNodeFlags_None;
}

/// Un point de la scène : sa position en pixels depuis le coin haut gauche de la scène, et la
/// taille de la scène où elle se lit (ce que le rayon de la caméra demande : `ndcOfPixel`).
struct ScenePixel
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

/// La taille de la scène où se lit un pixel (`ndcOfPixel`) : la fenêtre pour la scène dans la
/// fenêtre, le rectangle de la Vue pour la scène dans sa texture (étirée), 0 × 0 quand la scène
/// n'est nulle part ou que la taille est nulle. Ce que `scenePixelAt` rend, sans le curseur.
struct SceneExtent
{
    float width = 0.0f;
    float height = 0.0f;
};

[[nodiscard]] SceneExtent sceneExtentOf(SceneOutput output, const ScreenRect& scene,
                                        float windowWidth, float windowHeight);

/// **Où le curseur vise dans la scène** (le picking du sandbox, ADR-0036 décision 7), `cursor`
/// étant en pixels de l'image comme `sceneRect`. La scène dans la fenêtre (`Window`) : le curseur
/// et la fenêtre, comme avant la Vue. Dans la texture de la Vue : le curseur ramené au coin de
/// `sceneRect`, et la taille de ce rectangle, car la scène y est étirée (le rapport suffit au
/// rayon, une fois la texture à la taille du rectangle : pendant les deux ou trois images d'un
/// glissé qui précèdent sa recréation, elle garde l'ancien rapport et le rayon est faussé d'autant,
/// ce que le picking accepte). Rien si le curseur est
/// hors du rectangle (un clic sur un autre panneau ne vise rien), si la scène n'est nulle part
/// (`Nothing`) ou si la taille est nulle : jamais un pixel hors de la scène.
[[nodiscard]] std::optional<ScenePixel> scenePixelAt(SceneOutput output, const ScreenRect& scene,
                                                     float windowWidth, float windowHeight,
                                                     float cursorX, float cursorY);

/// L'état de la scène dans une texture, que garde `App`. Le test de recréation, `created`, compte
/// les textures nées depuis le début de la boucle.
struct SceneView
{
    nvrhi::TextureHandle texture; ///< La scène rendue ; nulle tant qu'aucune Vue n'a rien demandé.
    ImTextureID imageId = 0;      ///< Celui de `texture` pour `ImGui::Image` ; 0 : aucun.
    SizeSettling settling{};
    /// Ce que `ui` a demandé cette image, par `requestSceneTexture` ; la boucle le vide avant
    /// chaque `ui` (`beginSceneRequest`).
    std::optional<SceneSize> requested;
    /// Ce que fait l'image en cours, décidé après `ui` (`settleSceneOutput`). Lue avant `ui` à
    /// l'image suivante, elle est donc celle de l'image d'avant : c'est ainsi que le docking sait,
    /// avant que la Vue ne demande, si la scène était dans la texture.
    SceneOutput output = SceneOutput::Window;
    /// Les images remplacées, que la table de l'UI tient encore : libérées après l'image, quand
    /// plus aucun dessin de cette image ne les montre (`releaseRetiredScenes`).
    std::vector<ImTextureID> retired;
    int created = 0; ///< Les textures créées depuis le début, la première comprise.
};

/// La texture de la scène à `size`, au format de la swapchain : on y rend, puis un shader la lit
/// (`ImGui::Image`). La même qu'ailleurs dans le moteur pour une cible lue ensuite
/// (`ensureHdrTarget`) : au repos en `ShaderResource`, que NVRHI quitte et retrouve toute seule. Au
/// format de la swapchain (UNORM), le tonemapping y encode en sRGB et l'UI montre la texture
/// telle quelle : la conversion ne se fait qu'une fois. Nulle si NVRHI la refuse.
[[nodiscard]] nvrhi::TextureHandle createSceneTexture(nvrhi::IDevice& device, nvrhi::Format format,
                                                      const SceneSize& size);

/// **La demande de la Vue**, à appeler dans `FrameHooks::ui`, avant `ImGui::Image` : la taille que
/// la Vue montre cette image. Rend l'identifiant à donner à `ImGui::Image`, ou rien si la Vue n'a
/// rien à montrer (taille nulle, texture impossible). La texture n'est recréée qu'une fois la
/// taille stable (`shouldRecreateScene`) ; en attendant, c'est l'ancienne qui est rendue, étirée
/// par `ImGui::Image`. Une nouvelle texture existe donc déjà quand `ImGui::Image` la reçoit, et
/// l'ancienne n'est libérée qu'après l'image (`releaseRetiredScenes`).
///
/// Une Vue fermée demande 0 × 0 plutôt que de ne rien demander : rien demandé, c'est la scène dans
/// la fenêtre, comme avant la Vue. Il en va de même chaque image où la Vue n'est pas montrée
/// (cachée derrière un onglet, repliée : `ImGui::Begin` rend faux). Une fois par image : un second
/// appel ferait avancer deux fois l'attente de la taille stable.
[[nodiscard]] std::optional<ImTextureID> requestSceneTexture(SceneView& view,
                                                             nvrhi::IDevice& device,
                                                             ui::UiPass& pass, nvrhi::Format format,
                                                             const SceneSize& size);

/// Début de `ui` : la demande de l'image d'avant ne vaut plus.
void beginSceneRequest(SceneView& view);

/// Fin de `ui` : décide où va la scène (`sceneOutputFor`) et la range dans `view.output`.
void settleSceneOutput(SceneView& view);

/// Libère les images remplacées. À appeler une fois l'image enregistrée (ou sautée) : NVRHI garde
/// vivante la texture tant qu'une command list en vol la tient (`BindingSetDesc::trackLiveness`, et
/// le framebuffer de la command list qui l'a rendue ; `releaseUiTexture`).
void releaseRetiredScenes(SceneView& view, ui::UiPass& pass);

/// Efface la swapchain, quand la scène n'y est pas (`SceneOutput::Texture`, `Nothing`) : sans
/// cela, l'image d'avant resterait sous l'UI.
void clearSwapchain(nvrhi::ICommandList& commandList, nvrhi::ITexture& swapchainImage);

} // namespace levain::app
