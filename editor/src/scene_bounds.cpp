#include "levain/editor/scene_bounds.hpp"

#include <algorithm>

#include "levain/assets/asset_ref.hpp"

namespace levain::editor
{

namespace
{

render::Box unitBox()
{
    return {.min = glm::vec3{-UnitBoxHalfSize}, .max = glm::vec3{UnitBoxHalfSize}};
}

render::Box unionOf(const render::Box& a, const render::Box& b)
{
    return {.min = glm::min(a.min, b.min), .max = glm::max(a.max, b.max)};
}

} // namespace

render::Box entityBoxOf(const flecs::entity& entity,
                        const std::map<assets::AssetId, app::ModelGpu>& models,
                        const render::Instances& instance)
{
    const auto* world = entity.try_get<scene::WorldTransform>();
    if (world == nullptr)
    {
        return unitBox();
    }
    if (const auto* mesh = entity.try_get<assets::MeshRef>())
    {
        if (const auto found = models.find(mesh->mesh.asset); found != models.end())
        {
            std::optional<render::Box> box;
            const auto& meshes = found->second.meshes;
            if (mesh->mesh.sub >= meshes.size())
            {
                return render::transformed(unitBox(), world->matrix);
            }
            for (const app::ModelPrimitiveGpu& primitive : meshes[mesh->mesh.sub])
            {
                if (const auto part =
                        render::worldBoundsOf(primitive.mesh, instance, world->matrix))
                {
                    box = box ? unionOf(*box, *part) : *part;
                }
            }
            if (box)
            {
                return *box;
            }
        }
    }
    return render::transformed(unitBox(), world->matrix);
}

render::Box sceneBoxOf(const flecs::query<const scene::WorldTransform>& everything,
                       const std::map<assets::AssetId, app::ModelGpu>& models,
                       const render::Instances& instance)
{
    std::optional<render::Box> scene;
    everything.each(
        [&](flecs::entity entity, const scene::WorldTransform&)
        {
            const render::Box box = entityBoxOf(entity, models, instance);
            scene = scene ? unionOf(*scene, box) : box;
        });
    return scene.value_or(unitBox());
}

render::Box cachedSceneBoxOf(SceneBoundsCache& cache, const SceneBoundsFacts& facts,
                             const flecs::query<const scene::WorldTransform>& all,
                             const std::map<assets::AssetId, app::ModelGpu>& models,
                             const render::Instances& instance)
{
    if (sceneBoundsStale(cache, facts))
    {
        cache = {.box = sceneBoxOf(all, models, instance),
                 .valid = true,
                 .frameAge = 0,
                 .entityCount = facts.entityCount,
                 .modelCount = facts.modelCount};
    }
    ++cache.frameAge;
    return cache.box;
}

ClipPlanes editorClipPlanes(const EditorCamera& camera, const render::Box& sceneBox)
{
    // Le lointain d'abord : la scène, jamais sous le défaut (les passes de terrain, d'eau, d'herbe
    // et de ciel ne sont pas des entités), jamais au-delà du plafond (le proche en découlerait).
    const float farPlane = std::min(
        std::max(clipPlanesFor(camera, sceneBox).farPlane, DefaultFarPlane), MaxEditorFarPlane);
    // Le proche ne vient que du rapport : l'entité la plus proche ne dit rien du sol ou du terrain
    // qui passe devant elle, et un proche à 0,9 de sa profondeur les rognerait.
    return {.nearPlane = std::max(MinNearPlane, farPlane / MaxClipRatio), .farPlane = farPlane};
}

} // namespace levain::editor
