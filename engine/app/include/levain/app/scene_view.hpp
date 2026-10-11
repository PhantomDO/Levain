#pragma once

// La scène rendue dans une texture, que l'UI montre (ADR-0036, décision 7) : la matière de la
// fenêtre « Vue » de l'éditeur (morceau 8). Ici, la texture, sa taille qui ne se recrée qu'une fois
// stable, et où va la scène à chaque image ; la fenêtre qui la montre n'y est pas.

#include <cstdint>
#include <optional>
#include <vector>

#include <imgui.h>
#include <nvrhi/nvrhi.h>

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

/// L'état de la scène dans une texture, que garde `App`.
struct SceneView
{
    nvrhi::TextureHandle texture; ///< La scène rendue ; nulle tant qu'aucune Vue n'a rien demandé.
    ImTextureID imageId = 0;      ///< Celui de `texture` pour `ImGui::Image` ; 0 : aucun.
    SizeSettling settling{};
    /// Ce que `ui` a demandé cette image ; la boucle le vide avant
    /// chaque `ui` (`beginSceneRequest`).
    std::optional<SceneSize> requested;
    /// Ce que fait l'image en cours, décidé après `ui` (`settleSceneOutput`). Lue avant `ui` à
    /// l'image suivante, elle est donc celle de l'image d'avant.
    SceneOutput output = SceneOutput::Window;
    /// Les images remplacées, que la table de l'UI tient encore : libérées après l'image, quand
    /// plus aucun dessin de cette image ne les montre (`releaseRetiredScenes`).
    std::vector<ImTextureID> retired;
    int created = 0; ///< Les textures créées depuis le début, la première comprise.
};

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
