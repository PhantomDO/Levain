#include "levain/render/stages.hpp"

#include <format>
#include <utility>

namespace levain::render
{

void addStageFunction(RenderStages& stages, RenderStage stage, std::string name,
                      StageFunction function)
{
    stages.entries[static_cast<std::size_t>(stage)].push_back(
        {.name = std::move(name), .function = std::move(function)});
}

void runStage(const RenderStages& stages, RenderStage stage, const StageContext& context)
{
    for (const RenderStages::Entry& entry : stages.entries[static_cast<std::size_t>(stage)])
    {
        entry.function(context);
    }
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
