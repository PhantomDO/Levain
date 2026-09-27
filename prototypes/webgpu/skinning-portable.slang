// Le skinning en compute (ADR-0022) : chaque sommet est déplacé par la moyenne pondérée des
// matrices de ses quatre os, et écrit au format de la passe des meshes, qui le dessine ensuite sans
// rien savoir de l'animation. Les buffers sont lus et écrits octet par octet (ByteAddressBuffer) :
// leur disposition est exactement celle de SkinnedVertex et de MeshVertex en C++, sans les
// alignements qu'imposerait un StructuredBuffer d'un float3 sous Vulkan.

static const uint SourceStride = 56; // SkinnedVertex : position, couleur, uv, os, poids
static const uint SkinnedStride = 32; // MeshVertex : position, couleur, uv

StructuredBuffer<uint> sourceVertices : register(t0);
StructuredBuffer<float4x4> jointMatrices : register(t1);
RWStructuredBuffer<uint> skinnedVertices : register(u0);


// Variante portable (prototype) : des tampons de mots de 32 bits, indexés par adresse / 4, au lieu
// de ByteAddressBuffer, que la cible WGSL de Slang ne sait pas lire.
uint3 load3(StructuredBuffer<uint> b, uint a) { return uint3(b[a / 4], b[a / 4 + 1], b[a / 4 + 2]); }
uint2 load2(StructuredBuffer<uint> b, uint a) { return uint2(b[a / 4], b[a / 4 + 1]); }
uint4 load4(StructuredBuffer<uint> b, uint a) { return uint4(b[a / 4], b[a / 4 + 1], b[a / 4 + 2], b[a / 4 + 3]); }
void store3(RWStructuredBuffer<uint> b, uint a, uint3 v) { b[a / 4] = v.x; b[a / 4 + 1] = v.y; b[a / 4 + 2] = v.z; }
void store2(RWStructuredBuffer<uint> b, uint a, uint2 v) { b[a / 4] = v.x; b[a / 4 + 1] = v.y; }

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 thread : SV_DispatchThreadID)
{
    // Le dernier groupe déborde : sa taille ne divise pas le nombre de sommets.
    uint skinnedWords;
    uint stride;
    skinnedVertices.GetDimensions(skinnedWords, stride);
    const uint skinnedBytes = skinnedWords * 4;
    if (thread.x >= skinnedBytes / SkinnedStride)
    {
        return;
    }

    const uint source = thread.x * SourceStride;
    const float3 position = asfloat(load3(sourceVertices, source));
    const uint3 colorBits = load3(sourceVertices, source + 12);
    const uint2 uvBits = load2(sourceVertices, source + 24);
    // Quatre indices de 16 bits, rangés deux par mot.
    const uint2 packedJoints = load2(sourceVertices, source + 32);
    const uint4 joints = uint4(packedJoints.x & 0xFFFF, packedJoints.x >> 16,
                               packedJoints.y & 0xFFFF, packedJoints.y >> 16);
    const float4 weights = asfloat(load4(sourceVertices, source + 40));

    const float4x4 skin = weights.x * jointMatrices[joints.x] + weights.y * jointMatrices[joints.y] +
                          weights.z * jointMatrices[joints.z] + weights.w * jointMatrices[joints.w];
    const float3 skinned = mul(skin, float4(position, 1.0)).xyz;

    const uint target = thread.x * SkinnedStride;
    store3(skinnedVertices, target, asuint(skinned));
    store3(skinnedVertices, target + 12, colorBits);
    store2(skinnedVertices, target + 24, uvBits);
}
