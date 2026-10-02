// La passe de tonemapping (M5.2) : un triangle qui couvre l'image, et chaque pixel lit la lumière de
// la scène dans l'image HDR, l'expose, et la ramène dans ce que l'écran sait afficher.

/// Trois `float` de bourrage, pas un `float3` : en WGSL, un `float3` s'aligne sur 16 octets, et la
/// structure en ferait 32 au lieu des 16 du C++ (build/GOTCHA.md).
struct TonemapConstants
{
    float exposure;
    float padding0;
    float padding1;
    float padding2;
};

ConstantBuffer<TonemapConstants> settings : register(b0);
Texture2D hdrImage : register(t0);
SamplerState hdrSampler : register(s0);

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD;
};

/// Un seul triangle, deux fois plus grand que l'écran : il le couvre entièrement, sans la diagonale
/// de deux triangles (que le GPU dessinerait deux fois).
[shader("vertex")]
VertexOutput vertexMain(uint vertexId: SV_VertexID)
{
    VertexOutput output;
    output.uv = float2((vertexId << 1) & 2, vertexId & 2);
    output.position = float4(output.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    const float3 light = hdrImage.Sample(hdrSampler, input.uv).rgb * settings.exposure;
    // ponytail: au-delà de 1, la lumière est coupée net ; AgX ou ACES la ramèneront en douceur
    // (#127).
    return float4(saturate(light), 1.0);
}
