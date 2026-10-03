// Le terrain (M5.6) : une parcelle de la grille, posée sur la heightmap. Ses sommets ne portent que
// leur place dans la parcelle (de 0 à 1) ; la hauteur se lit dans la heightmap, la normale aussi, au
// pixel, et l'éclairage est celui de toute la scène (lighting.slang, ADR-0025). Les ressources du
// terrain occupent space1, celles de la passe (ADR-0013).

#include "lighting.slang"

/// Doit correspondre à `TerrainConstants` (terrain_pass.cpp). Des scalaires après la matrice, pas
/// de float3 (build/GOTCHA.md).
struct TerrainConstants
{
    float4x4 viewProjection; ///< Celle de la caméra, ou de la cascade pour l'ombre.
    float2 patchOrigin;      ///< Le coin de la parcelle, en mètres.
    float patchSize;         ///< Son côté, en mètres.
    float spacing;           ///< Les mètres entre deux échantillons de la heightmap.
    float heightOffset;      ///< La hauteur du 0 de la heightmap.
    float heightScale;       ///< Celle de son 1 moins celle de son 0.
    float samples;           ///< Les échantillons sur un côté.
    float skirtDepth;        ///< De combien la jupe descend sous le bord.
};

ConstantBuffer<TerrainConstants> terrain : register(b0, space1);
Texture2D heightmap : register(t0, space1);  ///< R16 flottant, de 0 à 1, filtré.
Texture2D weightMap : register(t1, space1);  ///< Herbe, sol rocailleux, roche.
SamplerState terrainSampler : register(s0, space1);

/// Le point de la heightmap à la position (x, z) : le centre d'un texel est un échantillon.
float2 heightmapUvOf(float2 position)
{
    return (position / terrain.spacing + 0.5) / terrain.samples;
}

float heightAt(float2 position)
{
    return heightmap.SampleLevel(terrainSampler, heightmapUvOf(position), 0.0).r *
               terrain.heightScale +
           terrain.heightOffset;
}

/// La position, dans le monde, du sommet de la parcelle : `local.xy` de 0 à 1 sur ses côtés, et
/// `local.z` à 1 pour un sommet de jupe, descendu sous le bord pour cacher la fente avec une parcelle
/// d'un autre niveau de détail.
float3 worldPositionOf(float3 local)
{
    const float2 position = terrain.patchOrigin + local.xy * terrain.patchSize;
    return float3(position.x, heightAt(position) - local.z * terrain.skirtDepth, position.y);
}

struct VertexOutput
{
    float4 position : SV_Position;
    float3 worldPosition : POSITION;
    float4 clipPosition : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertexMain(float3 local : POSITION)
{
    VertexOutput output;
    output.worldPosition = worldPositionOf(local);
    output.position = mul(terrain.viewProjection, float4(output.worldPosition, 1.0));
    output.clipPosition = output.position;
    return output;
}

/// La couleur de chaque couche, en attendant leurs textures : des albédos de sol réels, en lumière
/// linéaire (une herbe renvoie moins d'un dixième de la lumière, une roche claire un cinquième).
static const float3 GrassColor = float3(0.05, 0.09, 0.02);
static const float3 StonyColor = float3(0.12, 0.10, 0.08);
static const float3 RockColor = float3(0.20, 0.19, 0.17);

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    // La normale à la pente, entre les échantillons voisins : la même à tous les niveaux de détail.
    const float2 position = input.worldPosition.xz;
    const float step = terrain.spacing;
    const float slopeX =
        (heightAt(position + float2(step, 0.0)) - heightAt(position - float2(step, 0.0))) /
        (2.0 * step);
    const float slopeZ =
        (heightAt(position + float2(0.0, step)) - heightAt(position - float2(0.0, step))) /
        (2.0 * step);
    const float3 normal = normalize(float3(-slopeX, 1.0, -slopeZ));
    const float3 weights = weightMap.Sample(terrainSampler, heightmapUvOf(position)).rgb;
    const float3 albedo =
        (weights.r * GrassColor + weights.g * StonyColor + weights.b * RockColor) /
        max(weights.r + weights.g + weights.b, 1e-3);
    const Surface surface = {
        input.worldPosition, input.clipPosition, normal, normal, albedo, 0.0, 0.9,
    };
    return float4(shadeSurface(surface), 1.0);
}

/// L'ombre portée : la même parcelle, vue du soleil, la profondeur seule.
[shader("vertex")]
float4 shadowMain(float3 local : POSITION) : SV_Position
{
    return mul(terrain.viewProjection, float4(worldPositionOf(local), 1.0));
}
