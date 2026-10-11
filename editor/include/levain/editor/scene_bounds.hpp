#pragma once

// Les boîtes que la caméra de l'éditeur cadre (ADR-0036, décision 6) : celle d'une entité, que F
// cadre, et celle de toute la scène, qui règle les plans de découpe. Fonctions libres, leurs
// dépendances en paramètres (ADR-0011) ; la glu d'`Editor` les appelle en une ligne.

#include <map>

#include <flecs.h>

#include "levain/app/models.hpp"
#include "levain/assets/asset_ref.hpp"
#include "levain/editor/camera.hpp"
#include "levain/render/culling.hpp"
#include "levain/render/mesh.hpp"
#include "levain/scene/components.hpp"

namespace levain::editor
{

/// **Une entité qu'on sait cadrer** : vivante et placée (`WorldTransform`). Sans matrice monde, sa
/// boîte serait le cube à l'origine, ce qui n'est pas elle : F ne cadre rien plutôt que cela.
[[nodiscard]] inline bool canFrameEntity(const flecs::entity& entity)
{
    return entity.is_valid() && entity.has<scene::WorldTransform>();
}

/// La boîte d'une entité qu'on ne sait pas mesurer, et celle d'un point : un cube de côté 1 autour
/// de sa position. Un nom pour que le repli ne soit pas un chiffre au milieu d'un calcul.
inline constexpr float UnitBoxHalfSize = 0.5f;

/// **La boîte d'une entité, dans le monde.** Celle de son mesh (`MeshRef`) si le modèle est sur le
/// GPU, passée par sa matrice monde ; sinon un cube de côté 1 à sa position (`WorldTransform`), mis
/// à l'échelle de l'entité. Une entité sans matrice monde (pas encore composée) : le cube à
/// l'origine. `instance` est l'instance unique, à l'origine, que l'étape « modèles » dessine
/// (`App::modelInstance`).
[[nodiscard]] render::Box entityBoxOf(const flecs::entity& entity,
                                      const std::map<assets::AssetId, app::ModelGpu>& models,
                                      const render::Instances& instance);

/// **La boîte de la scène** : l'union de celle de chaque entité qui a une matrice monde, ce que les
/// plans de découpe doivent contenir (`clipPlanesFor`). Une scène vide : le cube de côté 1 à
/// l'origine, car une boîte vide n'a pas de plans.
[[nodiscard]] render::Box sceneBoxOf(const flecs::query<const scene::WorldTransform>& everything,
                                     const std::map<assets::AssetId, app::ModelGpu>& models,
                                     const render::Instances& instance);

/// **Le cache de la boîte de la scène.** `sceneBoxOf` parcourt toutes les entités (deux recherches
/// et une table par entité) : à chaque image, sur 10 000 cubes, il pesait dans le « ui : CPU » du
/// jalon M7.1. En Édition rien ne bouge la scène que l'inspecteur et le chargement des modèles ;
/// la boîte se refait donc quand l'un d'eux a parlé, quand le nombre d'entités ou de modèles a
/// changé, au retour en Édition, et tous les `SceneBoundsRefreshFrames` images par prudence.
struct SceneBoundsCache
{
    render::Box box{};
    bool valid = false;
    int frameAge = 0; ///< Les images depuis le dernier calcul.
    std::size_t entityCount = 0;
    std::size_t modelCount = 0;
};

inline constexpr int SceneBoundsRefreshFrames = 60;

/// Ce que l'image sait de la scène, sans la parcourir.
struct SceneBoundsFacts
{
    std::size_t entityCount = 0;
    std::size_t modelCount = 0;
    bool edited = false; ///< L'inspecteur a écrit, ou le mode vient de changer.
};

/// **La boîte est à refaire.** Jamais calculée, éditée, un compte qui change, ou trop ancienne.
[[nodiscard]] constexpr bool sceneBoundsStale(const SceneBoundsCache& cache,
                                              const SceneBoundsFacts& facts)
{
    return !cache.valid || facts.edited || cache.entityCount != facts.entityCount ||
           cache.modelCount != facts.modelCount || cache.frameAge >= SceneBoundsRefreshFrames;
}

/// **La boîte de la scène, du cache si elle est fraîche**, refaite par `sceneBoxOf` sinon. Compte
/// une image de plus d'âge à chaque appel : à appeler une fois par image.
[[nodiscard]] render::Box cachedSceneBoxOf(SceneBoundsCache& cache, const SceneBoundsFacts& facts,
                                           const flecs::query<const scene::WorldTransform>& all,
                                           const std::map<assets::AssetId, app::ModelGpu>& models,
                                           const render::Instances& instance);

/// Le lointain le plus grand que l'éditeur demande : au-delà, le proche (lointain / `MaxClipRatio`)
/// passerait 10 m et rognerait le premier plan.
inline constexpr float MaxEditorFarPlane = 100000.0f;

/// **Les plans de découpe de la caméra de l'éditeur**, le lointain d'abord : celui de
/// `clipPlanesFor` pour `sceneBox`, **jamais sous `DefaultFarPlane`** (un terrain, l'eau, l'herbe
/// ou un ciel sont tracés par une passe, non par une entité : les bornes de la scène ne les
/// contiennent pas) et plafonné à `MaxEditorFarPlane`. Le proche vient **du seul rapport**
/// (lointain / `MaxClipRatio`, au moins `MinNearPlane`), jamais de l'entité la plus proche : l'œil
/// dans la boîte de la scène, ou loin d'elle, le sol devant lui reste entier.
[[nodiscard]] ClipPlanes editorClipPlanes(const EditorCamera& camera, const render::Box& sceneBox);

} // namespace levain::editor
