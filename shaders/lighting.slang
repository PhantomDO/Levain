// L'éclairage de la scène, partagé par tous les shaders qui dessinent une surface éclairée : les meshes
// (mesh.slang) et les passes des plugins, comme le terrain (ADR-0025). Les ressources de space0 (ADR-0013),
// que décrit `FrameBindings` (frame.hpp), et les fonctions d'éclairage : le soleil et ses ombres, les
// lumières ponctuelles de son cluster (forward+, ADR-0024) et le ciel (l'IBL, M5.4). Un contrat entre le
// moteur et ses plugins : le changer, c'est changer les plugins dans la même PR.
// Inclus, jamais compilé seul.

#include "brdf.slang"

/// Doit valoir MaxLightsPerCluster (light_clusters.hpp).
static const uint MaxLightsPerCluster = 32;
/// Doit valoir SpecularMips (environment.hpp).
static const uint SpecularMips = 6;

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
    /// Des scalaires et non un float3 de remplissage : en WGSL, un float3 après un scalaire ne
    /// tombe pas au même endroit (build/GOTCHA.md).
    float environmentIntensity;
    float padding0;
    float padding1;
    float padding2;
    uint3 clusterGrid;
    uint padding3;
    /// Les ombres du soleil (M5.3) : la projection de chaque cascade, la profondeur où elle finit,
    /// et la taille d'un de ses texels, dans le monde.
    float4x4 cascadeViewProjection[4];
    float4 cascadeFarDepths;
    float4 cascadeTexelSizes;
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
/// DepthTexture2D et non Texture2D : en WGSL, seule une texture de profondeur se compare
/// (build/GOTCHA.md).
DepthTexture2D shadowAtlas : register(t3, space0);
SamplerComparisonState shadowSampler : register(s0, space0);
/// L'éclairage par l'image (environment.hpp) : l'irradiance, le reflet du ciel par rugosité et la
/// table de la BRDF.
TextureCube irradianceMap : register(t4, space0);
TextureCube specularMap : register(t5, space0);
Texture2D brdfLut : register(t6, space0);
SamplerState environmentSampler : register(s1, space0);

/// La lumière réfléchie vers l'œil pour une lumière venue de `toLight`, d'éclairement `radiance`,
/// selon l'annexe B de la spécification glTF : un diélectrique (F0 de 0,04) mêle le diffus de Lambert
/// et le reflet de Cook-Torrance selon son Fresnel, un métal ne fait que refléter, teinté par sa
/// couleur, et `metallic` passe de l'un à l'autre. Mélanger les deux BRDF, et non leurs F0 : avec un
/// F0 mélangé, le diffus perdait deux fois la part du métal, (1 − métal) et (1 − F), et une surface à
/// moitié métallique sortait trop sombre (#125).
float3 brdf(float3 normal, float3 toEye, float3 toLight, float3 albedo, float metallic,
            float roughness, float3 radiance)
{
    const float3 halfway = normalize(toEye + toLight);
    const float nDotL = saturate(dot(normal, toLight));
    const float nDotV = max(dot(normal, toEye), 1e-4);
    const float nDotH = saturate(dot(normal, halfway));
    const float hDotV = saturate(dot(halfway, toEye));

    const float alpha = roughness * roughness;
    const float specular =
        ggxDistributionOf(nDotH, alpha) * smithVisibilityOf(nDotV, max(nDotL, 1e-4), alpha);
    const float schlick = pow(1.0 - hDotV, 5.0);
    const float dielectricFresnel = 0.04 + 0.96 * schlick;
    const float3 metalFresnel = albedo + (1.0 - albedo) * schlick;
    const float3 dielectric = lerp(albedo / Pi, float3(specular), dielectricFresnel);
    const float3 metal = metalFresnel * specular;
    return lerp(dielectric, metal, metallic) * radiance * nDotL;
}

/// L'atténuation d'une lumière ponctuelle : l'inverse du carré de la distance, ramené à zéro en
/// douceur à sa portée (la fenêtre de KHR_lights_punctual).
float attenuationOf(float distance, float range)
{
    const float ratio = distance / range;
    const float window = saturate(1.0 - ratio * ratio * ratio * ratio);
    return window * window / max(distance * distance, 1e-4);
}

/// La part du soleil qui atteint le point : 1 en pleine lumière, 0 dans l'ombre. Le point se projette
/// dans la shadow map de la cascade de sa profondeur (un quart de l'atlas, shadows.hpp), décalé le
/// long de sa normale d'un texel et demi, contre l'« acné ». Le bord de l'ombre est adouci par un
/// PCF : 3 × 3 comparaisons voisines, chacune déjà filtrée par le GPU sur 2 × 2 texels.
float sunVisibilityOf(float3 worldPosition, float3 normal, float viewDepth)
{
    if (viewDepth >= frame.cascadeFarDepths[3])
    {
        return 1.0; // au-delà de la distance d'ombre
    }
    uint cascade = 0;
    while (cascade < 3 && viewDepth >= frame.cascadeFarDepths[cascade])
    {
        ++cascade;
    }
    const float3 biased = worldPosition + normal * (1.5 * frame.cascadeTexelSizes[cascade]);
    const float4 clip = mul(frame.cascadeViewProjection[cascade], float4(biased, 1.0));
    const float2 uv = float2(clip.x * 0.5 + 0.5, 0.5 - clip.y * 0.5);
    const float2 cell = float2(cascade % 2, cascade / 2);

    uint width;
    uint height;
    shadowAtlas.GetDimensions(width, height);
    const float2 texel = 1.0 / float2(width, height);
    // Les échantillons restent dans le quart de leur cascade : un texel et demi de marge au bord, que
    // le filtre 2 × 2 ne déborde pas sur la cascade voisine.
    const float2 cellMin = cell * 0.5 + 1.5 * texel;
    const float2 cellMax = (cell + 1.0) * 0.5 - 1.5 * texel;
    const float2 center = (cell + saturate(uv)) * 0.5;
    float lit = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            const float2 tap = clamp(center + float2(x, y) * texel, cellMin, cellMax);
            lit += shadowAtlas.SampleCmpLevelZero(shadowSampler, tap, clip.z);
        }
    }
    return lit / 9.0;
}

/// La part du ciel que la surface reflète, pour une couleur spéculaire `f0` : la table de la BRDF
/// (la « split sum » de Karis), un Fresnel qui tient compte de la rugosité, et l'énergie des rebonds
/// multiples entre micro-facettes (Fdez-Agüera 2019), que la table ne voit pas : sans elle, un métal
/// rugueux perd jusqu'au tiers de sa lumière. Le calcul de la spécification glTF (annexe B) et du
/// glTF Sample Viewer, celui des mesures de #131.
float3 iblFresnelOf(float cosView, float roughness, float3 f0)
{
    const float2 scaleBias =
        brdfLut.SampleLevel(environmentSampler, float2(cosView, roughness), 0.0).rg;
    const float3 fresnel = f0 + (max(float3(1.0 - roughness), f0) - f0) * pow(1.0 - cosView, 5.0);
    const float3 singleScattering = fresnel * scaleBias.x + scaleBias.y;
    const float missing = 1.0 - (scaleBias.x + scaleBias.y);
    const float3 averageFresnel = f0 + (1.0 - f0) / 21.0;
    return singleScattering +
           missing * singleScattering * averageFresnel / (1.0 - averageFresnel * missing);
}

/// Ce que le ciel renvoie vers l'œil : le reflet préfiltré à la rugosité du matériau, et
/// l'irradiance pour ce que la surface diffuse. Un diélectrique (F0 de 0,04) mêle les deux selon son
/// Fresnel, un métal ne fait que refléter, teinté par sa couleur ; `metallic` passe de l'un à
/// l'autre.
float3 environmentLightOf(float3 normal, float3 toEye, float3 albedo, float metallic,
                          float roughness)
{
    const float cosView = saturate(dot(normal, toEye));
    const float3 reflected = reflect(-toEye, normal);
    const float3 specular =
        specularMap.SampleLevel(environmentSampler, reflected, roughness * float(SpecularMips - 1))
            .rgb;
    const float3 irradiance = irradianceMap.SampleLevel(environmentSampler, normal, 0.0).rgb;
    const float3 dielectric =
        lerp(albedo * irradiance, specular, iblFresnelOf(cosView, roughness, float3(0.04)));
    const float3 metal = iblFresnelOf(cosView, roughness, albedo) * specular;
    return lerp(dielectric, metal, metallic) * frame.environmentIntensity;
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

/// Ce que voit l'œil d'une surface : sa position, ses deux normales et son matériau.
struct Surface
{
    float3 worldPosition;
    float4 clipPosition;     ///< Pour retrouver son cluster.
    float3 normal;           ///< Celle de l'éclairage, normal map comprise.
    float3 geometricNormal;  ///< Celle du triangle, pour décaler la lecture des ombres.
    float3 albedo;
    float metallic;
    float roughness;
};

/// La lumière que la surface renvoie vers l'œil : le ciel, le soleil et son ombre, et les lumières
/// ponctuelles que le tri a rangées dans son cluster.
float3 shadeSurface(Surface surface)
{
    const float3 toEye = normalize(frame.cameraPosition - surface.worldPosition);
    float3 color = environmentLightOf(surface.normal, toEye, surface.albedo, surface.metallic,
                                      surface.roughness);
    const float viewDepth = -mul(frame.view, float4(surface.worldPosition, 1.0)).z;
    const float sunVisibility =
        sunVisibilityOf(surface.worldPosition, normalize(surface.geometricNormal), viewDepth);
    color += brdf(surface.normal, toEye, normalize(frame.sunDirection), surface.albedo,
                  surface.metallic, surface.roughness,
                  frame.sunColor * frame.sunIntensity * sunVisibility);

    const uint cluster = clusterOf(surface.clipPosition, surface.worldPosition);
    const uint count = min(clusterLightCounts[cluster], MaxLightsPerCluster);
    for (uint i = 0; i < count; ++i)
    {
        const PointLight light = pointLights[clusterLightIndices[cluster * MaxLightsPerCluster + i]];
        const float3 offset = light.position - surface.worldPosition;
        const float distance = length(offset);
        const float3 radiance = light.color * light.intensity * attenuationOf(distance, light.range);
        color += brdf(surface.normal, toEye, offset / distance, surface.albedo, surface.metallic,
                      surface.roughness, radiance);
    }
    return color;
}
