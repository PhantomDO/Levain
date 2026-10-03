// Un mesh indexé, éclairé en PBR (M5.1) : matériau glTF metallic-roughness, éclairé par
// lighting.slang (le soleil, les lumières du cluster, le ciel). Les ressources suivent l'ADR-0013 :
// space0 pour ce qui change une fois par frame (lighting.slang), space2 pour ce qui change à chaque
// matériau.

#include "lighting.slang"

/// Les facteurs du matériau (glTF metallic-roughness) : ils multiplient leur texture.
struct MaterialConstants
{
    float4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float padding;
};

ConstantBuffer<MaterialConstants> material : register(b0, space2);
Texture2D baseColorTexture : register(t0, space2);         ///< sRGB : lue en lumière linéaire.
Texture2D metallicRoughnessTexture : register(t1, space2); ///< Rugosité en vert, métal en bleu.
Texture2D normalTexture : register(t2, space2);            ///< Dans l'espace tangent.
SamplerState materialSampler : register(s0, space2);

struct VertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT; ///< w : le sens où croît v.
    float3 color : COLOR;
    float2 uv : TEXCOORD;
    float3 instanceOffset : INSTANCE_OFFSET; ///< Une valeur par instance, pas par sommet.
};

struct VertexOutput
{
    float4 position : SV_Position;
    float3 worldPosition : WORLD_POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    /// La position dans l'espace de découpe, pour retrouver le cluster du pixel sans connaître la
    /// taille de l'image.
    float4 clipPosition : CLIP_POSITION;
    float3 color : COLOR;
    float2 uv : TEXCOORD;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    // mul(matrice, vecteur) : les matrices arrivent de GLM rangées par colonnes, et le shader est
    // compilé avec -matrix-layout-column-major (shaders/CMakeLists.txt).
    VertexOutput output;
    // Toutes les instances tournent ensemble (scene.model), puis chacune se place à sa position.
    const float3 world = mul(scene.model, float4(input.position, 1.0)).xyz + input.instanceOffset;
    output.position = mul(scene.viewProjection, float4(world, 1.0));
    output.worldPosition = world;
    // ponytail: la matrice du modèle tourne la normale telle quelle, juste sans échelle non
    // uniforme ; la transposée de son inverse quand un modèle s'étirera d'un seul côté.
    output.normal = normalize(mul((float3x3)scene.model, input.normal));
    output.tangent =
        float4(normalize(mul((float3x3)scene.model, input.tangent.xyz)), input.tangent.w);
    output.clipPosition = output.position;
    output.color = input.color;
    output.uv = input.uv;
    return output;
}

/// La normale du pixel : celle du sommet, inclinée par la normal map dans l'espace tangent.
float3 shadingNormalOf(VertexOutput input)
{
    const float3 normal = normalize(input.normal);
    const float3 tangent = normalize(input.tangent.xyz - dot(input.tangent.xyz, normal) * normal);
    const float3 bitangent = cross(normal, tangent) * input.tangent.w;
    float3 mapped = normalTexture.Sample(materialSampler, input.uv).xyz * 2.0 - 1.0;
    mapped.xy *= material.normalScale;
    return normalize(tangent * mapped.x + bitangent * mapped.y + normal * mapped.z);
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    // La texture est en sRGB : Sample rend déjà de la lumière linéaire, et une cible sRGB, comme la
    // swapchain, reconvertit à l'écriture.
    const float3 albedo = input.color * material.baseColorFactor.rgb *
                          baseColorTexture.Sample(materialSampler, input.uv).rgb;
    const float4 metallicRoughness = metallicRoughnessTexture.Sample(materialSampler, input.uv);
    // Une rugosité nulle ferait un reflet infiniment petit, et des pixels isolés trop brillants.
    const Surface surface = {
        input.worldPosition,
        input.clipPosition,
        shadingNormalOf(input),
        input.normal,
        albedo,
        saturate(material.metallicFactor * metallicRoughness.b),
        clamp(material.roughnessFactor * metallicRoughness.g, 0.045, 1.0),
    };
    return float4(shadeSurface(surface), 1.0);
}
