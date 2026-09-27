// Le backend WebGPU de NVRHI (ADR-0023, #184), en natif sur Dawn. Sans GPU, en CI, Dawn tourne sur
// lavapipe, son adaptateur de repli.

#include <string>

#include <doctest/doctest.h>
#include <nvrhi/nvrhi.h>

#include "levain/gpu/webgpu.hpp"

namespace
{

nvrhi::DeviceHandle webGpuDevice(nvrhi::IMessageCallback* messages = nullptr)
{
    // La couche de validation de NVRHI vérifie aussi ce que le moteur demande au backend.
    auto device = levain::gpu::createWebGpuDevice(
        {.enableValidation = messages == nullptr, .messageCallback = messages});
    INFO("erreur : " << (device ? std::string{} : device.error().message));
    REQUIRE(device.has_value());
    return *device;
}

} // namespace

TEST_CASE("le device WebGPU crée buffers, textures et samplers, sous la validation de NVRHI")
{
    const nvrhi::DeviceHandle device = webGpuDevice();
    CHECK(device->getGraphicsAPI() == nvrhi::GraphicsAPI::WEBGPU);

    const nvrhi::BufferHandle buffer = device->createBuffer(
        nvrhi::BufferDesc().setByteSize(64).setIsVertexBuffer(true).setDebugName("sommets"));
    REQUIRE(buffer);
    CHECK(buffer->getDesc().byteSize == 64);

    const nvrhi::TextureHandle texture =
        device->createTexture(nvrhi::TextureDesc()
                                  .setWidth(4)
                                  .setHeight(4)
                                  .setMipLevels(3)
                                  .setFormat(nvrhi::Format::SRGBA8_UNORM)
                                  .setDebugName("texture"));
    REQUIRE(texture);
    CHECK(texture->getDesc().mipLevels == 3);

    CHECK(device->createSampler(nvrhi::SamplerDesc().setAllFilters(true).setMaxAnisotropy(16)));
}

TEST_CASE("le backend WebGPU déclare absent ce que WebGPU n'a pas")
{
    const nvrhi::DeviceHandle device = webGpuDevice();
    CHECK_FALSE(device->queryFeatureSupport(nvrhi::Feature::RayTracingPipeline));
    CHECK_FALSE(device->queryFeatureSupport(nvrhi::Feature::Meshlets));
    CHECK_FALSE(device->queryFeatureSupport(nvrhi::Feature::VariableRateShading));
    CHECK((device->queryFormatSupport(nvrhi::Format::SRGBA8_UNORM) &
           nvrhi::FormatSupport::ShaderSample) != nvrhi::FormatSupport::None);
    // Le BC1 : ce backend ne propose que le BC7 des textures cuites (ADR-0020).
    CHECK(device->queryFormatSupport(nvrhi::Format::BC1_UNORM) == nvrhi::FormatSupport::None);
}
