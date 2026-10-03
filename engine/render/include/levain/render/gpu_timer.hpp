#pragma once

#include <array>
#include <cstddef>
#include <optional>

#include <nvrhi/nvrhi.h>

namespace levain::render
{

/// Mesure du temps que le GPU passe sur une frame, par des timer queries : le GPU horodate le début
/// et la fin, rien n'est estimé côté CPU.
///
/// Le résultat d'une frame n'est lisible que quand le GPU l'a finie, soit deux frames plus tard
/// (deux frames en vol, `swapchain_vk.cpp`). D'où un anneau de trois requêtes : on relit la plus
/// ancienne au moment de la réutiliser.
struct GpuTimer
{
    std::array<nvrhi::TimerQueryHandle, 3> queries;
    std::array<bool, 3> isPending{};
    std::size_t next = 0;
};

[[nodiscard]] GpuTimer createGpuTimer(nvrhi::IDevice& device);

/// Commence la mesure de la frame dans `commandList`. Rend, en millisecondes, la mesure de la frame
/// qui occupait la même requête trois frames plus tôt, si elle est prête.
[[nodiscard]] std::optional<double>
beginGpuTimer(nvrhi::IDevice& device, nvrhi::ICommandList& commandList, GpuTimer& timer);

/// Termine la mesure commencée par `beginGpuTimer`, dans la même command list.
void endGpuTimer(nvrhi::ICommandList& commandList, GpuTimer& timer);

/// La somme des mesures d'un timer, et leur nombre : de quoi donner une moyenne.
struct GpuTimeAverage
{
    double totalMs = 0.0;
    int samples = 0;
};

/// La moyenne des mesures, 0 sans mesure.
[[nodiscard]] inline double averageOf(const GpuTimeAverage& average)
{
    return average.samples > 0 ? average.totalMs / average.samples : 0.0;
}

} // namespace levain::render
