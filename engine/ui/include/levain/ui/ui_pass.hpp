#pragma once

// Le rendu d'ImGui par NVRHI (M7.1, ADR-0032), adapté du renderer de Donut (`imgui_nvrhi.cpp`,
// NVIDIA, MIT) : sa classe devient des fonctions libres, comme nos passes (ADR-0011). Une passe
// de plus, chronométrée comme les autres, sous Vulkan comme sous WebGPU : ImGui ne donne que des
// triangles, des rectangles de découpe et des textures.

#include <cstdint>
#include <optional>
#include <unordered_map>

#include <imgui.h>
#include <nvrhi/nvrhi.h>

#include "levain/core/error.hpp"

namespace levain::ui
{

/// Une texture d'ImGui sur le GPU, avec le binding set qui la lit.
struct UiTexture
{
    nvrhi::TextureHandle texture;
    nvrhi::BindingSetHandle bindings;
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
    /// Les textures qu'ImGui a demandées (`ImTextureData`), par l'identifiant qu'on lui a donné.
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

/// Enregistre l'UI d'`drawData` dans `target` : les textures d'abord, puis les sommets, puis une
/// commande de dessin par commande d'ImGui, chacune avec sa texture et son rectangle de découpe.
UiDrawStats recordUi(nvrhi::IDevice& device, nvrhi::ICommandList& commandList, UiPass& pass,
                     ImDrawData& drawData, nvrhi::IFramebuffer& target);

/// Libère toutes les textures, et le dit à ImGui. À l'arrêt, avant le device.
void destroyUiTextures(UiPass& pass, ImVector<ImTextureData*>& textures);

} // namespace levain::ui
