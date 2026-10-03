// L'herbe (M5.7) : une forme de brin, instanciée des milliers de fois par parcelle du terrain, sans
// buffer d'instances. Le numéro d'instance donne la place du brin dans sa parcelle (une suite à
// faible discrépance, qui couvre la parcelle régulièrement quel que soit le nombre de brins), et un
// hachage donne le reste : son orientation, sa taille, sa teinte, et s'il pousse là, selon la carte
// de densité. Le vent le courbe. L'éclairage est celui de la scène (lighting.slang, ADR-0025).

#include "lighting.slang"

/// Doit correspondre à `GrassConstants` (grass_pass.cpp).
struct GrassConstants
{
    float4x4 viewProjection;
    float2 patchOrigin; ///< Le coin de la parcelle, en mètres.
    float patchSize;    ///< Son côté, en mètres.
    float spacing;      ///< Les mètres entre deux échantillons de la heightmap.
    float heightOffset; ///< La hauteur du 0 de la heightmap.
    float heightScale;  ///< Celle de son 1 moins celle de son 0.
    float samples;      ///< Les échantillons sur un côté.
    float seconds;      ///< Le temps de la scène, qui fait souffler le vent.
    float range;        ///< Au-delà, plus d'herbe.
    uint patchIndex;    ///< La graine de la parcelle.
    float padding0;
    float padding1;
};

ConstantBuffer<GrassConstants> grass : register(b0, space1);
Texture2D heightmap : register(t0, space1); ///< Celle du terrain : R16 flottant, de 0 à 1.
Texture2D density : register(t1, space1);   ///< De 0 à 1 : la part des brins qui poussent.
SamplerState terrainSampler : register(s0, space1);

/// La taille d'un brin, en mètres : la hauteur entre ses deux bornes, selon le hachage.
static const float MinBladeHeight = 0.3;
static const float MaxBladeHeight = 0.7;
static const float BladeWidth = 0.08;
/// Le décalage d'un brin autour de son point de la suite, en mètres : la trame de la suite ne se
/// voit plus quand une parcelle lointaine a peu de brins.
static const float Jitter = 0.2;
/// Le vent : sa direction au sol, la vitesse de ses rafales, et de combien il couche une pointe.
static const float2 WindDirection = float2(0.8, 0.6);
static const float GustSpeed = 1.3;
static const float GustSize = 0.08; ///< Rafales par mètre, le long du vent.
static const float MaxBend = 0.35;
/// Les teintes de la base, à l'ombre des autres brins, et de la pointe, plus jaune.
static const float3 RootColor = float3(0.05, 0.09, 0.02);
static const float3 TipColor = float3(0.30, 0.42, 0.08);
/// Mate : un brin diffuse plus qu'il ne reflète le ciel.
static const float BladeRoughness = 0.9;

/// Un hachage d'entier (PCG) : des bits bien mêlés, les mêmes pour la même entrée.
uint hashOf(uint value)
{
    const uint state = value * 747796405u + 2891336453u;
    const uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

/// Un nombre de [0, 1) tiré de `value`.
float unitOf(uint value)
{
    return float(hashOf(value) >> 8) / 16777216.0;
}

/// La suite R2 de Roberts : le point `index` de la suite, de 0 à 1 sur deux axes. Ses premiers points
/// couvrent le carré régulièrement, quel qu'en soit le nombre : une parcelle lointaine en dessine
/// moins sans faire de trou. En virgule fixe sur 32 bits, exacte jusqu'au dernier brin.
float2 r2PointOf(uint index)
{
    const uint2 fixedPoint = uint2(index * 3242174889u, index * 2447445414u);
    return float2(fixedPoint >> 8) / 16777216.0;
}

float terrainHeightAt(float2 position)
{
    const float2 uv = (position / grass.spacing + 0.5) / grass.samples;
    return heightmap.SampleLevel(terrainSampler, uv, 0.0).r * grass.heightScale + grass.heightOffset;
}

float densityAt(float2 position)
{
    const float2 uv = (position / grass.spacing + 0.5) / grass.samples;
    return density.SampleLevel(terrainSampler, uv, 0.0).r;
}

struct VertexOutput
{
    float4 position : SV_Position;
    float3 worldPosition : POSITION;
    float4 clipPosition : TEXCOORD0;
    float3 normal : NORMAL;
    float3 color : COLOR;
};

[shader("vertex")]
VertexOutput vertexMain(float2 local : POSITION, uint instance : SV_InstanceID)
{
    VertexOutput output;
    // La graine du brin : son numéro, dans sa parcelle. Un décalage de la suite par parcelle, pour
    // que les parcelles ne se ressemblent pas.
    const uint seed = hashOf(instance ^ hashOf(grass.patchIndex));
    const float2 offset = float2(unitOf(grass.patchIndex * 2u), unitOf(grass.patchIndex * 2u + 1u));
    const float2 jitter = (float2(unitOf(seed + 6u), unitOf(seed + 7u)) - 0.5) * Jitter;
    const float2 base =
        grass.patchOrigin + frac(r2PointOf(instance) + offset) * grass.patchSize + jitter;

    // Plus près de la portée, plus petit : l'herbe s'efface au lieu de s'arrêter net.
    const float3 root = float3(base.x, terrainHeightAt(base), base.y);
    const float fade = saturate((grass.range - distance(root, frame.cameraPosition)) / (0.25 * grass.range));
    const bool grows = unitOf(seed) < densityAt(base) && fade > 0.0;
    if (!grows)
    {
        // Hors de l'écran, et réduit à un point : le GPU écarte ses triangles sans les dessiner.
        output.position = float4(2.0, 2.0, 2.0, 1.0);
        output.worldPosition = root;
        output.clipPosition = output.position;
        output.normal = float3(0.0, 1.0, 0.0);
        output.color = 0.0;
        return output;
    }
    const float height = lerp(MinBladeHeight, MaxBladeHeight, unitOf(seed + 1u)) * fade;
    const float angle = unitOf(seed + 2u) * 6.2831853;
    const float3 across = float3(cos(angle), 0.0, sin(angle));
    const float3 facing = float3(-across.z, 0.0, across.x);

    // Le vent : une onde de rafales qui avance le long de sa direction, et un frémissement propre à
    // chaque brin. La courbure croît comme le carré de la hauteur : la base ne bouge pas.
    const float phase = grass.seconds * GustSpeed - dot(base, WindDirection) * GustSize * 6.2831853;
    const float gust = 0.6 + 0.4 * sin(phase);
    const float flutter = 0.15 * sin(grass.seconds * 4.0 + unitOf(seed + 3u) * 6.2831853);
    const float bend = MaxBend * (gust + flutter) * local.y * local.y;
    const float3 wind = float3(WindDirection.x, 0.0, WindDirection.y);

    output.worldPosition = root + across * (local.x * BladeWidth * (1.0 + 0.5 * unitOf(seed + 4u))) +
                           float3(0.0, local.y * height * (1.0 - 0.5 * bend * bend), 0.0) +
                           wind * (bend * height);
    output.position = mul(grass.viewProjection, float4(output.worldPosition, 1.0));
    output.clipPosition = output.position;
    // Une normale arrondie en travers du brin, et penchée vers le ciel : un brin éclairé comme un
    // cylindre aplati, et non comme une carte plate.
    output.normal = normalize(facing + across * (local.x * 1.6) + float3(0.0, 1.0, 0.0));
    const float shade = lerp(0.85, 1.15, unitOf(seed + 5u));
    output.color = lerp(RootColor, TipColor, local.y) * shade;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input, bool isFrontFace : SV_IsFrontFace) : SV_Target
{
    // Vu de l'autre côté, le brin montre son revers : la normale se retourne, sauf vers le haut.
    float3 normal = normalize(input.normal);
    if (!isFrontFace)
    {
        normal = normalize(float3(-normal.x, normal.y, -normal.z));
    }
    const Surface surface = {
        input.worldPosition, input.clipPosition, normal, float3(0.0, 1.0, 0.0), input.color, 0.0,
        BladeRoughness,
    };
    return float4(shadeSurface(surface), 1.0);
}
