#pragma once

// Le rendu d'ImGui par NVRHI (M7.1, ADR-0032), adapté du renderer de Donut (`imgui_nvrhi.cpp`,
// NVIDIA, MIT) : sa classe devient des fonctions libres, comme nos passes (ADR-0011). Une passe
// de plus, chronométrée comme les autres, sous Vulkan comme sous WebGPU : ImGui ne donne que des
// triangles, des rectangles de découpe et des textures.

#include <cstdint>
#include <optional>
#include <string_view>
#include <unordered_map>

#include <imgui.h>
#include <nvrhi/nvrhi.h>

#include "levain/core/error.hpp"

namespace levain::ui
{

/// Une texture de l'UI sur le GPU, avec le binding set qui la lit : celle qu'ImGui a demandée
/// (l'atlas des polices), ou celle qu'un programme montre par `ImGui::Image`.
struct UiTexture
{
    nvrhi::TextureHandle texture;
    nvrhi::BindingSetHandle bindings;
    /// Vrai pour une texture que le programme a enregistrée (`registerUiTexture`) : elle ne part de
    /// la table que par `releaseUiTexture`, jamais par un `WantDestroy` d'ImGui, qui ne connaît pas
    /// son identifiant.
    bool registered = false;
};

/// La passe de l'UI, construite pour une cible (l'image finale).
struct UiPass
{
    nvrhi::ShaderHandle vertexShader;
    nvrhi::ShaderHandle pixelShader;
    nvrhi::InputLayoutHandle inputLayout;
    nvrhi::BindingLayoutHandle layout;
    nvrhi::GraphicsPipelineHandle pipeline;
    nvrhi::SamplerHandle sampler;
    nvrhi::BufferHandle constants;
    /// Les sommets et les index de toutes les fenêtres, réunis : ils grandissent au besoin, et ne
    /// rapetissent pas.
    nvrhi::BufferHandle vertices;
    nvrhi::BufferHandle indices;
    /// Les textures de l'UI, par identifiant : celles qu'ImGui a demandées (`ImTextureData`) et
    /// celles que le programme a enregistrées. Un seul compteur pour les deux, qui ne rend jamais
    /// un identifiant : un `ImDrawList` périmé ne montre pas une autre texture que la sienne.
    std::unordered_map<ImTextureID, UiTexture> textures;
    ImTextureID nextTextureId = 1;
    /// La cible est sRGB : les couleurs sont linéarisées dans le shader (`linearOnSrgbTarget`).
    bool linearizeColors = false;
};

/// Ce qu'une image d'UI a dessiné : la CI échoue si elle n'a rien dessiné (ADR-0032).
struct UiDrawStats
{
    std::uint32_t draws = 0;
    std::uint32_t vertices = 0;
    /// Les commandes non dessinées : leur découpe, bornée à l'image, est vide (une fenêtre hors de
    /// l'écran).
    std::uint32_t clipped = 0;
    /// Les commandes sautées parce que leur texture n'est pas (ou plus) dans la table : une image
    /// libérée avant d'être dessinée, ou un identifiant jamais enregistré. Un bug du programme :
    /// l'assertion en Debug, et ici l'erreur au journal, jamais un silence (règle n°7).
    std::uint32_t unknownTextures = 0;
};

/// La passe, pour une cible du format `target`.
[[nodiscard]] core::Result<UiPass> createUiPass(nvrhi::IDevice& device,
                                                const nvrhi::FramebufferInfo& target);

/// Une cible sRGB convertit en écrivant : les couleurs d'ImGui, déjà en sRGB, doivent alors être
/// linéarisées, sinon l'UI est délavée (`linearOnSrgbTarget`). Le format se lit sur la cible, pas
/// sur la plateforme : la swapchain native est sRGB, son repli ne l'est pas.
[[nodiscard]] bool linearOnSrgbTarget(nvrhi::Format format);

/// Le rectangle de découpe d'une commande d'ImGui, borné à la cible. ImGui en donne qui sortent de
/// l'image, ou négatifs : passés tels quels, c'est une erreur de validation, sous WebGPU comme sous
/// Vulkan. Rien si le rectangle borné est vide : la commande ne se dessine pas.
[[nodiscard]] std::optional<nvrhi::Rect>
clampScissorToTarget(const ImVec4& clip, std::uint32_t width, std::uint32_t height);

/// Crée, met à jour ou détruit les textures qu'ImGui demande (`ImDrawData::Textures`, ImGui 1.92) :
/// c'est ce qui lui permet de rastériser ses polices à la taille de l'écran. Une mise à jour
/// renvoie la texture **entière** : le `writeTexture` de NVRHI écrit une sous-ressource entière.
/// Une texture détruite part avec son binding set, sans quoi le cache garderait chaque ancien
/// atlas.
void updateUiTextures(nvrhi::IDevice& device, nvrhi::ICommandList& commandList, UiPass& pass,
                      ImVector<ImTextureData*>& textures);

/// Pourquoi l'UI ne peut pas montrer une texture de cette description, ou vide si elle le peut. Son
/// shader lit un `Texture2D<float4>` par un échantillonneur qui filtre : une texture
/// multi-échantillon, un cube, une profondeur, un format entier, une texture que NVRHI ne laisse
/// pas à un shader (`isShaderResource`) ou un flottant sur 32 bits (WebGPU ne le filtre pas) ferait
/// une erreur de validation au premier dessin, ou à la création du bind group. Mieux vaut le
/// refuser à l'enregistrement.
[[nodiscard]] std::string_view uiTextureRefusal(const nvrhi::TextureDesc& desc);

/// Montre à l'UI une texture qu'elle ne possède pas, la scène rendue dans la Vue (ADR-0036,
/// décision 7) : le binding set se construit comme celui de `createTexture`, et l'identifiant rendu
/// va à `ImGui::Image`. La table garde une référence à la texture, qui vit donc jusqu'à
/// `releaseUiTexture` au moins ; l'appelant peut lâcher la sienne.
///
/// Les couleurs : la passe **ne touche pas aux texels**, elle les multiplie par la couleur du
/// sommet. C'est la cible qui convertit, une seule fois, en écrivant (`linearOnSrgbTarget`). Une
/// texture au format de la cible est donc juste : une texture sRGB sur une cible sRGB (le matériel
/// décode en lisant, la cible encode en écrivant), une texture UNORM qui garde des valeurs déjà
/// encodées sur une cible UNORM. Une texture UNORM aux valeurs encodées sur une cible sRGB serait
/// convertie deux fois, et délavée (le test `levain_ui_gpu` le mesure).
///
/// L'alpha : le mélange de la passe est celui d'ImGui (`SrcAlpha`, `InvSrcAlpha`), l'alpha du texel
/// compris. Une image montrée doit être **opaque** (alpha 1) : la sortie du tonemap l'est
/// (`shaders/tonemap.slang` écrit 1.0), pas toute texture rendue.
///
/// L'état de la texture : NVRHI ne le devine pas. Deux façons de le lui dire, qui n'ont pas la même
/// fin. Avec un état initial gardé (`setKeepInitialState`), la passe la fait passer à
/// `ShaderResource` toute seule (`setGraphicsState`) et la command list la remet dans son état
/// initial en se fermant. Suivie par la command list (`beginTrackingTextureState`), elle reste en
/// `ShaderResource` après `close()` : c'est à l'appelant de fixer l'état où il la veut ensuite
/// (`setPermanentTextureState`, ou `endTrackingTextureState`).
///
/// Jamais la texture où l'UI dessine : la lire et y écrire dans la même passe est une boucle de
/// rétroaction, que les API interdisent et que `registerUiTexture` ne détecte pas (la passe ne
/// connaît que le format de sa cible).
///
/// Une erreur, et pas une assertion, pour une texture nulle ou que l'UI ne sait pas lire
/// (`uiTextureRefusal`) : le programme peut les recevoir d'une ressource qu'il a créée.
[[nodiscard]] core::Result<ImTextureID> registerUiTexture(nvrhi::IDevice& device, UiPass& pass,
                                                          nvrhi::ITexture* texture);

/// Retire de la table une texture enregistrée. Aucune attente du GPU : les command lists qui ont
/// dessiné la texture dans l'image en cours, ou dans une image en vol, tiennent chacune une
/// référence au binding set (NVRHI : `referencedResources` sous Vulkan et Direct3D 12 ; le bind
/// group de WebGPU garde la texture). Cette garde est celle de `BindingSetDesc::trackLiveness`
/// (nvrhi.h, « Enables automatic liveness tracking… », vrai par défaut), que `registerUiTexture`
/// pose explicitement. La texture n'est vraiment détruite qu'une fois la dernière terminée : la
/// libérer après `recordUi` est sûr, et c'est ce que fait la Vue pour l'ancienne image.
/// L'identifiant ne revient pas : dessiner un identifiant libéré est une assertion en Debug, une
/// commande sautée et une erreur au journal en Release (`UiDrawStats::unknownTextures`). Libérer
/// deux fois, ou un identifiant d'ImGui (l'atlas des polices), est le même bug, traité de même.
void releaseUiTexture(UiPass& pass, ImTextureID id);

/// Enregistre l'UI d'`drawData` dans `target` : les textures d'abord, puis les sommets, puis une
/// commande de dessin par commande d'ImGui, chacune avec sa texture et son rectangle de découpe.
UiDrawStats recordUi(nvrhi::IDevice& device, nvrhi::ICommandList& commandList, UiPass& pass,
                     ImDrawData& drawData, nvrhi::IFramebuffer& target);

/// Libère toutes les textures, celles qu'un programme a enregistrées comprises (leurs identifiants
/// ne valent plus rien), et le dit à ImGui. À l'arrêt, avant le device.
void destroyUiTextures(UiPass& pass, ImVector<ImTextureData*>& textures);

} // namespace levain::ui
