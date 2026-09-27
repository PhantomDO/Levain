// Le skinning en compute (ADR-0022) : chaque sommet est déplacé par la moyenne pondérée des
// matrices de ses quatre os, et écrit au format de la passe des meshes, qui le dessine ensuite sans
// rien savoir de l'animation.
//
// Les sommets se lisent et s'écrivent mot de 32 bits par mot (StructuredBuffer<uint>, indexé par
// adresse / 4) : leur disposition est exactement celle de SkinnedVertex et de MeshVertex en C++, sans
// les alignements qu'imposerait un StructuredBuffer d'un float3 sous Vulkan. Pas de
// ByteAddressBuffer : la cible WGSL de Slang ne sait pas le lire, et le shader doit aussi tourner dans
// le navigateur (ADR-0023).

static const uint SourceStride = 56; // SkinnedVertex : position, couleur, uv, os, poids
static const uint SkinnedStride = 32; // MeshVertex : position, couleur, uv

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
    const uint3 colorBits = loadWords(source + 12, 3).xyz;
    const uint2 uvBits = loadWords(source + 24, 2).xy;
    // Quatre indices de 16 bits, rangés deux par mot.
    const uint2 packedJoints = loadWords(source + 32, 2).xy;
    const uint4 joints = uint4(packedJoints.x & 0xFFFF, packedJoints.x >> 16,
                               packedJoints.y & 0xFFFF, packedJoints.y >> 16);
    const float4 weights = asfloat(loadWords(source + 40, 4));

    const float4x4 skin = weights.x * jointMatrices[joints.x] + weights.y * jointMatrices[joints.y] +
                          weights.z * jointMatrices[joints.z] + weights.w * jointMatrices[joints.w];
    const float3 skinned = mul(skin, float4(position, 1.0)).xyz;

    const uint target = thread.x * SkinnedStride / 4;
    const uint3 skinnedBits = asuint(skinned);
    skinnedVertices[target] = skinnedBits.x;
    skinnedVertices[target + 1] = skinnedBits.y;
    skinnedVertices[target + 2] = skinnedBits.z;
    skinnedVertices[target + 3] = colorBits.x;
    skinnedVertices[target + 4] = colorBits.y;
    skinnedVertices[target + 5] = colorBits.z;
    skinnedVertices[target + 6] = uvBits.x;
    skinnedVertices[target + 7] = uvBits.y;
}
