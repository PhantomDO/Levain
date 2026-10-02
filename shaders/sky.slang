// Le ciel en fond (M5.4) : un triangle qui couvre l'image au plan lointain, et chaque pixel lit la
// cubemap de l'environnement dans la direction où il regarde. Dessiné après les meshes, il ne
// colore que les pixels où rien n'a été dessiné : le test de profondeur écarte les autres.

/// Du bourrage en scalaires, pas en `float3` (build/GOTCHA.md).
struct SkyConstants
{
    /// De l'écran vers les directions du monde : la projection et la seule rotation de la vue,
    /// inversées. Sans la translation, le ciel reste à l'infini quand la caméra avance.
    float4x4 inverseViewProjection;
    float intensity;
    float padding0;
    float padding1;
    float padding2;
};

ConstantBuffer<SkyConstants> sky : register(b0);
TextureCube environment : register(t0);
SamplerState skySampler : register(s0);

struct VertexOutput
{
    float4 position : SV_Position;
    float2 ndc : TEXCOORD;
};

/// Le triangle de la passe de tonemapping (shaders/tonemap.slang), à la profondeur 1 : le plan
/// lointain, que le depth buffer garde là où aucun mesh n'est passé.
[shader("vertex")]
VertexOutput vertexMain(uint vertexId: SV_VertexID)
{
    const float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    VertexOutput output;
    output.ndc = uv * float2(2.0, -2.0) + float2(-1.0, 1.0);
    output.position = float4(output.ndc, 1.0, 1.0);
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    const float4 far = mul(sky.inverseViewProjection, float4(input.ndc, 1.0, 1.0));
    const float3 direction = normalize(far.xyz / far.w);
    return float4(environment.SampleLevel(skySampler, direction, 0.0).rgb * sky.intensity, 1.0);
}
