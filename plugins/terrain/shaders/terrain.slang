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

/// Les trois couches (layer_textures.hpp), dans l'ordre des canaux de la carte de poids.
Texture2DArray layerAlbedo : register(t2, space1);    ///< sRGB : lue en lumière linéaire.
Texture2DArray layerNormal : register(t3, space1);    ///< L'espace tangent, le vert vers le haut.
Texture2DArray layerRoughness : register(t4, space1); ///< Dans le rouge.
SamplerState layerSampler : register(s1, space1);     ///< Répété, anisotrope.

/// Le côté, en mètres, d'une répétition des textures au sol, et celui d'une seconde lecture de la
/// couleur, plus large : leur moyenne casse la trame que la répétition dessine vue de loin. Un
/// rapport non entier, pour que les deux répétitions ne retombent jamais ensemble.
static const float LayerTileSize = 4.0;
static const float LayerMacroTileSize = 29.3;

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
    const float3 surfaceNormal = normalize(float3(-slopeX, 1.0, -slopeZ));
    const float3 weights = weightMap.Sample(terrainSampler, heightmapUvOf(position)).rgb;
    const float3 normalizedWeights = weights / max(weights.r + weights.g + weights.b, 1e-3);

    // Les couches, plaquées vues du ciel (x et z du monde).
    // ponytail: projection plane, étirée dans les pentes raides (×1,4 à 45°) ; un plaquage
    // triplanaire pour la roche si les falaises le demandent.
    const float2 uv = position / LayerTileSize;
    const float2 macroUv = position / LayerMacroTileSize;
    // Les dérivées hors de la boucle : en WGSL, une lecture qui les calcule elle-même (Sample) est
    // interdite dans une branche qui dépend des données, comme le saut des couches sans poids.
    const float2 uvDx = ddx(uv);
    const float2 uvDy = ddy(uv);
    const float2 macroDx = ddx(macroUv);
    const float2 macroDy = ddy(macroUv);
    float3 albedo = 0.0;
    float3 tangentNormal = 0.0;
    float roughness = 0.0;
    for (uint layer = 0; layer < 3; ++layer)
    {
        const float weight = normalizedWeights[layer];
        // La plupart des pixels n'ont qu'une ou deux couches : ne pas lire les autres.
        if (weight < 1e-3)
        {
            continue;
        }
        const float3 coordinates = float3(uv, float(layer));
        const float3 macroCoordinates = float3(macroUv, float(layer));
        albedo += weight * 0.5 *
                  (layerAlbedo.SampleGrad(layerSampler, coordinates, uvDx, uvDy).rgb +
                   layerAlbedo.SampleGrad(layerSampler, macroCoordinates, macroDx, macroDy).rgb);
        tangentNormal +=
            weight *
            (layerNormal.SampleGrad(layerSampler, coordinates, uvDx, uvDy).rgb * 2.0 - 1.0);
        roughness += weight * layerRoughness.SampleGrad(layerSampler, coordinates, uvDx, uvDy).r;
    }
    // L'espace tangent du plaquage : u le long de x, v le long de z, la normale de la pente.
    const float3 tangent = normalize(float3(1.0, 0.0, 0.0) -
                                     surfaceNormal * dot(surfaceNormal, float3(1.0, 0.0, 0.0)));
    const float3 bitangent = cross(tangent, surfaceNormal);
    const float3 normal = normalize(tangent * tangentNormal.x + bitangent * tangentNormal.y +
                                    surfaceNormal * max(tangentNormal.z, 1e-3));
    const Surface surface = {
        input.worldPosition, input.clipPosition, normal, surfaceNormal, albedo, 0.0,
        clamp(roughness, 0.045, 1.0),
    };
    return float4(shadeSurface(surface), 1.0);
}

/// L'ombre portée : la même parcelle, vue du soleil, la profondeur seule.
[shader("vertex")]
float4 shadowMain(float3 local : POSITION) : SV_Position
{
    return mul(terrain.viewProjection, float4(worldPositionOf(local), 1.0));
}
