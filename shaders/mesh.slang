// Un mesh indexé, coloré par sommet et texturé (M2.2), avec un matériau glTF metallic-roughness
// (M5.1). Les ressources suivent l'ADR-0013 : space0 pour ce qui change une fois par frame, space2
// pour ce qui change à chaque matériau.

struct SceneConstants
{
    float4x4 viewProjection;
    float4x4 model;
};

ConstantBuffer<SceneConstants> scene : register(b0, space0);

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
    float3 normal : NORMAL; ///< Dans le repère du monde ; l'éclairage viendra le lire (M5.1).
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
    // ponytail: la matrice du modèle tourne la normale telle quelle, juste sans échelle non
    // uniforme ; la transposée de son inverse quand un modèle s'étirera d'un seul côté.
    output.normal = normalize(mul((float3x3)scene.model, input.normal));
    output.color = input.color;
    output.uv = input.uv;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    // La texture est en sRGB : Sample rend déjà de la lumière linéaire, et une cible sRGB, comme la
    // swapchain, reconvertit à l'écriture.
    const float4 baseColor =
        material.baseColorFactor * baseColorTexture.Sample(materialSampler, input.uv);
    return float4(input.color * baseColor.rgb, 1.0);
}
