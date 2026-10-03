#include "levain/render/shader.hpp"

#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <utility>

#include "levain/core/file.hpp"

namespace levain::render
{

namespace
{

/// SPIR-V pour Vulkan, WGSL pour WebGPU, DXIL pour Direct3D 12 : shaders/CMakeLists.txt produit
/// les trois. Avec `entryNameFor`, c'est tout ce que le module sait de l'API graphique.
std::string_view shaderExtensionFor(nvrhi::GraphicsAPI api)
{
    switch (api)
    {
    case nvrhi::GraphicsAPI::VULKAN:
        return ".spv";
    case nvrhi::GraphicsAPI::WEBGPU:
        return ".wgsl";
    default:
        return ".dxil";
    }
}

/// Le point d'entrée dans le fichier compilé. slangc nomme « main » celui d'un SPIR-V ou d'un DXIL
/// ; un WGSL garde le nom de la fonction, celui qui suit le point dans « mesh.vertexMain ».
std::string entryNameFor(nvrhi::GraphicsAPI api, std::string_view name)
{
    return api == nvrhi::GraphicsAPI::WEBGPU ? std::string{name.substr(name.rfind('.') + 1)}
                                             : std::string{"main"};
}

// ponytail: chemin absolu du dossier de build, suffisant tant qu'on lance depuis le build. À
// remplacer par un chemin relatif à l'exécutable le jour où on distribue un binaire.
std::filesystem::path shaderPath(nvrhi::IDevice& device, std::string_view name)
{
    return std::filesystem::path{LEVAIN_SHADER_DIR} /
           std::format("{}{}", name, shaderExtensionFor(device.getGraphicsAPI()));
}

} // namespace

core::Result<nvrhi::ShaderHandle> loadShader(nvrhi::IDevice& device, std::string_view name,
                                             nvrhi::ShaderType type)
{
    auto bytecode = core::readFile(shaderPath(device, name));
    if (!bytecode)
    {
        return std::unexpected(std::move(bytecode.error()));
    }

    nvrhi::ShaderHandle shader =
        device.createShader(nvrhi::ShaderDesc()
                                .setShaderType(type)
                                .setEntryName(entryNameFor(device.getGraphicsAPI(), name))
                                .setDebugName(std::string{name}),
                            bytecode->data(), bytecode->size());
    if (!shader)
    {
        return core::makeError(core::ErrorCode::InvalidData, std::format("shader {} refusé", name));
    }
    return shader;
}

} // namespace levain::render
