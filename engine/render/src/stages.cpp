#include "levain/render/stages.hpp"

#include <format>
#include <functional>
#include <set>
#include <string>
#include <utility>

#include "levain/core/profile.hpp"

namespace levain::render
{

namespace
{

/// Un nom interné : la même adresse toute la session, ce que Tracy exige d'un nom de courbe
/// (LEVAIN_PROFILE_PLOT). Les noms ne sont jamais libérés : quelques dizaines d'octets par
/// fonction.
const char* internedName(std::string name)
{
    static std::set<std::string, std::less<>> names;
    return names.insert(std::move(name)).first->c_str();
}

} // namespace

void addStageFunction(RenderStages& stages, RenderStage stage, std::string name,
                      StageFunction function)
{
    const std::string label =
        std::format("{}/{}", RenderStageNames[static_cast<std::size_t>(stage)], name);
    stages.entries[static_cast<std::size_t>(stage)].push_back(
        {.name = std::move(name),
         .function = std::move(function),
         .label = internedName(label),
         .plotName = internedName(std::format("GPU {}", label)),
         .calls = {}});
}

void runStage(RenderStages& stages, RenderStage stage, const StageContext& context)
{
    for (RenderStages::Entry& entry : stages.entries[static_cast<std::size_t>(stage)])
    {
        LEVAIN_PROFILE_SCOPE_TEXT(entry.label);
        if (!stages.timeFunctions)
        {
            entry.function(context);
            continue;
        }
        // Un appel par cascade : l'anneau de trois requêtes d'un minuteur suppose un seul appel par
        // image (gpu_timer.hpp). Une étape appelée deux fois pour la même cascade le casserait.
        while (entry.calls.size() <= context.cascade)
        {
            entry.calls.push_back(
                {.timer = createGpuTimer(context.device), .average = {}, .lastMs = 0.0});
        }
        RenderStages::TimedCall& call = entry.calls[context.cascade];
        if (const auto ms = beginGpuTimer(context.device, context.commandList, call.timer))
        {
            call.average.totalMs += *ms;
            ++call.average.samples;
            call.lastMs = *ms;
        }
        entry.function(context);
        endGpuTimer(context.commandList, call.timer);
    }
}

void plotStageTimes([[maybe_unused]] const RenderStages& stages)
{
#if defined(LEVAIN_PROFILING_ENABLED) && LEVAIN_PROFILING_ENABLED
    for (const auto& entries : stages.entries)
    {
        for (const RenderStages::Entry& entry : entries)
        {
            double lastMs = 0.0;
            for (const RenderStages::TimedCall& call : entry.calls)
            {
                lastMs += call.lastMs;
            }
            if (!entry.calls.empty())
            {
                LEVAIN_PROFILE_PLOT(entry.plotName, lastMs);
            }
        }
    }
#endif
}

std::string describeStageTimes(const RenderStages& stages)
{
    std::string description;
    for (const auto& entries : stages.entries)
    {
        for (const RenderStages::Entry& entry : entries)
        {
            double frameMs = 0.0;
            bool measured = false;
            for (const RenderStages::TimedCall& call : entry.calls)
            {
                frameMs += averageOf(call.average);
                measured = measured || call.average.samples > 0;
            }
            if (measured)
            {
                description += std::format("{}{} {:.3f} ms", description.empty() ? "" : ", ",
                                           entry.label, frameMs);
            }
        }
    }
    return description.empty() ? "aucune mesure" : description;
}

std::string describeStages(const RenderStages& stages)
{
    std::string description;
    for (std::size_t stage = 0; stage < RenderStageNames.size(); ++stage)
    {
        std::string names;
        for (const RenderStages::Entry& entry : stages.entries[stage])
        {
            names += std::format("{}{}", names.empty() ? "" : ", ", entry.name);
        }
        description += std::format("{}{} : {}", stage == 0 ? "" : " ; ", RenderStageNames[stage],
                                   names.empty() ? "aucune" : names);
    }
    return description;
}

} // namespace levain::render
