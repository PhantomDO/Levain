// La passe de tonemapping (M5.2) : un triangle qui couvre l'image, et chaque pixel lit la lumière de
// la scène dans l'image HDR, l'expose, et la ramène dans ce que l'écran sait afficher.

/// Du bourrage en scalaires, pas en `float3` : en WGSL, un `float3` s'aligne sur 16 octets, et la
/// structure en ferait 32 au lieu des 16 du C++ (build/GOTCHA.md).
struct TonemapConstants
{
    float exposure;
    uint tonemapper; ///< Tonemapper (tonemap.hpp) : 0 coupe, 1 ACES, 2 AgX, 3 Khronos PBR Neutral.
    float padding1;
    float padding2;
};

ConstantBuffer<TonemapConstants> settings : register(b0);
Texture2D hdrImage : register(t0);
SamplerState hdrSampler : register(s0);

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD;
};

/// Un seul triangle, deux fois plus grand que l'écran : il le couvre entièrement, sans la diagonale
/// de deux triangles (que le GPU dessinerait deux fois).
[shader("vertex")]
VertexOutput vertexMain(uint vertexId: SV_VertexID)
{
    VertexOutput output;
    output.uv = float2((vertexId << 1) & 2, vertexId & 2);
    output.position = float4(output.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

/// ACES, par l'approximation de Stephen Hill (BakingLab, licence MIT) : les transformations RRT et
/// ODT d'ACES ajustées par une fraction rationnelle, entre deux changements d'espace de couleur. Le
/// résultat est linéaire, prêt pour une cible sRGB.
float3 aces(float3 color)
{
    const float3x3 input = float3x3(0.59719, 0.35458, 0.04823, 0.07600, 0.90834, 0.01566, 0.02840,
                                    0.13383, 0.83777);
    const float3x3 output = float3x3(1.60475, -0.53108, -0.07367, -0.10208, 1.10813, -0.00605,
                                     -0.00327, -0.07276, 1.07602);
    const float3 v = mul(input, color);
    const float3 fitted = (v * (v + 0.0245786) - 0.000090537) /
                          (v * (0.983729 * v + 0.4329510) + 0.238081);
    return saturate(mul(output, fitted));
}

/// AgX (Troy Sobotka), dans sa version courante à polynôme (Benjamin Wrensch) : la lumière passe en
/// log2, se cale entre −12,5 et +4 diaphragmes, puis une courbe en S la ramène dans [0, 1]. Le
/// changement d'espace d'entrée désature les couleurs très vives, qui virent au blanc plutôt que de
/// saturer d'un seul canal.
float3 agx(float3 color)
{
    // Les matrices sont écrites colonne par colonne dans la source GLSL : mul(v, M) ici.
    const float3x3 inset = float3x3(0.842479062253094, 0.0423282422610123, 0.0423756549057051,
                                    0.0784335999999992, 0.878468636469772, 0.0784336,
                                    0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const float3x3 outset = float3x3(1.19687900512017, -0.0528968517574562, -0.0529716355144438,
                                     -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
                                     -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
    const float minEv = -12.47393;
    const float maxEv = 4.026069;
    float3 v = mul(max(color, 1e-10), inset);
    v = (clamp(log2(v), minEv, maxEv) - minEv) / (maxEv - minEv);
    const float3 v2 = v * v;
    const float3 v4 = v2 * v2;
    v = 15.5 * v4 * v2 - 40.14 * v4 * v + 31.96 * v4 - 6.868 * v2 * v + 0.4298 * v2 + 0.1191 * v -
        0.00232;
    // La courbe rend une image pour l'écran (gamma 2,2) : de retour en linéaire pour la cible sRGB.
    return pow(saturate(mul(v, outset)), 2.2);
}

/// Khronos PBR Neutral, d'après le code de référence de KhronosGroup/ToneMapping (Apache-2.0) : la
/// lumière jusqu'à 0,76 passe telle quelle, teintes comprises, puis le pic se comprime vers 1 et se
/// désature un peu. La courbe par défaut du glTF Sample Viewer, celle des comparaisons avec lui
/// (#125, #131) : un albédo s'y lit à sa vraie couleur.
float3 khronosPbrNeutral(float3 color)
{
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;
    const float x = min(color.r, min(color.g, color.b));
    const float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;
    const float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression)
    {
        return color;
    }
    const float d = 1.0 - startCompression;
    const float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;
    const float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return lerp(color, float3(newPeak), g);
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    const float3 light = hdrImage.Sample(hdrSampler, input.uv).rgb * settings.exposure;
    switch (settings.tonemapper)
    {
    case 1:
        return float4(aces(light), 1.0);
    case 2:
        return float4(agx(light), 1.0);
    case 3:
        return float4(khronosPbrNeutral(light), 1.0);
    default:
        // La lumière au-delà de 1 est coupée net : ce que faisait le moteur avant M5.2.
        return float4(saturate(light), 1.0);
    }
}
