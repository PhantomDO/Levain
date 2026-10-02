// Un mesh indexé, éclairé en PBR (M5.1) : matériau glTF metallic-roughness, BRDF de Cook-Torrance,
// un soleil et les lumières ponctuelles de son cluster (forward+, ADR-0024). Les ressources suivent
// l'ADR-0013 : space0 pour ce qui change une fois par frame, space2 pour ce qui change à chaque
// matériau.

static const float Pi = 3.14159265;

/// Doit valoir MaxLightsPerCluster (light_clusters.hpp).
static const uint MaxLightsPerCluster = 32;

struct SceneConstants
{
    float4x4 viewProjection;
    float4x4 model;
};

/// L'éclairage de l'image. Doit correspondre à `FrameConstants` dans `mesh_pass.cpp`.
struct FrameConstants
{
    float4x4 view;
    float3 cameraPosition;
    float sunIntensity;
    float3 sunDirection; ///< Vers le soleil.
    float nearPlane;
    float3 sunColor;
    float farPlane;
    float3 ambient; ///< En attendant l'IBL (M5.4).
    uint padding;
    uint3 clusterGrid;
    uint padding2;
};

struct PointLight
{
    float3 position;
    float range;
    float3 color;
    float intensity;
};

ConstantBuffer<SceneConstants> scene : register(b0, space0);
ConstantBuffer<FrameConstants> frame : register(b1, space0);
StructuredBuffer<PointLight> pointLights : register(t0, space0);
StructuredBuffer<uint> clusterLightCounts : register(t1, space0);
StructuredBuffer<uint> clusterLightIndices : register(t2, space0);

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

/// La lumière réfléchie vers l'œil pour une lumière venue de `toLight`, d'éclairement `radiance` :
/// Cook-Torrance (distribution GGX, ombrage de Smith, Fresnel de Schlick), plus le diffus de
/// Lambert pour ce qui n'est pas réfléchi.
float3 brdf(float3 normal, float3 toEye, float3 toLight, float3 albedo, float metallic,
            float roughness, float3 radiance)
{
    const float3 halfway = normalize(toEye + toLight);
    const float nDotL = saturate(dot(normal, toLight));
    const float nDotV = max(dot(normal, toEye), 1e-4);
    const float nDotH = saturate(dot(normal, halfway));
    const float hDotV = saturate(dot(halfway, toEye));

    const float alpha = roughness * roughness;
    const float alpha2 = alpha * alpha;
    const float denominator = nDotH * nDotH * (alpha2 - 1.0) + 1.0;
    const float distribution = alpha2 / (Pi * denominator * denominator);
    const float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    const float geometry = (nDotV / (nDotV * (1.0 - k) + k)) * (nDotL / (nDotL * (1.0 - k) + k));
    // Un diélectrique réfléchit 4 % de face ; un métal, sa couleur.
    const float3 f0 = lerp(float3(0.04), albedo, metallic);
    const float3 fresnel = f0 + (1.0 - f0) * pow(1.0 - hDotV, 5.0);

    const float3 specular = distribution * geometry * fresnel / (4.0 * nDotV * max(nDotL, 1e-4));
    const float3 diffuse = (1.0 - fresnel) * (1.0 - metallic) * albedo / Pi;
    return (diffuse + specular) * radiance * nDotL;
}

/// L'atténuation d'une lumière ponctuelle : l'inverse du carré de la distance, ramené à zéro en
/// douceur à sa portée (la fenêtre de KHR_lights_punctual).
float attenuationOf(float distance, float range)
{
    const float ratio = distance / range;
    const float window = saturate(1.0 - ratio * ratio * ratio * ratio);
    return window * window / max(distance * distance, 1e-4);
}

/// L'indice du cluster du pixel, par sa position à l'écran et sa profondeur : le calcul inverse de
/// celui du tri (shaders/light_clusters.slang).
uint clusterOf(float4 clipPosition, float3 worldPosition)
{
    const uint3 grid = frame.clusterGrid;
    const float2 ndc = clipPosition.xy / clipPosition.w;
    const float depth = -mul(frame.view, float4(worldPosition, 1.0)).z;
    const float slice = log(depth / frame.nearPlane) / log(frame.farPlane / frame.nearPlane);
    const uint2 cellXY = min(uint2(saturate(ndc * 0.5 + 0.5) * float2(grid.xy)), grid.xy - 1);
    const uint cellZ = min(uint(saturate(slice) * float(grid.z)), grid.z - 1);
    return cellXY.x + grid.x * (cellXY.y + grid.y * cellZ);
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    // La texture est en sRGB : Sample rend déjà de la lumière linéaire, et une cible sRGB, comme la
    // swapchain, reconvertit à l'écriture.
    const float3 albedo = input.color * material.baseColorFactor.rgb *
                          baseColorTexture.Sample(materialSampler, input.uv).rgb;
    const float4 metallicRoughness = metallicRoughnessTexture.Sample(materialSampler, input.uv);
    const float metallic = saturate(material.metallicFactor * metallicRoughness.b);
    // Une rugosité nulle ferait un reflet infiniment petit, et des pixels isolés trop brillants.
    const float roughness = clamp(material.roughnessFactor * metallicRoughness.g, 0.045, 1.0);
    const float3 normal = shadingNormalOf(input);
    const float3 toEye = normalize(frame.cameraPosition - input.worldPosition);

    float3 color = frame.ambient * albedo;
    color += brdf(normal, toEye, normalize(frame.sunDirection), albedo, metallic, roughness,
                  frame.sunColor * frame.sunIntensity);

    // Les lumières ponctuelles du cluster : seules celles que le tri y a rangées.
    const uint cluster = clusterOf(input.clipPosition, input.worldPosition);
    const uint count = min(clusterLightCounts[cluster], MaxLightsPerCluster);
    for (uint i = 0; i < count; ++i)
    {
        const PointLight light = pointLights[clusterLightIndices[cluster * MaxLightsPerCluster + i]];
        const float3 offset = light.position - input.worldPosition;
        const float distance = length(offset);
        const float3 radiance = light.color * light.intensity * attenuationOf(distance, light.range);
        color += brdf(normal, toEye, offset / distance, albedo, metallic, roughness, radiance);
    }
    // ponytail: la lumière au-delà de 1 est coupée par la cible 8 bits ; le HDR et le tonemapping
    // viennent en M5.2.
    return float4(color, 1.0);
}
