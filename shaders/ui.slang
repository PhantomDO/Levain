// L'interface d'ImGui (M7.1, ADR-0032), adaptée des shaders `imgui_vertex.hlsl` et `imgui_pixel.hlsl`
// de Donut (NVIDIA, MIT). Des triangles en pixels de l'image, une couleur par sommet, une texture :
// l'atlas des polices, ou une image qu'un panneau affiche.

struct UiConstants
{
    float2 inverseDisplaySize;
    // 1 : la cible est sRGB, et convertit en écrivant. Les couleurs d'ImGui, en sRGB, sont alors
    // linéarisées ici, sinon elles seraient converties deux fois (`linearOnSrgbTarget`).
    uint linearizeColors;
    uint padding;
};

// Un constant buffer et non des push constants, que notre backend WebGPU n'a pas (ADR-0032).
ConstantBuffer<UiConstants> ui : register(b0);
Texture2D atlas : register(t0);
SamplerState atlasSampler : register(s0);

struct VertexInput
{
    float2 position : POSITION;
    float2 uv : TEXCOORD;
    float4 color : COLOR;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD;
    float4 color : COLOR;
};

/// La fonction exacte de sRGB vers linéaire, par composante.
float3 linearFromSrgb(float3 color)
{
    return select(color <= 0.04045, color / 12.92, pow((color + 0.055) / 1.055, 2.4));
}

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    // Des pixels, origine en haut à gauche, vers [-1, 1], y vers le haut.
    output.position = float4(input.position * ui.inverseDisplaySize * float2(2.0, -2.0) +
                                 float2(-1.0, 1.0),
                             0.0, 1.0);
    output.uv = input.uv;
    output.color = ui.linearizeColors != 0 ? float4(linearFromSrgb(input.color.rgb), input.color.a)
                                           : input.color;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    return input.color * atlas.Sample(atlasSampler, input.uv);
}
