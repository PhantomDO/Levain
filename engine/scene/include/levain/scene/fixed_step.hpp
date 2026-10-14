#pragma once

#include <algorithm>

namespace levain::scene
{

/// L'horloge de la simulation : elle avance par pas entiers, jamais du temps réel de l'image
/// (ADR-0016). Le temps des images s'accumule ici jusqu'à former un pas.
struct FixedStep
{
    float stepSeconds = 1.0f / 60.0f; ///< 60 Hz : le pas de simulation par défaut (SPECS §7).
    float accumulator = 0.0f; ///< Le temps pas encore simulé, toujours plus court qu'un pas.
    int maxStepsPerFrame = 4; ///< Le garde-fou contre la « spirale de la mort ».
};

/// Ce qu'une image doit faire : combien de pas de simulation, et où en est le rendu entre les deux
/// derniers (`alpha`, entre 0 et 1).
struct StepPlan
{
    int steps = 0;
    float alpha = 1.0f;
};

/// Range `frameSeconds` dans l'accumulateur et en sort des pas entiers.
///
/// Le temps au-delà du plafond est **abandonné** : sans ça, une image lente réclamerait plus de
/// pas, qui la rendraient plus lente encore, jusqu'à ce que le jeu ne réponde plus (ADR-0016, la «
/// spirale de la mort »). Le jeu ralentit alors par rapport à l'horloge, mais garde la main.
inline StepPlan planSteps(FixedStep& step, float frameSeconds)
{
    const float budget = step.stepSeconds * static_cast<float>(step.maxStepsPerFrame);
    step.accumulator += std::clamp(frameSeconds, 0.0f, budget);
    const int steps = static_cast<int>(step.accumulator / step.stepSeconds);
    step.accumulator -= static_cast<float>(steps) * step.stepSeconds;
    return {.steps = steps, .alpha = step.accumulator / step.stepSeconds};
}

/// Le plan d'une image dont la simulation est arrêtée (ADR-0036, décision 1) : aucun pas, et un
/// `alpha` de 1, pour que l'image montre le `Transform` tel qu'il est, non un mélange de l'ancien
/// et du nouveau que l'éditeur vient d'écrire.
[[nodiscard]] constexpr StepPlan pausedPlan()
{
    return {.steps = 0, .alpha = 1.0f};
}

/// Le plan de l'image : celui de l'accumulateur, ou `pausedPlan` quand la simulation est arrêtée.
/// À l'arrêt, **l'accumulateur ne reçoit pas `frameSeconds`** : il le rangerait pour rien, et le
/// retour au jeu jouerait d'un coup les pas que l'arrêt a empêchés.
inline StepPlan planFrame(FixedStep& step, float frameSeconds, bool simulationPaused)
{
    return simulationPaused ? pausedPlan() : planSteps(step, frameSeconds);
}

} // namespace levain::scene
