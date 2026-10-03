// Le lac (M5.7) : un carré d'eau plate, mêlé à l'image derrière lui. Sa surface bouge par ses
// normales, deux lectures de la carte de vaguelettes qui glissent l'une contre l'autre. Ce que
// l'œil en voit :
// - le ciel reflété, selon le Fresnel : presque rien vu d'en haut, presque tout vu de biais ;
// - le reflet du soleil, de la BRDF commune (lighting.slang, ADR-0025), à l'ombre près ;
// - le fond, au travers d'une eau qui l'absorbe d'autant plus qu'elle est profonde, et la lumière
//   que l'eau elle-même diffuse à sa place.
// Le fond est le terrain : la profondeur d'eau se lit dans sa heightmap.

#include "lighting.slang"

/// Doit correspondre à `WaterConstants` (water_pass.cpp).
struct WaterConstants
{
    float4x4 viewProjection;
    float2 center;      ///< Le centre du lac, en mètres.
    float radius;       ///< La moitié du côté du carré d'eau.
    float level;        ///< La hauteur de l'eau.
    float spacing;      ///< Les mètres entre deux échantillons de la heightmap.
    float heightOffset; ///< La hauteur du 0 de la heightmap.
    float heightScale;  ///< Celle de son 1 moins celle de son 0.
    float samples;      ///< Les échantillons sur un côté.
    float seconds;      ///< Le temps de la scène, qui fait glisser les vaguelettes.
    float padding0;
    float padding1;
    float padding2;
};

ConstantBuffer<WaterConstants> water : register(b0, space1);
Texture2D heightmap : register(t0, space1); ///< Celle du terrain : R16 flottant, de 0 à 1.
Texture2D ripples : register(t1, space1);   ///< Les normales des vaguelettes, en espace tangent.
SamplerState heightSampler : register(s0, space1);
SamplerState rippleSampler : register(s1, space1); ///< Répété, anisotrope.

/// Les deux lectures des vaguelettes : leur taille au sol, en mètres, et leur vitesse, en mètres
/// par seconde. Des tailles au rapport non entier, pour que les deux ne retombent jamais ensemble.
static const float RippleTileSize0 = 9.0;
static const float RippleTileSize1 = 23.3;
static const float2 RippleVelocity0 = float2(0.21, 0.08);
static const float2 RippleVelocity1 = float2(-0.09, 0.14);
/// La pente des vaguelettes, réduite de moitié par rapport à la carte : un lac calme.
static const float RippleStrength = 0.5;
/// L'eau réfléchit 2 % de la lumière qui la frappe de face (son indice, 1,33).
static const float WaterF0 = 0.02;
/// La part de la lumière que l'eau absorbe par mètre traversé, par canal : le rouge d'abord, d'où
/// le bleu-vert d'une eau profonde. Une eau claire de lac.
static const float3 Absorption = float3(0.45, 0.12, 0.08);
/// La couleur de ce que l'eau diffuse vers l'œil, à la place du fond qu'elle cache.
static const float3 ScatterColor = float3(0.01, 0.045, 0.05);
/// En deçà de cette profondeur, en mètres, l'eau s'efface : un rivage sans arête.
static const float ShoreFade = 0.15;
/// La rugosité du reflet du soleil : celle des vaguelettes plus fines que la carte.
static const float SunRoughness = 0.08;

/// Les deux triangles du carré, tirés de l'indice du sommet.
static const float2 Corners[6] = { float2(-1.0, -1.0), float2(1.0, -1.0), float2(1.0, 1.0),
                                   float2(-1.0, -1.0), float2(1.0, 1.0),  float2(-1.0, 1.0) };

struct VertexOutput
{
    float4 position : SV_Position;
    float3 worldPosition : POSITION;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertex : SV_VertexID)
{
    const float2 position = water.center + Corners[vertex] * water.radius;
    VertexOutput output;
    output.worldPosition = float3(position.x, water.level, position.y);
    output.position = mul(water.viewProjection, float4(output.worldPosition, 1.0));
    return output;
}

/// La hauteur du terrain en (x, z), comme la lit le shader du terrain (terrain.slang).
float terrainHeightAt(float2 position)
{
    const float2 uv = (position / water.spacing + 0.5) / water.samples;
    return heightmap.SampleLevel(heightSampler, uv, 0.0).r * water.heightScale + water.heightOffset;
}

/// La normale de la surface : deux lectures des vaguelettes, mêlées en « whiteout » (les pentes
/// s'ajoutent), puis passées de l'espace tangent (le haut en z) au monde (le haut en y).
float3 rippleNormalAt(float2 position)
{
    const float2 uv0 = position / RippleTileSize0 + RippleVelocity0 / RippleTileSize0 * water.seconds;
    const float2 uv1 = position / RippleTileSize1 + RippleVelocity1 / RippleTileSize1 * water.seconds;
    const float3 n0 = ripples.Sample(rippleSampler, uv0).rgb * 2.0 - 1.0;
    const float3 n1 = ripples.Sample(rippleSampler, uv1).rgb * 2.0 - 1.0;
    const float3 tangent = normalize(float3((n0.xy + n1.xy) * RippleStrength, n0.z * n1.z));
    return normalize(float3(tangent.x, tangent.z, tangent.y));
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    const float3 position = input.worldPosition;
    // Les vaguelettes avant le `discard` : en WGSL, une lecture qui calcule ses dérivées (Sample)
    // ne doit pas suivre une branche qui dépend des données (build/GOTCHA.md).
    const float3 normal = rippleNormalAt(position.xz);
    const float depth = water.level - terrainHeightAt(position.xz);
    const float shore = saturate(depth / ShoreFade);
    if (shore <= 0.0)
    {
        discard; // le terrain dépasse : pas d'eau ici
    }
    const float3 toEye = normalize(frame.cameraPosition - position);
    const float cosView = saturate(dot(normal, toEye));
    const float fresnel = WaterF0 + (1.0 - WaterF0) * pow(1.0 - cosView, 5.0);

    const float3 reflected = reflect(-toEye, normal);
    const float3 sky =
        specularMap.SampleLevel(environmentSampler, reflected, 0.0).rgb * frame.environmentIntensity;
    const float viewDepth = -mul(frame.view, float4(position, 1.0)).z;
    const float sunVisibility = sunVisibilityOf(position, float3(0.0, 1.0, 0.0), viewDepth);
    const float3 sunLight = frame.sunColor * frame.sunIntensity * sunVisibility;
    // La BRDF commune, sans diffus (albedo noir) : le seul reflet du soleil.
    const float3 sunGlint =
        brdf(normal, toEye, normalize(frame.sunDirection), float3(0.0), 0.0, SunRoughness, sunLight);

    // Le fond se voit au travers de l'eau, le long du rayon de l'œil jusqu'à lui : plus le
    // regard est rasant, plus le chemin est long.
    const float path = depth / max(toEye.y, 0.1);
    const float3 transmittance = exp(-Absorption * path);
    const float transmitted = dot(transmittance, float3(0.2126, 0.7152, 0.0722));
    // Ce que l'eau diffuse, éclairée par le ciel et le soleil, là où elle cache le fond.
    const float3 ambient = irradianceMap.SampleLevel(environmentSampler, float3(0.0, 1.0, 0.0), 0.0).rgb *
                               frame.environmentIntensity +
                           sunLight * saturate(frame.sunDirection.y);
    const float3 scattered = ScatterColor * ambient * (1.0 - transmittance);

    // Mélange prémultiplié : l'image derrière garde (1 − Fresnel) × transmission, la part qui
    // traverse l'eau sans être reflétée ni absorbée.
    const float3 color = (1.0 - fresnel) * scattered + fresnel * sky + sunGlint;
    const float alpha = 1.0 - (1.0 - fresnel) * transmitted;
    return float4(color, alpha) * shore;
}
