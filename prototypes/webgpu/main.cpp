// Prototype jetable (spike/webgpu) : le cube texturé de M2.2 en WebGPU, dans le navigateur, par
// Emscripten et emdawnwebgpu. Il répond à une question : ce que le moteur demande à NVRHI (buffers,
// texture, sampler, binding set, pipeline, profondeur, envoi par writeBuffer) passe-t-il en WebGPU,
// avec des shaders Slang compilés en WGSL ? Il ne sera pas fusionné.

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <webgpu/webgpu_cpp.h>

namespace
{

struct Vertex
{
    float position[3];
    float uv[2];
};

// Le cube de createCube (engine/render/src/mesh.cpp), sans la couleur : six faces de quatre sommets.
const std::array<Vertex, 24> CubeVertices = [] {
    std::array<Vertex, 24> v{};
    const float p = 0.5f;
    const float faces[6][4][3] = {
        {{-p, -p, p}, {p, -p, p}, {p, p, p}, {-p, p, p}},     // +z
        {{p, -p, -p}, {-p, -p, -p}, {-p, p, -p}, {p, p, -p}}, // -z
        {{p, -p, p}, {p, -p, -p}, {p, p, -p}, {p, p, p}},     // +x
        {{-p, -p, -p}, {-p, -p, p}, {-p, p, p}, {-p, p, -p}}, // -x
        {{-p, p, p}, {p, p, p}, {p, p, -p}, {-p, p, -p}},     // +y
        {{-p, -p, -p}, {p, -p, -p}, {p, -p, p}, {-p, -p, p}}, // -y
    };
    const float uvs[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
    for (int f = 0; f < 6; ++f)
    {
        for (int c = 0; c < 4; ++c)
        {
            v[f * 4 + c] = {{faces[f][c][0], faces[f][c][1], faces[f][c][2]}, {uvs[c][0], uvs[c][1]}};
        }
    }
    return v;
}();

std::array<std::uint32_t, 36> cubeIndices()
{
    std::array<std::uint32_t, 36> indices{};
    for (std::uint32_t f = 0; f < 6; ++f)
    {
        const std::uint32_t b = f * 4;
        const std::uint32_t face[6] = {b, b + 1, b + 2, b, b + 2, b + 3};
        for (int i = 0; i < 6; ++i)
        {
            indices[f * 6 + i] = face[i];
        }
    }
    return indices;
}

using Mat4 = std::array<float, 16>; // par colonnes, comme glm

Mat4 multiply(const Mat4& a, const Mat4& b)
{
    Mat4 r{};
    for (int c = 0; c < 4; ++c)
    {
        for (int row = 0; row < 4; ++row)
        {
            for (int k = 0; k < 4; ++k)
            {
                r[c * 4 + row] += a[k * 4 + row] * b[c * 4 + k];
            }
        }
    }
    return r;
}

/// Perspective à profondeur de 0 à 1 (WebGPU, comme Vulkan et D3D12), caméra reculée de 2,5 sur z.
Mat4 viewProjection(float aspect)
{
    const float f = 1.0f / std::tan(0.5f * 1.047f);
    const float nearPlane = 0.1f;
    const float farPlane = 100.0f;
    const Mat4 projection{f / aspect, 0, 0, 0, 0, f, 0, 0, 0, 0, farPlane / (nearPlane - farPlane), -1,
                          0, 0, farPlane * nearPlane / (nearPlane - farPlane), 0};
    const Mat4 view{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, -2.5f, 1};
    return multiply(projection, view);
}

Mat4 rotation(float seconds)
{
    const float a = seconds;
    const float c = std::cos(a);
    const float s = std::sin(a);
    const Mat4 y{c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, 0, 0, 0, 1};
    const float b = 0.5f;
    const Mat4 x{1, 0, 0, 0, 0, std::cos(b), std::sin(b), 0, 0, -std::sin(b), std::cos(b), 0, 0, 0, 0, 1};
    return multiply(x, y);
}

struct App
{
    wgpu::Instance instance;
    wgpu::Adapter adapter;
    wgpu::Device device;
    wgpu::Surface surface;
    wgpu::TextureFormat format = wgpu::TextureFormat::Undefined;
    wgpu::RenderPipeline pipeline;
    wgpu::Buffer vertices;
    wgpu::Buffer indices;
    wgpu::Buffer uniforms;
    wgpu::BindGroup bindGroup;
    wgpu::Texture depth;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool ready = false;
    std::chrono::steady_clock::time_point start;
    std::chrono::steady_clock::time_point periodStart;
    int periodFrames = 0;
    int totalFrames = 0;
};

App app;

std::string readFile(const char* path)
{
    std::ifstream file{path};
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

/// Un damier 8 × 8 en sRGB, comme data/textures/checker.png, fabriqué ici pour ne rien charger.
wgpu::Texture createChecker()
{
    constexpr std::uint32_t Side = 8;
    std::vector<std::uint8_t> pixels(Side * Side * 4);
    for (std::uint32_t y = 0; y < Side; ++y)
    {
        for (std::uint32_t x = 0; x < Side; ++x)
        {
            const std::uint8_t value = ((x + y) % 2 == 0) ? 230 : 40;
            std::uint8_t* p = &pixels[(y * Side + x) * 4];
            p[0] = value;
            p[1] = static_cast<std::uint8_t>(value * 0.6f);
            p[2] = static_cast<std::uint8_t>(value * 0.3f);
            p[3] = 255;
        }
    }
    wgpu::TextureDescriptor desc{};
    desc.size = {Side, Side, 1};
    desc.format = wgpu::TextureFormat::RGBA8UnormSrgb;
    desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
    wgpu::Texture texture = app.device.CreateTexture(&desc);
    wgpu::TexelCopyTextureInfo destination{};
    destination.texture = texture;
    wgpu::TexelCopyBufferLayout layout{};
    layout.bytesPerRow = Side * 4;
    layout.rowsPerImage = Side;
    app.device.GetQueue().WriteTexture(&destination, pixels.data(), pixels.size(), &layout, &desc.size);
    return texture;
}

wgpu::Buffer createBuffer(const void* data, std::size_t size, wgpu::BufferUsage usage)
{
    wgpu::BufferDescriptor desc{};
    desc.size = (size + 3) & ~std::size_t{3};
    desc.usage = usage | wgpu::BufferUsage::CopyDst;
    wgpu::Buffer buffer = app.device.CreateBuffer(&desc);
    if (data != nullptr)
    {
        app.device.GetQueue().WriteBuffer(buffer, 0, data, desc.size);
    }
    return buffer;
}

void resizeTargets()
{
    double cssWidth = 0;
    double cssHeight = 0;
    emscripten_get_element_css_size("#canvas", &cssWidth, &cssHeight);
    const double ratio = emscripten_get_device_pixel_ratio();
    const auto width = static_cast<std::uint32_t>(cssWidth * ratio);
    const auto height = static_cast<std::uint32_t>(cssHeight * ratio);
    if (width == app.width && height == app.height)
    {
        return;
    }
    app.width = width;
    app.height = height;
    emscripten_set_canvas_element_size("#canvas", static_cast<int>(width), static_cast<int>(height));
    wgpu::SurfaceConfiguration config{};
    config.device = app.device;
    config.format = app.format;
    config.usage = wgpu::TextureUsage::RenderAttachment;
    config.width = width;
    config.height = height;
    config.presentMode = wgpu::PresentMode::Fifo;
    app.surface.Configure(&config);
    wgpu::TextureDescriptor depthDesc{};
    depthDesc.size = {width, height, 1};
    depthDesc.format = wgpu::TextureFormat::Depth32Float;
    depthDesc.usage = wgpu::TextureUsage::RenderAttachment;
    app.depth = app.device.CreateTexture(&depthDesc);
}

void createScene()
{
    wgpu::SurfaceCapabilities capabilities;
    app.surface.GetCapabilities(app.adapter, &capabilities);
    app.format = capabilities.formats[0];
    resizeTargets();

    // Le shader Slang, compilé en WGSL au build et embarqué dans le .wasm (--embed-file).
    const std::string wgsl = readFile("cube.wgsl");
    wgpu::ShaderSourceWGSL source{};
    source.code = {wgsl.data(), wgsl.size()};
    wgpu::ShaderModuleDescriptor moduleDesc{};
    moduleDesc.nextInChain = &source;
    wgpu::ShaderModule module = app.device.CreateShaderModule(&moduleDesc);

    std::array<wgpu::VertexAttribute, 2> attributes{};
    attributes[0] = {.format = wgpu::VertexFormat::Float32x3, .offset = 0, .shaderLocation = 0};
    attributes[1] = {.format = wgpu::VertexFormat::Float32x2, .offset = 12, .shaderLocation = 1};
    wgpu::VertexBufferLayout vertexLayout{};
    vertexLayout.arrayStride = sizeof(Vertex);
    vertexLayout.attributeCount = attributes.size();
    vertexLayout.attributes = attributes.data();

    wgpu::ColorTargetState target{};
    target.format = app.format;
    wgpu::FragmentState fragment{};
    fragment.module = module;
    fragment.entryPoint = "fragmentMain";
    fragment.targetCount = 1;
    fragment.targets = &target;
    wgpu::DepthStencilState depthState{};
    depthState.format = wgpu::TextureFormat::Depth32Float;
    depthState.depthWriteEnabled = wgpu::OptionalBool::True;
    depthState.depthCompare = wgpu::CompareFunction::Less;

    wgpu::RenderPipelineDescriptor pipelineDesc{};
    pipelineDesc.vertex.module = module;
    pipelineDesc.vertex.entryPoint = "vertexMain";
    pipelineDesc.vertex.bufferCount = 1;
    pipelineDesc.vertex.buffers = &vertexLayout;
    pipelineDesc.fragment = &fragment;
    pipelineDesc.depthStencil = &depthState;
    pipelineDesc.primitive.frontFace = wgpu::FrontFace::CCW;
    pipelineDesc.primitive.cullMode = wgpu::CullMode::Back;
    // layout vide : « auto », WebGPU le déduit du shader, comme NVRHI ne sait pas le faire.
    app.pipeline = app.device.CreateRenderPipeline(&pipelineDesc);

    const std::array<std::uint32_t, 36> indices = cubeIndices();
    app.vertices = createBuffer(CubeVertices.data(), sizeof(CubeVertices), wgpu::BufferUsage::Vertex);
    app.indices = createBuffer(indices.data(), sizeof(indices), wgpu::BufferUsage::Index);
    app.uniforms = createBuffer(nullptr, 2 * sizeof(Mat4), wgpu::BufferUsage::Uniform);

    wgpu::SamplerDescriptor samplerDesc{};
    samplerDesc.magFilter = wgpu::FilterMode::Nearest;
    samplerDesc.minFilter = wgpu::FilterMode::Nearest;
    wgpu::Sampler sampler = app.device.CreateSampler(&samplerDesc);
    wgpu::Texture checker = createChecker();

    // Les numéros de binding sont ceux que Slang a donnés dans le WGSL : 0, 1, 2 dans l'ordre des
    // déclarations. Le « binding set » de NVRHI est le « bind group » de WebGPU.
    std::array<wgpu::BindGroupEntry, 3> entries{};
    entries[0].binding = 0;
    entries[0].buffer = app.uniforms;
    entries[0].size = 2 * sizeof(Mat4);
    entries[1].binding = 1;
    entries[1].textureView = checker.CreateView();
    entries[2].binding = 2;
    entries[2].sampler = sampler;
    wgpu::BindGroupDescriptor groupDesc{};
    groupDesc.layout = app.pipeline.GetBindGroupLayout(0);
    groupDesc.entryCount = entries.size();
    groupDesc.entries = entries.data();
    app.bindGroup = app.device.CreateBindGroup(&groupDesc);

    app.start = std::chrono::steady_clock::now();
    app.periodStart = app.start;
    app.ready = true;
}

void frame()
{
    if (!app.ready)
    {
        return;
    }
    resizeTargets();
    const auto now = std::chrono::steady_clock::now();
    const float seconds = std::chrono::duration<float>(now - app.start).count();
    const std::array<Mat4, 2> constants{
        viewProjection(static_cast<float>(app.width) / static_cast<float>(app.height)),
        rotation(seconds)};
    app.device.GetQueue().WriteBuffer(app.uniforms, 0, constants.data(), sizeof(constants));

    wgpu::SurfaceTexture surfaceTexture;
    app.surface.GetCurrentTexture(&surfaceTexture);
    if (surfaceTexture.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal &&
        surfaceTexture.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal)
    {
        return;
    }
    wgpu::RenderPassColorAttachment color{};
    color.view = surfaceTexture.texture.CreateView();
    color.loadOp = wgpu::LoadOp::Clear;
    color.storeOp = wgpu::StoreOp::Store;
    color.clearValue = {0.55, 0.32, 0.14, 1.0}; // la croûte de levain du sandbox
    wgpu::RenderPassDepthStencilAttachment depthAttachment{};
    depthAttachment.view = app.depth.CreateView();
    depthAttachment.depthLoadOp = wgpu::LoadOp::Clear;
    depthAttachment.depthStoreOp = wgpu::StoreOp::Store;
    depthAttachment.depthClearValue = 1.0f;
    wgpu::RenderPassDescriptor passDesc{};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &color;
    passDesc.depthStencilAttachment = &depthAttachment;

    wgpu::CommandEncoder encoder = app.device.CreateCommandEncoder();
    wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&passDesc);
    pass.SetPipeline(app.pipeline);
    pass.SetBindGroup(0, app.bindGroup);
    pass.SetVertexBuffer(0, app.vertices);
    pass.SetIndexBuffer(app.indices, wgpu::IndexFormat::Uint32);
    pass.DrawIndexed(36);
    pass.End();
    wgpu::CommandBuffer commands = encoder.Finish();
    app.device.GetQueue().Submit(1, &commands);

    // Les images par seconde, toutes les deux secondes, dans la console et le titre de la page.
    ++app.periodFrames;
    ++app.totalFrames;
    const float period = std::chrono::duration<float>(now - app.periodStart).count();
    if (period >= 2.0f)
    {
        char title[128];
        std::snprintf(title, sizeof(title), "Levain WebGPU : %.0f images/s, %u × %u, %d images",
                      app.periodFrames / period, app.width, app.height, app.totalFrames);
        emscripten_set_window_title(title);
        std::printf("%s\n", title);
        app.periodStart = now;
        app.periodFrames = 0;
    }
}

} // namespace

int main()
{
    app.instance = wgpu::CreateInstance(nullptr);
    wgpu::EmscriptenSurfaceSourceCanvasHTMLSelector canvas{};
    canvas.selector = "#canvas";
    wgpu::SurfaceDescriptor surfaceDesc{};
    surfaceDesc.nextInChain = &canvas;
    app.surface = app.instance.CreateSurface(&surfaceDesc);

    // Tout est asynchrone dans le navigateur : l'adaptateur, puis le device, arrivent par des
    // callbacks, et la boucle attend qu'ils soient là. NVRHI, lui, suppose un device créé de façon
    // synchrone : c'est un point à traiter dans un backend.
    wgpu::RequestAdapterOptions adapterOptions{};
    adapterOptions.compatibleSurface = app.surface;
    app.instance.RequestAdapter(
        &adapterOptions, wgpu::CallbackMode::AllowSpontaneous,
        [](wgpu::RequestAdapterStatus status, wgpu::Adapter adapter, wgpu::StringView message)
        {
            if (status != wgpu::RequestAdapterStatus::Success)
            {
                std::printf("ÉCHEC adaptateur : %.*s\n", static_cast<int>(message.length), message.data);
                emscripten_set_window_title("Levain WebGPU : pas d'adaptateur");
                return;
            }
            app.adapter = adapter;
            wgpu::AdapterInfo info;
            adapter.GetInfo(&info);
            std::printf("adaptateur : %.*s (%.*s)\n", static_cast<int>(info.device.length),
                        info.device.data, static_cast<int>(info.description.length),
                        info.description.data);
            wgpu::DeviceDescriptor deviceDesc{};
            deviceDesc.SetUncapturedErrorCallback(
                [](const wgpu::Device&, wgpu::ErrorType, wgpu::StringView error)
                { std::printf("ERREUR WebGPU : %.*s\n", static_cast<int>(error.length), error.data); });
            adapter.RequestDevice(
                &deviceDesc, wgpu::CallbackMode::AllowSpontaneous,
                [](wgpu::RequestDeviceStatus status, wgpu::Device device, wgpu::StringView message)
                {
                    if (status != wgpu::RequestDeviceStatus::Success)
                    {
                        std::printf("ÉCHEC device : %.*s\n", static_cast<int>(message.length),
                                    message.data);
                        return;
                    }
                    app.device = device;
                    createScene();
                    std::printf("scène créée\n");
                });
        });
    emscripten_set_main_loop(frame, 0, false);
    return 0;
}
