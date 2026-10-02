// L'environnement de l'éclairage par l'image (IBL, M5.4) : l'HDRI équirectangulaire (une photo à
// 360°, la longitude en largeur, la latitude en hauteur) devient une cubemap, puis chaque niveau de
// mip de la cubemap est la moyenne 2 × 2 du précédent. Les passes de convolution liront ces mips
// pour ne pas suréchantillonner les zones très lumineuses, comme le soleil.
//
// Les mêmes formules, côté CPU, servent de référence aux tests (engine/render/src/environment.cpp).

static const float Pi = 3.14159265358979;

/// Le format « rgba16f » de Slang : en WGSL, une storage texture en écriture seule
/// (`texture_storage_2d_array<rgba16float, write>`). RWTexture2DArray donnerait read_write, que
/// WebGPU refuse pour ce format.
[format("rgba16f")] WTexture2DArray<float4> faces : register(u0);
SamplerState linearSampler : register(s0);

/// La direction qui passe par le point `uv` (de −1 à 1) de la face `face`, dans l'ordre et
/// l'orientation des cubemaps de Vulkan, Direct3D et WebGPU : +X, −X, +Y, −Y, +Z, −Z.
float3 cubeDirectionOf(uint face, float2 uv)
{
    switch (face)
    {
    case 0:
        return normalize(float3(1.0, -uv.y, -uv.x));
    case 1:
        return normalize(float3(-1.0, -uv.y, uv.x));
    case 2:
        return normalize(float3(uv.x, 1.0, uv.y));
    case 3:
        return normalize(float3(uv.x, -1.0, -uv.y));
    case 4:
        return normalize(float3(uv.x, -uv.y, 1.0));
    default:
        return normalize(float3(-uv.x, -uv.y, -1.0));
    }
}

/// Le point de l'image équirectangulaire que voit `direction` : la longitude autour de +Y en
/// largeur, le zénith en haut.
float2 equirectUvOf(float3 direction)
{
    return float2(atan2(direction.z, direction.x) / (2.0 * Pi) + 0.5,
                  acos(clamp(direction.y, -1.0, 1.0)) / Pi);
}

/// La direction du centre du texel `texel` de la face `face`, sur une face de `size` texels.
float3 texelDirectionOf(uint3 texel, uint size)
{
    const float2 uv = (float2(texel.xy) + 0.5) / float(size) * 2.0 - 1.0;
    return cubeDirectionOf(texel.z, uv);
}

uint faceSize()
{
    uint width, height, layers;
    faces.GetDimensions(width, height, layers);
    return width;
}

Texture2D equirect : register(t0);

/// Un thread par texel, la face en z.
[shader("compute")][numthreads(8, 8, 1)] void cubeFromEquirect(uint3 texel : SV_DispatchThreadID)
{
    const uint size = faceSize();
    if (texel.x >= size || texel.y >= size)
    {
        return;
    }
    const float2 uv = equirectUvOf(texelDirectionOf(texel, size));
    faces.Store(texel, float4(equirect.SampleLevel(linearSampler, uv, 0.0).rgb, 1.0));
}

/// Le niveau de mip au-dessus de celui à écrire, seul dans sa vue. En t1 : Slang refuse deux
/// paramètres au même registre dans un fichier, même lus par deux points d'entrée différents.
TextureCube finerMip : register(t1);

/// Le centre d'un texel du niveau écrit tombe au coin commun de quatre texels du niveau au-dessus :
/// le filtrage bilinéaire en fait la moyenne, d'une seule lecture.
[shader("compute")][numthreads(8, 8, 1)] void downsample(uint3 texel : SV_DispatchThreadID)
{
    const uint size = faceSize();
    if (texel.x >= size || texel.y >= size)
    {
        return;
    }
    const float3 direction = texelDirectionOf(texel, size);
    faces.Store(texel, float4(finerMip.SampleLevel(linearSampler, direction, 0.0).rgb, 1.0));
}

// Les convolutions de l'IBL, d'après Karis, « Real Shading in Unreal Engine 4 » (SIGGRAPH 2013),
// et l'échantillonnage filtré de Colbert et Křivánek (GPU Gems 3, chapitre 20) : chaque échantillon
// lit l'environnement au niveau de mip dont un texel couvre l'angle solide de l'échantillon. Peu
// d'échantillons suffisent alors, sans les points brillants d'un soleil tiré au hasard.

/// Doivent valoir les constantes de même nom (environment.hpp).
static const uint SpecularSize = 128;
static const uint SpecularMips = 6;

static const uint IrradianceSamples = 256;
static const uint SpecularSamples = 256;
static const uint BrdfSamples = 512;

/// L'environnement entier, tous ses mips.
TextureCube environment : register(t2);

/// Le i-ième des `count` points de Hammersley : une suite répartie sur le carré unité plus
/// régulièrement que le hasard, et la même à chaque exécution.
float2 hammersleyOf(uint i, uint count)
{
    return float2(float(i) / float(count), float(reversebits(i)) * 2.3283064365386963e-10);
}

/// Un repère orthonormé autour de `normal` : ses deux tangentes, en colonnes avec elle.
float3x3 basisOf(float3 normal)
{
    const float3 up = abs(normal.y) < 0.999 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    const float3 tangent = normalize(cross(up, normal));
    const float3 bitangent = cross(normal, tangent);
    return float3x3(tangent.x, bitangent.x, normal.x, tangent.y, bitangent.y, normal.y, tangent.z,
                    bitangent.z, normal.z);
}

/// Le niveau de mip de l'environnement dont un texel couvre l'angle solide d'un échantillon de
/// densité `pdf` parmi `count`.
float filteredLodOf(float pdf, uint count)
{
    uint width, height, levels;
    environment.GetDimensions(0, width, height, levels);
    const float sampleSolidAngle = 1.0 / (float(count) * pdf);
    const float texelSolidAngle = 4.0 * Pi / (6.0 * float(width) * float(width));
    return clamp(0.5 * log2(sampleSolidAngle / texelSolidAngle) + 1.0, 0.0, float(levels - 1));
}

/// L'irradiance divisée par π : ce que la surface reçoit du ciel, pondéré par le cosinus. Un
/// matériau mat de couleur `albedo` renvoie `albedo × irradiance`.
[shader("compute")]
[numthreads(8, 8, 1)]
void irradiance(uint3 texel: SV_DispatchThreadID)
{
    const uint size = faceSize();
    if (texel.x >= size || texel.y >= size)
    {
        return;
    }
    const float3x3 basis = basisOf(texelDirectionOf(texel, size));
    float3 sum = float3(0.0);
    for (uint i = 0; i < IrradianceSamples; ++i)
    {
        // Des directions tirées selon le cosinus : la moyenne de ce qu'elles voient est le
        // résultat, sans autre poids.
        const float2 point = hammersleyOf(i, IrradianceSamples);
        const float cosTheta = sqrt(1.0 - point.y);
        const float sinTheta = sqrt(point.y);
        const float phi = 2.0 * Pi * point.x;
        const float3 local = float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
        const float lod = filteredLodOf(cosTheta / Pi, IrradianceSamples);
        sum += environment.SampleLevel(linearSampler, mul(basis, local), lod).rgb;
    }
    faces.Store(texel, float4(sum / float(IrradianceSamples), 1.0));
}

/// Une demi-direction tirée selon la distribution GGX de rugosité `alpha` (le carré de la
/// rugosité du matériau), autour de +Z.
float3 ggxHalfVectorOf(float2 point, float alpha)
{
    const float cosTheta = sqrt((1.0 - point.y) / (1.0 + (alpha * alpha - 1.0) * point.y));
    const float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    const float phi = 2.0 * Pi * point.x;
    return float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
}

float ggxDistributionOf(float cosTheta, float alpha)
{
    const float alpha2 = alpha * alpha;
    const float denominator = cosTheta * cosTheta * (alpha2 - 1.0) + 1.0;
    return alpha2 / (Pi * denominator * denominator);
}

/// La rugosité que représente un niveau de mip de la cubemap spéculaire : 0 au premier, 1 au
/// dernier.
float roughnessOfMip(uint size)
{
    return log2(float(SpecularSize) / float(size)) / float(SpecularMips - 1);
}

/// La cubemap spéculaire préfiltrée, un niveau de mip par rugosité : le reflet du ciel flouté par
/// le lobe GGX, en supposant qu'on regarde la surface de face (la « split sum » de Karis).
[shader("compute")]
[numthreads(8, 8, 1)]
void prefilterSpecular(uint3 texel: SV_DispatchThreadID)
{
    const uint size = faceSize();
    if (texel.x >= size || texel.y >= size)
    {
        return;
    }
    const float3 normal = texelDirectionOf(texel, size);
    const float roughness = roughnessOfMip(size);
    const float alpha = max(roughness * roughness, 1e-4);
    uint width, height, levels;
    environment.GetDimensions(0, width, height, levels);
    // Au moins le niveau de l'environnement qui a la taille de ce mip : en dessous, un texel
    // spéculaire en couvrirait plusieurs sans les moyenner.
    const float baseLod = max(log2(float(width) / float(size)), 0.0);
    const float3x3 basis = basisOf(normal);
    float3 sum = float3(0.0);
    float weight = 0.0;
    for (uint i = 0; i < SpecularSamples; ++i)
    {
        const float3 halfVector = ggxHalfVectorOf(hammersleyOf(i, SpecularSamples), alpha);
        // La vue est la normale : la lumière est son reflet autour de la demi-direction.
        const float3 local = float3(0.0, 0.0, 1.0);
        const float3 light = 2.0 * halfVector.z * halfVector - local;
        if (light.z <= 0.0)
        {
            continue;
        }
        // La densité de `light` : D(h) cos θh / (4 v·h), et v·h = cos θh quand la vue est la
        // normale.
        const float pdf = ggxDistributionOf(halfVector.z, alpha) / 4.0;
        const float lod = max(filteredLodOf(pdf, SpecularSamples), baseLod);
        sum += environment.SampleLevel(linearSampler, mul(basis, light), lod).rgb * light.z;
        weight += light.z;
    }
    faces.Store(texel, float4(sum / max(weight, 1e-6), 1.0));
}

/// Le terme de Smith pour l'IBL : k = α / 2 (Karis), et non (r + 1)² / 8 comme pour une lumière
/// ponctuelle.
float smithGeometryOf(float cosView, float cosLight, float alpha)
{
    const float k = alpha / 2.0;
    return cosView / (cosView * (1.0 - k) + k) * cosLight / (cosLight * (1.0 - k) + k);
}

[format("rgba16f")]
WTexture2D<float4> brdfLut : register(u1);

/// La seconde moitié de la split sum : pour un cosinus de vue (en x) et une rugosité (en y), la
/// part de la lumière que la BRDF renvoie, en échelle (rouge) et biais (vert) de la couleur
/// spéculaire F0. Ne dépend pas du ciel : calculée une fois.
[shader("compute")]
[numthreads(8, 8, 1)]
void integrateBrdf(uint2 texel: SV_DispatchThreadID)
{
    uint size, height;
    brdfLut.GetDimensions(size, height);
    if (texel.x >= size || texel.y >= size)
    {
        return;
    }
    const float cosView = (float(texel.x) + 0.5) / float(size);
    const float roughness = (float(texel.y) + 0.5) / float(size);
    const float alpha = roughness * roughness;
    const float3 view = float3(sqrt(1.0 - cosView * cosView), 0.0, cosView);
    float2 sum = float2(0.0);
    for (uint i = 0; i < BrdfSamples; ++i)
    {
        const float3 halfVector = ggxHalfVectorOf(hammersleyOf(i, BrdfSamples), alpha);
        const float3 light = 2.0 * dot(view, halfVector) * halfVector - view;
        const float cosLight = saturate(light.z);
        const float cosHalf = saturate(halfVector.z);
        const float viewHalf = saturate(dot(view, halfVector));
        if (cosLight <= 0.0)
        {
            continue;
        }
        // La BRDF divisée par la densité de l'échantillon : G v·h / (n·h n·v).
        const float visibility =
            smithGeometryOf(cosView, cosLight, alpha) * viewHalf / (cosHalf * cosView);
        const float fresnel = pow(1.0 - viewHalf, 5.0);
        sum += float2((1.0 - fresnel) * visibility, fresnel * visibility);
    }
    brdfLut.Store(texel, float4(sum / float(BrdfSamples), 0.0, 1.0));
}
