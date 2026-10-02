// La passe d'ombres (M5.3) : chaque objet est dessiné vu du soleil, dans le quart de l'atlas de sa
// cascade. Seule la profondeur compte : pas de fragment shader.

struct ShadowConstants
{
    float4x4 viewProjection; ///< Celle de la cascade (shadows.cpp, cascadeOf).
    float4x4 model;
};

ConstantBuffer<ShadowConstants> caster : register(b0);

/// Les mêmes sommets que la passe des meshes, dont seule la position sert.
struct VertexInput
{
    float3 position : POSITION;
    float3 instanceOffset : INSTANCE_OFFSET;
};

[shader("vertex")]
float4 vertexMain(VertexInput input) : SV_Position
{
    const float3 world = mul(caster.model, float4(input.position, 1.0)).xyz + input.instanceOffset;
    return mul(caster.viewProjection, float4(world, 1.0));
}
