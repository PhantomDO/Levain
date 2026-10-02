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
