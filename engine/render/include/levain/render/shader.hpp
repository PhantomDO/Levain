#pragma once

#include <string_view>

#include <nvrhi/nvrhi.h>

#include "levain/core/error.hpp"

// Partagé par les passes du moteur et par celles des plugins (ADR-0025).

namespace levain::render
{

/// Charge un shader compilé au build (shaders/CMakeLists.txt), au format du device : SPIR-V sous
/// Vulkan, DXIL sous Direct3D 12. `name` est « fichier.pointDEntrée », par exemple «
/// mesh.vertexMain ».
[[nodiscard]] core::Result<nvrhi::ShaderHandle>
loadShader(nvrhi::IDevice& device, std::string_view name, nvrhi::ShaderType type);

} // namespace levain::render
