/// Benchmark du critère de M6.1 : 1 000 caisses en chute libre sur un sol, et pas un seul pas de
/// simulation au-dessus de 4 ms (ADR-0026).
///
/// Un pas, c'est ce que paie le jeu : le pipeline de simulation entier, donc la construction des
/// corps, le pas de Jolt et la recopie dans les `Transform`. La scène est celle de
/// `levain_sandbox --view physics` (`sandbox/src/crates.hpp`) : les caisses tombent d'une grille de
/// 10 × 10 × 10, s'entrechoquent et s'éboulent ; les premiers pas sont la chute libre, les suivants
/// les contacts, les derniers le tas qui s'endort.
///
/// Exécutable séparé, hors de `ctest`, comme levain_scene_bench : le chiffre dépend de la machine.
/// Il se mesure en Release sur la machine de référence (SPECS §10) et se consigne dans le journal :
///   ./build/linux-release/tests/levain_physics_bench [threads de travail]
///
/// Le critère n'est déclaré tenu que si le banc a vraiment simulé : 1 001 corps, un pas par appel,
/// et des caisses retombées. Sans ça, un `BuildBodies` cassé rendrait des pas gratuits (règle n°7).

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <exception>
#include <optional>
#include <print>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

#include <flecs.h>

#include "crates.hpp"

#include "levain/physics/physics.hpp"
#include "levain/physics/physics_world.hpp"
#include "levain/scene/scene.hpp"

namespace
{

constexpr int Steps = 600; ///< 10 s de simulation à 60 Hz : la chute, les chocs, le tas qui dort.
constexpr float StepSeconds = 1.0f / 60.0f;
constexpr double BudgetMilliseconds = 4.0;
constexpr std::uint32_t ExpectedBodies = 1001; ///< Les caisses et le sol.
/// Le sol de la démo s'étend jusqu'à l'horizon ; sa taille ne change rien aux caisses.
constexpr float GroundSize = 1000.0f;

/// Le nombre de threads de travail donné en argument, la valeur par défaut du moteur sans argument,
/// et rien pour un argument illisible : il ne doit pas tourner en silence avec la valeur par
/// défaut.
std::optional<int> parseThreads(std::span<char* const> arguments)
{
    if (arguments.size() < 2)
    {
        return -1; // les cœurs moins un, comme le moteur
    }
    const std::string_view text{arguments[1]};
    int threads = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), threads);
    if (error != std::errc{} || end != text.data() + text.size() || threads < 0)
    {
        return std::nullopt;
    }
    return threads;
}

/// Un pas de simulation, chronométré. `advanceWorld` doit en faire exactement un : sinon, ce n'est
/// plus un pas qu'on mesure.
std::optional<double> timedStep(flecs::world& world, levain::scene::FixedStep& step)
{
    const auto start = std::chrono::steady_clock::now();
    const int steps = levain::scene::advanceWorld(world, step, StepSeconds);
    const auto end = std::chrono::steady_clock::now();
    if (steps != 1)
    {
        return std::nullopt;
    }
    return std::chrono::duration<double, std::milli>(end - start).count();
}

int run(int workerThreads)
{
    flecs::world world;
    world.set<levain::physics::PhysicsSettings>({.workerThreads = workerThreads});
    world.import<levain::physics::PhysicsModule>();
    levain::sandbox::spawnCrates(world, GroundSize);
    levain::scene::FixedStep step;

    // Le premier pas construit les 1 001 corps (BuildBodies) : un coût de chargement, mesuré à
    // part, et non un pas de simulation.
    const std::optional<double> build = timedStep(world, step);
    std::vector<double> milliseconds;
    milliseconds.reserve(Steps);
    for (int i = 0; i < Steps && build; ++i)
    {
        const std::optional<double> elapsed = timedStep(world, step);
        if (!elapsed)
        {
            break;
        }
        milliseconds.push_back(*elapsed);
    }

    const levain::physics::PhysicsWorld& physics = world.get<levain::physics::PhysicsWorld>();
    const std::uint32_t bodies = levain::physics::bodyCount(physics);
    const float highest = levain::sandbox::highestCrate(world);
    if (!build || milliseconds.size() != Steps || bodies != ExpectedBodies ||
        highest > levain::sandbox::HighestCrateStart - 5.0f)
    {
        std::println(
            stderr,
            "ÉCHEC : le banc n'a pas simulé ce qu'il croit mesurer ({} pas d'un pas sur {}, "
            "{} corps sur {}, la plus haute caisse à {:.2f} m)",
            milliseconds.size(), Steps, bodies, ExpectedBodies, highest);
        return 1;
    }

    std::vector<double> sorted = milliseconds;
    std::ranges::sort(sorted);
    double sum = 0.0;
    for (const double value : milliseconds)
    {
        sum += value;
    }
    const auto worst = std::ranges::max_element(milliseconds);
    const int threads =
        workerThreads >= 0 ? workerThreads
                           : std::max(0, static_cast<int>(std::thread::hardware_concurrency()) - 1);
    std::println("M6.1 : {} corps, {} pas, {} threads de travail en plus du principal", bodies,
                 Steps, threads);
    std::println("  1er pas, construction des corps compris : {:.3f} ms", *build);
    std::println("  pas moyen  : {:.3f} ms", sum / Steps);
    std::println("  médiane    : {:.3f} ms", sorted[Steps / 2]);
    std::println("  99e centile: {:.3f} ms", sorted[(Steps * 99) / 100]);
    std::println("  pire pas   : {:.3f} ms (pas n° {})", *worst,
                 std::distance(milliseconds.begin(), worst) + 1);
    std::println("  la plus haute caisse, à la fin : {:.2f} m (partie de {:.2f} m)", highest,
                 levain::sandbox::HighestCrateStart);
    const bool met = *worst < BudgetMilliseconds;
    std::println("  critère    : aucun pas au-dessus de {} ms → {}", BudgetMilliseconds,
                 met ? "tenu" : "RATÉ");
    return met ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
    // Tout dans le try : std::println peut lever, et une exception ne doit pas sortir de main.
    try
    {
        const std::optional<int> threads =
            parseThreads(std::span{argv, static_cast<std::size_t>(argc)});
        if (!threads)
        {
            std::println(stderr, "usage : levain_physics_bench [threads de travail, 0 ou plus]");
            return 2;
        }
        return run(*threads);
    }
    catch (const std::exception& e)
    {
        std::fputs(e.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
}
