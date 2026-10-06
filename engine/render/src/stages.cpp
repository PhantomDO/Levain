#include "levain/render/stages.hpp"

#include <format>
#include <memory>
#include <utility>

#include "levain/core/profile.hpp"

namespace levain::render
{

void addStageFunction(RenderStages& stages, RenderStage stage, std::string name,
                      StageFunction function)
{
    auto plotName = std::make_shared<const std::string>(
        std::format("GPU {}/{}", RenderStageNames[static_cast<std::size_t>(stage)], name));
    stages.entries[static_cast<std::size_t>(stage)].push_back({.name = std::move(name),
                                                               .function = std::move(function),
                                                               .timers = {},
                                                               .gpuTime = {},
                                                               .lastFrameMs = 0.0,
                                                               .plotName = std::move(plotName)});
}

namespace
{

/// Ajoute la mesure d'un appel, relue trois images plus tard (gpu_timer.hpp). La cascade 0 ouvre
/// une nouvelle image : les autres s'ajoutent à la sienne. Le piège : sans ce découpage, la
/// moyenne serait celle d'une cascade, quatre fois trop petite pour les ombres.
void addStageReading(RenderStages::Entry& entry, std::uint32_t call, double ms)
{
    entry.gpuTime.totalMs += ms;
    if (call == 0)
    {
        ++entry.gpuTime.samples;
        entry.lastFrameMs = 0.0;
    }
    entry.lastFrameMs += ms;
}

} // namespace

void runStage(RenderStages& stages, RenderStage stage, const StageContext& context)
{
    for (RenderStages::Entry& entry : stages.entries[static_cast<std::size_t>(stage)])
    {
        LEVAIN_PROFILE_SCOPE_TEXT(entry.name);
        while (entry.timers.size() <= context.cascade)
        {
            entry.timers.push_back(createGpuTimer(context.device));
        }
        GpuTimer& timer = entry.timers[context.cascade];
        if (const auto ms = beginGpuTimer(context.device, context.commandList, timer))
        {
            addStageReading(entry, context.cascade, *ms);
        }
        entry.function(context);
        endGpuTimer(context.commandList, timer);
    }
}

void plotStageTimes([[maybe_unused]] const RenderStages& stages)
{
#if defined(LEVAIN_PROFILING_ENABLED) && LEVAIN_PROFILING_ENABLED
    for (const auto& entries : stages.entries)
    {
        for (const RenderStages::Entry& entry : entries)
        {
            LEVAIN_PROFILE_PLOT(entry.plotName->c_str(), entry.lastFrameMs);
        }
    }
#endif
}

std::string describeStageTimes(const RenderStages& stages)
{
    std::string description;
    for (std::size_t stage = 0; stage < RenderStageNames.size(); ++stage)
    {
        for (const RenderStages::Entry& entry : stages.entries[stage])
        {
            if (entry.gpuTime.samples == 0)
            {
                continue;
            }
            description +=
                std::format("{}{}/{} {:.3f} ms", description.empty() ? "" : ", ",
                            RenderStageNames[stage], entry.name, averageOf(entry.gpuTime));
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
