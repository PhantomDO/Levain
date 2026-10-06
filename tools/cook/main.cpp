// levain_cook : cuit les assets d'une ou plusieurs racines (ADR-0020). Sans GPU : il tourne sur une
// machine de build, comme le calcul des mips (M2.2).
//
//   levain_cook data assets-cache
//
// Chaque glTF devient <racine>/.cooked/<guid>.lvmesh ; chaque image, et chaque image embarquée
// dans un glTF, un maître UASTC (<guid>.ktx2) et le cache BC7 du PC (<guid>.bc7.ktx2). Une image
// qu'un matériau lit comme des données (normal map, rugosité-métal) est cuite en <guid>.linear.*,
// ses mips moyennées sur les octets. Un fichier déjà à jour (même hash de source, même version du
// cuiseur) n'est pas refait.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "levain/assets/asset_ref.hpp"
#include "levain/assets/collision.hpp"
#include "levain/assets/cooked.hpp"
#include "levain/assets/cooked_texture.hpp"
#include "levain/assets/gltf.hpp"
#include "levain/assets/image.hpp"
#include "levain/assets/registry.hpp"
#include "levain/core/log.hpp"

namespace
{

namespace fs = std::filesystem;
using levain::core::log;
using levain::core::LogLevel;

bool isModel(const fs::path& path)
{
    const fs::path extension = path.extension();
    return extension == ".gltf" || extension == ".glb";
}

fs::path withSuffix(fs::path stem, std::string_view suffix)
{
    stem += suffix;
    return stem;
}

using TextureUses = std::vector<std::pair<levain::assets::AssetRef, levain::assets::ImageEncoding>>;

/// Le cache BC7 de `stem` est-il à jour pour une source de hash `hash` ?
bool isTextureCooked(const fs::path& stem, std::uint64_t hash)
{
    return levain::assets::readCookedTexture(withSuffix(stem, ".bc7.ktx2"), hash,
                                             levain::assets::TextureFormat::Bc7Srgb)
        .has_value();
}

/// Cuit une texture (ADR-0020, et son amendement du 24/09) : le maître UASTC, puis le cache BC7 du
/// PC transcodé depuis lui. Ses mips se moyennent selon `encoding` : une couleur en lumière
/// linéaire, une donnée (une normal map) sur ses octets. Rien n'est refait si le cache est à jour.
/// Rend faux en cas d'échec.
bool cookTexture(const fs::path& stem, const levain::assets::Image& image, std::uint64_t hash,
                 levain::assets::ImageEncoding encoding)
{
    if (isTextureCooked(stem, hash))
    {
        return true;
    }
    const fs::path master = withSuffix(stem, ".ktx2");
    const fs::path platform = withSuffix(stem, ".bc7.ktx2");
    const auto start = std::chrono::steady_clock::now();
    // ponytail: le KTX2 se dit sRGB même pour des données ; personne ne lit cette étiquette, c'est
    // l'usage (le matériau) qui choisit le format du GPU à l'envoi.
    auto written = levain::assets::writeCookedTexture(
        master, levain::assets::buildMipChain(image, encoding), hash);
    if (written)
    {
        written = levain::assets::writePlatformTexture(master, platform,
                                                       levain::assets::TextureFormat::Bc7Srgb);
    }
    if (!written)
    {
        log("cook", LogLevel::Error, "{}", written.error().message);
        return false;
    }
    log("cook", LogLevel::Info, "texture cuite en {:.0f} ms : {}",
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(),
        master.filename().string());
    return true;
}

/// Les encodages sous lesquels cuire l'image `texture` : ceux de ses usages dans les matériaux, ou
/// la couleur si aucun matériau ne la désigne.
std::vector<levain::assets::ImageEncoding> encodingsOf(const TextureUses& uses,
                                                       levain::assets::AssetRef texture)
{
    std::vector<levain::assets::ImageEncoding> encodings;
    for (const auto& [ref, encoding] : uses)
    {
        if (ref == texture && std::ranges::find(encodings, encoding) == encodings.end())
        {
            encodings.push_back(encoding);
        }
    }
    if (encodings.empty())
    {
        encodings.push_back(levain::assets::ImageEncoding::Srgb);
    }
    return encodings;
}

/// Cuit une image dans chacun des encodages où elle sert. Rend faux en cas d'échec.
bool cookImage(const levain::assets::AssetRegistry& registry, levain::assets::AssetRef texture,
               const levain::assets::Image& image, std::uint64_t hash, const TextureUses& uses)
{
    for (const levain::assets::ImageEncoding encoding : encodingsOf(uses, texture))
    {
        const auto stem = levain::assets::cookedTextureStem(registry, texture, encoding);
        if (!stem || !cookTexture(*stem, image, hash, encoding))
        {
            return false;
        }
    }
    return true;
}

/// Cuit la collision d'un modèle (`.lvcol`, ADR-0028) si elle n'est pas à jour : sa simplification
/// coûte 60 ms pour Sponza, que le chargement ne paie plus. Rend faux en cas d'échec d'écriture.
bool cookCollision(const levain::assets::AssetRegistry& registry, levain::assets::AssetId id,
                   const levain::assets::AssetEntry& entry, const levain::assets::Model& model)
{
    const std::optional<fs::path> cooked = levain::assets::cookedPathOf(registry, id, ".lvcol");
    if (!cooked)
    {
        log("cook", LogLevel::Error, "{} : hors des racines d'assets, pas de .lvcol",
            entry.file.string());
        return false;
    }
    if (levain::assets::readCookedCollision(*cooked, entry.hash,
                                            levain::assets::DefaultCollisionError))
    {
        return true;
    }
    const auto start = std::chrono::steady_clock::now();
    const levain::assets::CollisionMesh mesh = levain::assets::collisionMeshOf(model);
    if (auto written = levain::assets::writeCookedCollision(*cooked, mesh, entry.hash,
                                                            levain::assets::DefaultCollisionError);
        !written)
    {
        log("cook", LogLevel::Error, "{}", written.error().message);
        return false;
    }
    // « cuite en » : le garde-fou de la CI (cache restauré, rien ne doit être recuit) le cherche.
    log("cook", LogLevel::Info, "collision cuite en {:.0f} ms ({} triangles) : {}",
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(),
        mesh.indices.size() / 3, cooked->string());
    return true;
}

/// Cuit un modèle et ses images embarquées, s'ils ne sont pas déjà à jour, et ajoute à `uses` les
/// usages des textures de ses matériaux. Rend faux en cas d'échec.
bool cookModel(const levain::assets::AssetRegistry& registry, levain::assets::AssetId id,
               const levain::assets::AssetEntry& entry, TextureUses& uses)
{
    const fs::path cooked =
        levain::assets::cookedPathOf(registry, id, ".lvmesh").value_or(fs::path{});
    if (auto cached = levain::assets::readCookedModel(cooked, entry.hash))
    {
        const TextureUses own = levain::assets::textureUsesOf(*cached);
        uses.insert(uses.end(), own.begin(), own.end());
        // Ses images embarquées aussi, dans chacun de leurs usages : une normal map cuite avant
        // qu'on distingue les données n'a pas encore sa version `.linear`.
        const bool embeddedCooked = std::ranges::all_of(
            own,
            [&](const auto& use)
            {
                const auto stem =
                    levain::assets::cookedTextureStem(registry, use.first, use.second);
                return use.first.asset != id || (stem && isTextureCooked(*stem, entry.hash));
            });
        if (embeddedCooked)
        {
            // Le modèle est à jour : seule sa collision peut manquer. Son échec ne recuit pas le
            // modèle, qui serait réécrit pour échouer au même endroit.
            if (!cookCollision(registry, id, entry, *cached))
            {
                return false;
            }
            log("cook", LogLevel::Info, "à jour : {}", entry.file.string());
            return true;
        }
    }
    const auto start = std::chrono::steady_clock::now();
    auto model = levain::assets::loadGltf(entry.file, id, registry);
    if (!model)
    {
        log("cook", LogLevel::Error, "{}", model.error().message);
        return false;
    }
    const TextureUses own = levain::assets::textureUsesOf(*model);
    uses.insert(uses.end(), own.begin(), own.end());
    // Ses images embarquées sont des textures comme les autres, désignées par {GUID, indice}.
    for (const auto& [index, image] : model->embeddedImages)
    {
        if (!cookImage(registry, {.asset = id, .sub = index}, image, entry.hash, own))
        {
            return false;
        }
    }
    if (auto written = levain::assets::writeCookedModel(cooked, *model, entry.hash); !written)
    {
        log("cook", LogLevel::Error, "{}", written.error().message);
        return false;
    }
    log("cook", LogLevel::Info, "cuit en {:.0f} ms : {} → {}",
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(),
        entry.file.string(), cooked.string());
    return cookCollision(registry, id, entry, *model);
}

} // namespace

int main(int argc, char** argv)
{
    try
    {
        const std::span arguments{argv, static_cast<std::size_t>(argc)};
        if (arguments.size() < 2)
        {
            std::fputs("usage : levain_cook <racine d'assets>...\n", stderr);
            return 2;
        }

        // Toutes les racines d'abord : un glTF peut désigner une image d'une autre racine.
        levain::assets::AssetRegistry registry;
        for (const char* root : arguments.subspan(1))
        {
            if (auto report = levain::assets::scanAssets(root, registry); !report)
            {
                log("cook", LogLevel::Error, "{}", report.error().message);
                return 1;
            }
        }

        // Les modèles d'abord : leurs matériaux disent sous quel encodage cuire chaque image,
        // couleur ou données (ADR-0020, amendement du 03/10).
        int failures = 0;
        int models = 0;
        int textures = 0;
        TextureUses uses;
        for (const auto& [id, entry] : registry.entries)
        {
            if (isModel(entry.file))
            {
                ++models;
                failures += cookModel(registry, id, entry, uses) ? 0 : 1;
            }
        }
        for (const auto& [id, entry] : registry.entries)
        {
            if (isModel(entry.file))
            {
                continue;
            }
            ++textures;
            auto image = levain::assets::loadImage(entry.file);
            if (!image)
            {
                log("cook", LogLevel::Error, "{}", image.error().message);
            }
            failures +=
                image && cookImage(registry, {.asset = id, .sub = 0}, *image, entry.hash, uses) ? 0
                                                                                                : 1;
        }
        // Rien à cuire est suspect (règle n°7) : une racine mal orthographiée ne doit pas passer
        // pour un succès.
        if (models + textures == 0)
        {
            log("cook", LogLevel::Error, "aucun asset dans les racines données");
            return 1;
        }
        log("cook", LogLevel::Info, "{} modèles, {} images, {} échecs", models, textures, failures);
        return failures == 0 ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        std::fputs(e.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
}
