/*
 * Adapté de Donut, src/app/imgui_nvrhi.cpp (https://github.com/NVIDIA-RTX/Donut) :
 *
 * Copyright (c) 2014-2025, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * License for Dear ImGui
 *
 * Copyright (c) 2014-2025 Omar Cornut
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "levain/ui/ui_pass.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <string_view>
#include <utility>
#include <vector>

#include "levain/core/assert.hpp"
#include "levain/core/log.hpp"
#include "levain/render/shader.hpp"

namespace levain::ui
{

namespace
{

/// Ce que lit `shaders/ui.slang`.
struct UiConstants
{
    float inverseDisplayWidth;
    float inverseDisplayHeight;
    std::uint32_t linearizeColors;
    std::uint32_t padding;
};

static_assert(sizeof(UiConstants) == 16, "disposition lue par shaders/ui.slang");
static_assert(sizeof(ImDrawIdx) == 2,
              "le format d'index de recordUi suppose des index sur 16 bits");

/// Une image par command list ; de la marge pour les images en vol.
constexpr std::uint32_t ConstantVersions = 4;

/// La marge ajoutée quand un buffer grandit, en éléments : sans elle, une fenêtre qui s'ouvre le
/// referait grandir à chaque image.
constexpr std::size_t GrowthMargin = 5000;

/// Les écritures de buffer arrondies à 4 octets (`roundToFourBytes`) : NVRHI sous Vulkan écrit par
/// `vkCmdUpdateBuffer`, qui exige une taille multiple de 4, et arrondit au-dessus. Un nombre impair
/// d'index sur 16 bits (un triangle lissé en a 21) ferait lire 2 octets après la fin des données,
/// et écrire après la fin d'un buffer juste plein, une erreur de validation.
constexpr std::size_t roundToFourBytes(std::size_t bytes)
{
    return (bytes + 3) / 4 * 4;
}

/// Un buffer de sommets ou d'index d'au moins `bytes` octets : celui d'avant s'il suffit, un plus
/// grand sinon. Il ne rapetisse pas.
void ensureBuffer(nvrhi::IDevice& device, nvrhi::BufferHandle& buffer, std::size_t bytes,
                  std::size_t growTo, bool isIndexBuffer)
{
    if (buffer && buffer->getDesc().byteSize >= bytes)
    {
        return;
    }
    buffer = device.createBuffer(
        nvrhi::BufferDesc()
            .setByteSize(roundToFourBytes(growTo))
            .setIsVertexBuffer(!isIndexBuffer)
            .setIsIndexBuffer(isIndexBuffer)
            .setInitialState(isIndexBuffer ? nvrhi::ResourceStates::IndexBuffer
                                           : nvrhi::ResourceStates::VertexBuffer)
            .setKeepInitialState(true)
            .setDebugName(isIndexBuffer ? "index de l'UI" : "sommets de l'UI"));
}

/// Le binding set qui fait lire `texture` à la passe : la constante, la texture, l'échantillonneur.
/// Le même pour l'atlas d'ImGui et pour une texture que le programme montre (`registerUiTexture`),
/// et sans format d'une vue (`Format::UNKNOWN`) : le texel se lit dans le format de la texture,
/// donc décodé par le matériel si elle est sRGB, brut sinon.
///
/// `setTrackLiveness(true)` est la valeur par défaut de NVRHI, posée ici pour qu'elle ne change pas
/// sans qu'on le voie : `releaseUiTexture` lâche la texture sans attendre le GPU, ce que seule la
/// référence que les command lists prennent au binding set permet (`BindingSetDesc::trackLiveness`,
/// nvrhi.h : à faux, c'est à l'appelant de ne rien libérer avant la fin des commandes).
nvrhi::BindingSetHandle bindingsFor(nvrhi::IDevice& device, const UiPass& pass,
                                    nvrhi::ITexture* texture)
{
    return device.createBindingSet(
        nvrhi::BindingSetDesc()
            .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, pass.constants))
            .addItem(nvrhi::BindingSetItem::Texture_SRV(0, texture))
            .addItem(nvrhi::BindingSetItem::Sampler(0, pass.sampler))
            .setTrackLiveness(true),
        pass.layout);
}

/// Les quatre formats de flottants sur 32 bits par canal. Le sampler de l'UI filtre (linéaire), et
/// WebGPU ne filtre ces formats qu'avec la fonctionnalité facultative `float32-filterable` : Dawn
/// refuse le bind group (« UnfilterableFloat … expected Float »), une erreur de NVRHI qui, en
/// Debug, arrête sur l'assertion. Vulkan et Direct3D 12 les liraient d'ordinaire : on les refuse
/// partout, pour qu'un même programme montre la même chose sur chaque backend.
bool isUnfilterableFloat32(nvrhi::Format format)
{
    switch (format)
    {
    case nvrhi::Format::R32_FLOAT:
    case nvrhi::Format::RG32_FLOAT:
    case nvrhi::Format::RGB32_FLOAT:
    case nvrhi::Format::RGBA32_FLOAT:
        return true;
    default:
        return false;
    }
}

/// La texture d'ImGui sur le GPU, et son binding set. ImGui la donne en RGBA tant qu'on ne lui en
/// demande pas une autre (`ImFontAtlas::TexDesiredFormat`) : le shader multiplie par toute la
/// couleur, un atlas d'un seul canal y serait faux.
UiTexture createTexture(nvrhi::IDevice& device, const UiPass& pass, const ImTextureData& data)
{
    LEVAIN_ASSERT(data.Format == ImTextureFormat_RGBA32, "texture d'ImGui attendue en RGBA");
    UiTexture texture;
    texture.texture =
        device.createTexture(nvrhi::TextureDesc()
                                 .setWidth(static_cast<std::uint32_t>(data.Width))
                                 .setHeight(static_cast<std::uint32_t>(data.Height))
                                 .setFormat(nvrhi::Format::RGBA8_UNORM)
                                 .setInitialState(nvrhi::ResourceStates::ShaderResource)
                                 .setKeepInitialState(true)
                                 .setDebugName("texture de l'UI"));
    if (texture.texture)
    {
        texture.bindings = bindingsFor(device, pass, texture.texture);
    }
    return texture;
}

/// Toute la texture, d'un coup : `writeTexture` écrit une sous-ressource entière.
void uploadTexture(nvrhi::ICommandList& commandList, const UiTexture& texture, ImTextureData& data)
{
    commandList.writeTexture(texture.texture, 0, 0, data.GetPixels(),
                             static_cast<std::size_t>(data.GetPitch()));
}

} // namespace

core::Result<UiPass> createUiPass(nvrhi::IDevice& device, const nvrhi::FramebufferInfo& target)
{
    auto vertexShader = render::loadShader(device, "ui.vertexMain", nvrhi::ShaderType::Vertex);
    auto pixelShader = render::loadShader(device, "ui.fragmentMain", nvrhi::ShaderType::Pixel);
    if (!vertexShader || !pixelShader)
    {
        return std::unexpected(vertexShader ? pixelShader.error() : vertexShader.error());
    }
    UiPass pass;
    pass.vertexShader = std::move(*vertexShader);
    pass.pixelShader = std::move(*pixelShader);
    // Les noms sont les sémantiques de VertexInput, dans le même ordre (le backend WebGPU numérote
    // les attributs ainsi).
    const std::array<nvrhi::VertexAttributeDesc, 3> attributes{
        nvrhi::VertexAttributeDesc()
            .setName("POSITION")
            .setFormat(nvrhi::Format::RG32_FLOAT)
            .setOffset(offsetof(ImDrawVert, pos))
            .setElementStride(sizeof(ImDrawVert)),
        nvrhi::VertexAttributeDesc()
            .setName("TEXCOORD")
            .setFormat(nvrhi::Format::RG32_FLOAT)
            .setOffset(offsetof(ImDrawVert, uv))
            .setElementStride(sizeof(ImDrawVert)),
        nvrhi::VertexAttributeDesc()
            .setName("COLOR")
            .setFormat(nvrhi::Format::RGBA8_UNORM)
            .setOffset(offsetof(ImDrawVert, col))
            .setElementStride(sizeof(ImDrawVert)),
    };
    pass.inputLayout =
        device.createInputLayout(attributes.data(), attributes.size(), pass.vertexShader);
    nvrhi::BindingLayoutDesc layoutDesc;
    layoutDesc.visibility = nvrhi::ShaderType::Vertex | nvrhi::ShaderType::Pixel;
    layoutDesc.bindings = {nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
                           nvrhi::BindingLayoutItem::Texture_SRV(0),
                           nvrhi::BindingLayoutItem::Sampler(0)};
    pass.layout = device.createBindingLayout(layoutDesc);

    nvrhi::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc.primType = nvrhi::PrimitiveType::TriangleList;
    pipelineDesc.inputLayout = pass.inputLayout;
    pipelineDesc.VS = pass.vertexShader;
    pipelineDesc.PS = pass.pixelShader;
    pipelineDesc.addBindingLayout(pass.layout);
    // Le mélange d'ImGui : la couleur par son alpha. L'alpha de l'image, que rien ne lit après,
    // devient celui de l'UI posée sur la scène.
    pipelineDesc.renderState.blendState.targets[0]
        .setBlendEnable(true)
        .setSrcBlend(nvrhi::BlendFactor::SrcAlpha)
        .setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
        .setSrcBlendAlpha(nvrhi::BlendFactor::One)
        .setDestBlendAlpha(nvrhi::BlendFactor::InvSrcAlpha);
    pipelineDesc.renderState.depthStencilState.depthTestEnable = false;
    pipelineDesc.renderState.depthStencilState.depthWriteEnable = false;
    pipelineDesc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
    pipelineDesc.renderState.rasterState.scissorEnable = true;
    pass.pipeline = device.createGraphicsPipeline(pipelineDesc, target);
    pass.constants = device.createBuffer(nvrhi::BufferDesc()
                                             .setByteSize(sizeof(UiConstants))
                                             .setIsConstantBuffer(true)
                                             .setIsVolatile(true)
                                             .setMaxVersions(ConstantVersions)
                                             .setDebugName("constantes de l'UI"));
    // Les polices sont rastérisées à la taille de l'écran : un texel par pixel, mais le filtrage
    // lisse les images qu'un panneau agrandit.
    pass.sampler = device.createSampler(nvrhi::SamplerDesc().setAllFilters(true).setAllAddressModes(
        nvrhi::SamplerAddressMode::Clamp));
    if (!pass.inputLayout || !pass.layout || !pass.pipeline || !pass.constants || !pass.sampler)
    {
        return core::makeError(core::ErrorCode::InvalidData, "passe de l'UI refusée par NVRHI");
    }
    pass.linearizeColors =
        !target.colorFormats.empty() && linearOnSrgbTarget(target.colorFormats.front());
    return pass;
}

bool linearOnSrgbTarget(nvrhi::Format format)
{
    return nvrhi::getFormatInfo(format).isSRGB;
}

std::optional<nvrhi::Rect> clampScissorToTarget(const ImVec4& clip, std::uint32_t width,
                                                std::uint32_t height)
{
    // Arrondi vers l'extérieur, comme le ferait le rasteriseur : un rectangle à 10,5 couvre le
    // pixel 10.
    const auto bounded = [](float value, std::uint32_t limit)
    {
        if (!(value > 0.0f)) // négatif ou NaN
        {
            return 0;
        }
        return static_cast<int>(std::min(value, static_cast<float>(limit)));
    };
    const int minX = bounded(std::floor(clip.x), width);
    const int minY = bounded(std::floor(clip.y), height);
    const int maxX = bounded(std::ceil(clip.z), width);
    const int maxY = bounded(std::ceil(clip.w), height);
    if (maxX <= minX || maxY <= minY)
    {
        return std::nullopt;
    }
    return nvrhi::Rect(minX, maxX, minY, maxY);
}

void updateUiTextures(nvrhi::IDevice& device, nvrhi::ICommandList& commandList, UiPass& pass,
                      ImVector<ImTextureData*>& textures)
{
    for (ImTextureData* data : textures)
    {
        switch (data->Status)
        {
        case ImTextureStatus_WantCreate:
        {
            UiTexture texture = createTexture(device, pass, *data);
            if (!texture.texture || !texture.bindings)
            {
                // Bruyamment (règle n°7) : sans identifiant, la commande qui lit cette texture
                // ferait échouer une assertion d'ImGui (ImDrawCmd::GetTexID).
                core::log("ui", core::LogLevel::Error,
                          "texture de l'UI de {} × {} refusée par NVRHI", data->Width,
                          data->Height);
                LEVAIN_ASSERT(false, "texture de l'UI refusée");
                break;
            }
            uploadTexture(commandList, texture, *data);
            const ImTextureID id = pass.nextTextureId++;
            pass.textures.emplace(id, std::move(texture));
            data->SetTexID(id);
            data->SetStatus(ImTextureStatus_OK);
            break;
        }
        case ImTextureStatus_WantUpdates:
        {
            const auto found = pass.textures.find(data->GetTexID());
            // Une texture que la passe ne connaît pas : elle a été recréée sans
            // `destroyUiTextures`, et ImGui garde un identifiant périmé.
            LEVAIN_ASSERT(found != pass.textures.end(), "mise à jour d'une texture d'UI inconnue");
            if (found != pass.textures.end())
            {
                uploadTexture(commandList, found->second, *data);
            }
            data->SetStatus(ImTextureStatus_OK);
            break;
        }
        case ImTextureStatus_WantDestroy:
            // NVRHI garde la texture en vie tant qu'une command list soumise la référence : on peut
            // la lâcher tout de suite.
            pass.textures.erase(data->GetTexID());
            data->SetTexID(ImTextureID_Invalid);
            data->SetStatus(ImTextureStatus_Destroyed);
            break;
        case ImTextureStatus_OK:
        case ImTextureStatus_Destroyed:
            break;
        }
    }
}

std::string_view uiTextureRefusal(const nvrhi::TextureDesc& desc)
{
    // Avant la dimension : NVRHI ne crée une texture multi-échantillon qu'en `Texture2DMS`, que la
    // dimension refuserait sans dire pourquoi.
    if (desc.sampleCount != 1)
    {
        return "l'UI ne lit pas une texture multi-échantillon (résoudre d'abord)";
    }
    if (desc.dimension != nvrhi::TextureDimension::Texture2D)
    {
        return "l'UI ne montre que des textures 2D";
    }
    // `isShaderResource` vaut vrai par défaut dans NVRHI (nvrhi.h, « backward compatibility ») :
    // une texture qui le désactive n'a ni usage d'échantillonnage (Vulkan) ni SRV (Direct3D 12).
    if (!desc.isShaderResource)
    {
        return "la texture n'est pas lisible par un shader (isShaderResource)";
    }
    const nvrhi::FormatInfo& format = nvrhi::getFormatInfo(desc.format);
    if (format.hasDepth || format.hasStencil)
    {
        return "l'UI ne lit pas une profondeur ou un stencil";
    }
    // Le shader déclare un `Texture2D<float4>` : un format entier ne s'y lit pas (Vulkan : VUID
    // 07753, le type de composante).
    if (format.kind == nvrhi::FormatKind::Integer)
    {
        return "l'UI ne lit pas un format entier";
    }
    if (isUnfilterableFloat32(desc.format))
    {
        return "l'UI filtre ses textures, et WebGPU ne filtre pas les flottants sur 32 bits";
    }
    return {};
}

core::Result<ImTextureID> registerUiTexture(nvrhi::IDevice& device, UiPass& pass,
                                            nvrhi::ITexture* texture)
{
    if (texture == nullptr)
    {
        return core::makeError(core::ErrorCode::InvalidData, "texture d'UI nulle");
    }
    if (const std::string_view refusal = uiTextureRefusal(texture->getDesc()); !refusal.empty())
    {
        return core::makeError(
            core::ErrorCode::Unsupported,
            std::format("texture d'UI « {} » : {}", texture->getDesc().debugName, refusal));
    }
    UiTexture shown{
        .texture = texture, .bindings = bindingsFor(device, pass, texture), .registered = true};
    if (!shown.bindings)
    {
        return core::makeError(core::ErrorCode::InvalidData,
                               "binding set de la texture d'UI refusé par NVRHI");
    }
    const ImTextureID id = pass.nextTextureId++;
    pass.textures.emplace(id, std::move(shown));
    return id;
}

void releaseUiTexture(UiPass& pass, ImTextureID id)
{
    const auto found = pass.textures.find(id);
    const bool isRegistered = found != pass.textures.end() && found->second.registered;
    LEVAIN_ASSERT(isRegistered, "libération d'une texture d'UI qui n'est pas enregistrée");
    if (!isRegistered)
    {
        core::log("ui", core::LogLevel::Error,
                  "libération de la texture d'UI {} : elle n'est pas enregistrée (déjà libérée, ou "
                  "à ImGui)",
                  id);
        return;
    }
    // Pas d'attente du GPU : le binding set, que les command lists d'une image en vol tiennent
    // (`trackLiveness`, voir `bindingsFor`), garde la texture jusqu'à leur fin.
    pass.textures.erase(found);
}

UiDrawStats recordUi(nvrhi::IDevice& device, nvrhi::ICommandList& commandList, UiPass& pass,
                     ImDrawData& drawData, nvrhi::IFramebuffer& target)
{
    UiDrawStats stats;
    if (drawData.Textures != nullptr)
    {
        updateUiTextures(device, commandList, pass, *drawData.Textures);
    }
    if (drawData.TotalVtxCount == 0 || drawData.DisplaySize.x <= 0.0f ||
        drawData.DisplaySize.y <= 0.0f)
    {
        return stats;
    }

    // Toutes les listes de dessin dans un buffer de sommets et un d'index.
    const auto vertexCount = static_cast<std::size_t>(drawData.TotalVtxCount);
    const auto indexCount = static_cast<std::size_t>(drawData.TotalIdxCount);
    ensureBuffer(device, pass.vertices, vertexCount * sizeof(ImDrawVert),
                 (vertexCount + GrowthMargin) * sizeof(ImDrawVert), false);
    ensureBuffer(device, pass.indices, indexCount * sizeof(ImDrawIdx),
                 (indexCount + GrowthMargin) * sizeof(ImDrawIdx), true);
    if (!pass.vertices || !pass.indices)
    {
        return stats;
    }
    std::vector<ImDrawVert> vertices;
    std::vector<ImDrawIdx> indices;
    vertices.reserve(vertexCount);
    indices.reserve(indexCount + 1);
    for (const ImDrawList* list : drawData.CmdLists)
    {
        vertices.insert(vertices.end(), list->VtxBuffer.begin(), list->VtxBuffer.end());
        indices.insert(indices.end(), list->IdxBuffer.begin(), list->IdxBuffer.end());
    }
    // Un nombre pair d'index : l'écriture fait alors un multiple de 4 octets (`roundToFourBytes`).
    if (indices.size() % 2 != 0)
    {
        indices.push_back(0);
    }
    // Une écriture par buffer et par image : ce que permet notre backend WebGPU (ADR-0032).
    commandList.writeBuffer(pass.vertices, vertices.data(), vertices.size() * sizeof(ImDrawVert));
    commandList.writeBuffer(pass.indices, indices.data(), indices.size() * sizeof(ImDrawIdx));
    const UiConstants constants{.inverseDisplayWidth = 1.0f / drawData.DisplaySize.x,
                                .inverseDisplayHeight = 1.0f / drawData.DisplaySize.y,
                                .linearizeColors = pass.linearizeColors ? 1U : 0U,
                                .padding = 0};
    commandList.writeBuffer(pass.constants, &constants, sizeof(constants));

    const nvrhi::FramebufferInfoEx& info = target.getFramebufferInfo();
    // Une seule fenêtre, sans viewports : l'image d'ImGui commence à l'origine. Sinon, le shader
    // (qui ne décale pas) et les découpes ne s'accorderaient plus. Sa taille, elle, peut différer
    // de la cible le temps d'un redimensionnement (sous X11, la swapchain suit la surface avant
    // SDL) : l'UI est alors étirée une image, et les découpes restent bornées à la cible.
    LEVAIN_ASSERT(drawData.DisplayPos.x == 0.0f && drawData.DisplayPos.y == 0.0f,
                  "image d'UI décalée");
    nvrhi::GraphicsState state;
    state.pipeline = pass.pipeline;
    state.framebuffer = &target;
    state.viewport.addViewport(info.getViewport());
    state.viewport.scissorRects.resize(1);
    state.addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(pass.vertices).setSlot(0));
    state.indexBuffer =
        nvrhi::IndexBufferBinding().setBuffer(pass.indices).setFormat(nvrhi::Format::R16_UINT);

    // Les décalages globaux de chaque liste : ses commandes y ajoutent les leurs (VtxOffset,
    // IdxOffset), ce qui permet plus de 65 536 sommets avec des index sur 16 bits.
    std::uint32_t listVertexOffset = 0;
    std::uint32_t listIndexOffset = 0;
    for (const ImDrawList* list : drawData.CmdLists)
    {
        for (const ImDrawCmd& command : list->CmdBuffer)
        {
            if (command.UserCallback != nullptr)
            {
                // ImDrawCallback_ResetRenderState et les rappels d'un programme : aucun en M7.1.
                continue;
            }
            const auto texture = pass.textures.find(command.GetTexID());
            if (texture == pass.textures.end())
            {
                // Une image libérée (`releaseUiTexture`) que le programme dessine encore, ou un
                // identifiant venu de nulle part. En Debug, l'assertion arrête ; en Release, la
                // commande saute, bruyamment (règle n°7), et ne touche à aucun binding set.
                LEVAIN_ASSERT(false, "commande d'UI sur une texture inconnue");
                core::log("ui", core::LogLevel::Error,
                          "commande d'UI sur la texture {}, inconnue ou libérée : sautée",
                          command.GetTexID());
                ++stats.unknownTextures;
                continue;
            }
            const std::optional<nvrhi::Rect> scissor =
                clampScissorToTarget(command.ClipRect, info.width, info.height);
            if (!scissor)
            {
                ++stats.clipped;
                continue;
            }
            state.bindings = {texture->second.bindings};
            state.viewport.scissorRects[0] = *scissor;
            commandList.setGraphicsState(state);
            nvrhi::DrawArguments arguments;
            arguments.vertexCount = command.ElemCount;
            arguments.startIndexLocation = listIndexOffset + command.IdxOffset;
            arguments.startVertexLocation = listVertexOffset + command.VtxOffset;
            commandList.drawIndexed(arguments);
            ++stats.draws;
        }
        listVertexOffset += static_cast<std::uint32_t>(list->VtxBuffer.Size);
        listIndexOffset += static_cast<std::uint32_t>(list->IdxBuffer.Size);
    }
    stats.vertices = static_cast<std::uint32_t>(vertexCount);
    return stats;
}

void destroyUiTextures(UiPass& pass, ImVector<ImTextureData*>& textures)
{
    for (ImTextureData* data : textures)
    {
        if (data->Status != ImTextureStatus_Destroyed && data->GetTexID() != ImTextureID_Invalid)
        {
            data->SetTexID(ImTextureID_Invalid);
            data->SetStatus(ImTextureStatus_Destroyed);
        }
    }
    pass.textures.clear();
}

} // namespace levain::ui
