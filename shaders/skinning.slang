// Le skinning en compute (ADR-0022) : chaque sommet est déplacé par la moyenne pondérée des
// matrices de ses quatre os, et écrit au format de la passe des meshes, qui le dessine ensuite sans
// rien savoir de l'animation.
//
// Les sommets se lisent et s'écrivent mot de 32 bits par mot (StructuredBuffer<uint>, indexé par
// adresse / 4) : leur disposition est exactement celle de SkinnedVertex et de MeshVertex en C++, sans
// les alignements qu'imposerait un StructuredBuffer d'un float3 sous Vulkan. Pas de
// ByteAddressBuffer : la cible WGSL de Slang ne sait pas le lire, et le shader doit aussi tourner dans
// le navigateur (ADR-0023).

static const uint SourceStride = 84; // SkinnedVertex : position, normale, tangente, couleur, uv, os, poids
static const uint SkinnedStride = 60; // MeshVertex : position, normale, tangente, couleur, uv

StructuredBuffer<uint> sourceVertices : register(t0);
StructuredBuffer<float4x4> jointMatrices : register(t1);
RWStructuredBuffer<uint> skinnedVertices : register(u0);

/// `count` mots consécutifs à partir de l'octet `address`, qui est un multiple de 4.
uint4 loadWords(uint address, uint count)
{
    const uint word = address / 4;
    return uint4(sourceVertices[word], count > 1 ? sourceVertices[word + 1] : 0,
                 count > 2 ? sourceVertices[word + 2] : 0, count > 3 ? sourceVertices[word + 3] : 0);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 thread : SV_DispatchThreadID)
{
    // Le dernier groupe déborde : sa taille ne divise pas le nombre de sommets.
    uint skinnedWords;
    uint wordStride;
    skinnedVertices.GetDimensions(skinnedWords, wordStride);
    if (thread.x >= skinnedWords * 4 / SkinnedStride)
    {
        return;
    }

    const uint source = thread.x * SourceStride;
    const float3 position = asfloat(loadWords(source, 3).xyz);
    const float3 normal = asfloat(loadWords(source + 12, 3).xyz);
    const float4 tangent = asfloat(loadWords(source + 24, 4));
    const uint3 colorBits = loadWords(source + 40, 3).xyz;
    const uint2 uvBits = loadWords(source + 52, 2).xy;
    // Quatre indices de 16 bits, rangés deux par mot.
    const uint2 packedJoints = loadWords(source + 60, 2).xy;
    const uint4 joints = uint4(packedJoints.x & 0xFFFF, packedJoints.x >> 16,
                               packedJoints.y & 0xFFFF, packedJoints.y >> 16);
    const float4 weights = asfloat(loadWords(source + 68, 4));

    const float4x4 skin = weights.x * jointMatrices[joints.x] + weights.y * jointMatrices[joints.y] +
                          weights.z * jointMatrices[joints.z] + weights.w * jointMatrices[joints.w];
    const float3 skinned = mul(skin, float4(position, 1.0)).xyz;
    // ponytail: la normale et la tangente tournent avec la matrice des os, sans la transposée de
    // son inverse : juste tant que les os ne s'étirent pas d'un seul côté (glTF le permet).
    const float3 skinnedNormal = normalize(mul((float3x3)skin, normal));
    const float3 skinnedTangent = normalize(mul((float3x3)skin, tangent.xyz));

    uint words[15];
    const uint3 positionBits = asuint(skinned);
    const uint3 normalBits = asuint(skinnedNormal);
    const uint3 tangentBits = asuint(skinnedTangent);
    words[0] = positionBits.x;
    words[1] = positionBits.y;
    words[2] = positionBits.z;
    words[3] = normalBits.x;
    words[4] = normalBits.y;
    words[5] = normalBits.z;
    words[6] = tangentBits.x;
    words[7] = tangentBits.y;
    words[8] = tangentBits.z;
    words[9] = asuint(tangent.w); // le sens de v ne change pas avec la pose
    words[10] = colorBits.x;
    words[11] = colorBits.y;
    words[12] = colorBits.z;
    words[13] = uvBits.x;
    words[14] = uvBits.y;
    const uint target = thread.x * SkinnedStride / 4;
    for (uint word = 0; word < 15; ++word)
    {
        skinnedVertices[target + word] = words[word];
    }
}
