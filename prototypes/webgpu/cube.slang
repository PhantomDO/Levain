// Le cube texturé de M2.2, réduit à l'essentiel, pour le prototype WebGPU : compilé en WGSL par
// slangc (-target wgsl), la cible que Slang dit encore expérimentale.

struct Scene
{
    float4x4 viewProjection;
    float4x4 model;
};

ConstantBuffer<Scene> scene;
Texture2D albedo;
SamplerState albedoSampler;

struct VertexInput
{
    float3 position : POSITION;
    float2 uv : TEXCOORD;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(scene.viewProjection, mul(scene.model, float4(input.position, 1.0)));
    output.uv = input.uv;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    return albedo.Sample(albedoSampler, input.uv);
}
