#include "levain/animation/pose.hpp"

#include <cmath>
#include <vector>

#include <ozz/animation/runtime/blending_job.h>
#include <ozz/animation/runtime/local_to_model_job.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/base/maths/simd_math.h>
#include <ozz/base/maths/soa_transform.h>
#include <ozz/base/span.h>

#include "ozz_data.hpp"

#include "levain/core/assert.hpp"
#include "levain/core/profile.hpp"

namespace levain::animation
{

float loopedRatio(float seconds, float duration)
{
    const float looped = std::fmod(seconds, duration);
    return (looped < 0.0f ? looped + duration : looped) / duration;
}

void sampleBlend(const AnimationSet& set, std::span<const ClipLayer> layers, Pose& pose)
{
    LEVAIN_PROFILE_SCOPE();
    const ozz::animation::Skeleton& skeleton = *set.ozz->skeleton;
    const auto soaJoints = static_cast<std::size_t>(skeleton.num_soa_joints());

    // ponytail: les tampons sont alloués à chaque appel ; les garder par personnage si un profil
    // le demande (3 µs par image pour Fox, #117). Le contexte d'ozz mémorise aussi où il en était
    // dans le clip, et accélérerait un échantillonnage image après image.
    std::vector<std::vector<ozz::math::SoaTransform>> locals;
    std::vector<ozz::animation::BlendingJob::Layer> blendLayers;
    locals.reserve(layers.size());
    blendLayers.reserve(layers.size());
    for (const ClipLayer& layer : layers)
    {
        LEVAIN_ASSERT(layer.clip < set.clips.size(), "indice de clip hors limites");
        if (layer.weight <= 0.0f)
        {
            continue;
        }
        const ozz::animation::Animation& animation = *set.ozz->clips[layer.clip];
        ozz::animation::SamplingJob::Context context(animation.num_tracks());
        std::vector<ozz::math::SoaTransform>& local = locals.emplace_back(soaJoints);

        // Trois jobs d'ozz (manuel d'ozz, « Sampling », « Blending », « Local to model ») : chaque
        // clip donne la transformation locale de chaque os, rangée par quatre os (SoA) ; le
        // mélange les pondère ; la hiérarchie donne enfin la matrice de chaque os.
        ozz::animation::SamplingJob sampling;
        sampling.animation = &animation;
        sampling.context = &context;
        sampling.ratio = layer.ratio;
        sampling.output = ozz::make_span(local);
        LEVAIN_VERIFY(sampling.Run(), "échantillonnage refusé par ozz");
        blendLayers.push_back({.weight = layer.weight,
                               .transform = ozz::make_span(std::as_const(local)),
                               .joint_weights = {}});
    }

    // Sans couche de poids positif, ozz rend la pose de repos du squelette.
    std::vector<ozz::math::SoaTransform> blended(soaJoints);
    ozz::animation::BlendingJob blending;
    blending.layers = ozz::make_span(std::as_const(blendLayers));
    blending.rest_pose = skeleton.joint_rest_poses();
    blending.output = ozz::make_span(blended);
    LEVAIN_VERIFY(blending.Run(), "mélange refusé par ozz");

    std::vector<ozz::math::Float4x4> models(static_cast<std::size_t>(skeleton.num_joints()));
    ozz::animation::LocalToModelJob localToModel;
    localToModel.skeleton = &skeleton;
    localToModel.input = ozz::make_span(std::as_const(blended));
    localToModel.output = ozz::make_span(models);
    LEVAIN_VERIFY(localToModel.Run(), "calcul des matrices refusé par ozz");

    // ozz et glm rangent tous deux leurs matrices par colonnes.
    pose.joints.resize(models.size());
    for (std::size_t joint = 0; joint < models.size(); ++joint)
    {
        for (int column = 0; column < 4; ++column)
        {
            ozz::math::StorePtrU(models[joint].cols[column], &pose.joints[joint][column][0]);
        }
    }
}

void samplePose(const AnimationSet& set, std::size_t clip, float seconds, Pose& pose)
{
    LEVAIN_ASSERT(clip < set.clips.size(), "indice de clip hors limites");
    const ClipLayer layer{.clip = clip,
                          .ratio = loopedRatio(seconds, set.clips[clip].durationSeconds),
                          .weight = 1.0f};
    sampleBlend(set, std::span{&layer, 1}, pose);
}

void skinningMatrices(const AnimationSet& set, const Pose& pose, std::vector<glm::mat4>& matrices)
{
    matrices.resize(set.skinJoints.size());
    for (std::size_t joint = 0; joint < matrices.size(); ++joint)
    {
        matrices[joint] = set.skeletonToModel * pose.joints[set.skinJoints[joint]] *
                          set.inverseBindMatrices[joint];
    }
}

} // namespace levain::animation
