// Le tri des lumières ponctuelles en clusters (ADR-0024) : un thread par cluster calcule la boîte
// du cluster dans le repère de la caméra, puis garde les lumières dont la sphère la touche. Le même
// calcul, côté CPU, sert de référence aux tests (engine/render/src/light_clusters.cpp).

struct PointLight
{
    float3 position;
    float range;
    float3 color;
    float intensity;
};

struct ClusterConstants
{
    float4x4 view;
    float4x4 inverseProjection;
    uint3 grid;
    uint lightCount;
    float nearPlane;
    float farPlane;
    float2 padding;
};

/// Doit valoir MaxLightsPerCluster (light_clusters.hpp).
static const uint MaxLightsPerCluster = 32;

ConstantBuffer<ClusterConstants> clusters : register(b0);
StructuredBuffer<PointLight> lights : register(t0);
RWStructuredBuffer<uint> lightCounts : register(u0);
RWStructuredBuffer<uint> lightIndices : register(u1);

/// La profondeur où commence la tranche `slice`, répartie de façon logarithmique.
float sliceDepthOf(uint slice)
{
    return clusters.nearPlane *
           pow(clusters.farPlane / clusters.nearPlane, float(slice) / float(clusters.grid.z));
}

/// Le rayon qui passe par le point `ndc` de l'écran, mis à l'échelle pour que z vaille −1.
float3 rayOf(float2 ndc)
{
    const float4 point = mul(clusters.inverseProjection, float4(ndc, 0.5, 1.0));
    const float3 view = point.xyz / point.w;
    return view / -view.z;
}

bool sphereTouchesBox(float3 center, float radius, float3 boxMin, float3 boxMax)
{
    const float3 offset = center - clamp(center, boxMin, boxMax);
    return dot(offset, offset) <= radius * radius;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 thread: SV_DispatchThreadID)
{
    const uint3 grid = clusters.grid;
    const uint index = thread.x;
    if (index >= grid.x * grid.y * grid.z)
    {
        return;
    }
    const uint3 cell = uint3(index % grid.x, (index / grid.x) % grid.y, index / (grid.x * grid.y));

    const float2 ndcMin = float2(cell.xy) / float2(grid.xy) * 2.0 - 1.0;
    const float2 ndcMax = float2(cell.xy + 1) / float2(grid.xy) * 2.0 - 1.0;
    const float depthNear = sliceDepthOf(cell.z);
    const float depthFar = sliceDepthOf(cell.z + 1);
    float3 boxMin = float3(3.4e38);
    float3 boxMax = float3(-3.4e38);
    const float2 corners[4] = { ndcMin, float2(ndcMax.x, ndcMin.y), float2(ndcMin.x, ndcMax.y),
                                ndcMax };
    for (uint corner = 0; corner < 4; ++corner)
    {
        const float3 ray = rayOf(corners[corner]);
        boxMin = min(boxMin, min(ray * depthNear, ray * depthFar));
        boxMax = max(boxMax, max(ray * depthNear, ray * depthFar));
    }

    // Toutes les lumières sont comptées ; seules les MaxLightsPerCluster premières sont gardées.
    uint found = 0;
    for (uint light = 0; light < clusters.lightCount; ++light)
    {
        const float3 center = mul(clusters.view, float4(lights[light].position, 1.0)).xyz;
        if (sphereTouchesBox(center, lights[light].range, boxMin, boxMax))
        {
            if (found < MaxLightsPerCluster)
            {
                lightIndices[index * MaxLightsPerCluster + found] = light;
            }
            ++found;
        }
    }
    lightCounts[index] = found;
}
