// Les lignes de debug (M6.2, ADR-0027) : des segments colorés, dans le monde, par-dessus l'image. Les
// formes de collision, le rayon d'une sélection ; demain la caméra et l'IA. Le `DrawDebugLine`
// d'Unreal, le `Debug.DrawLine` d'Unity.

struct DebugLineConstants
{
    float4x4 viewProjection;
};

ConstantBuffer<DebugLineConstants> lines : register(b0);

struct VertexInput
{
    float3 position : POSITION;
    float3 color : COLOR;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float3 color : COLOR;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(lines.viewProjection, float4(input.position, 1.0));
    output.color = input.color;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    return float4(input.color, 1.0);
}
