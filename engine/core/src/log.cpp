#include "levain/core/log.hpp"

#include <algorithm>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "levain/core/assert.hpp"

namespace levain::core
{
namespace
{

spdlog::level::level_enum toSpdlog(LogLevel level)
{
    switch (level)
    {
    case LogLevel::Trace:
        return spdlog::level::trace;
    case LogLevel::Debug:
        return spdlog::level::debug;
    case LogLevel::Info:
        return spdlog::level::info;
    case LogLevel::Warning:
        return spdlog::level::warn;
    case LogLevel::Error:
        return spdlog::level::err;
    case LogLevel::Critical:
        return spdlog::level::critical;
    }

    return spdlog::level::info;
}

/// Table des catégories, créées à la première utilisation.
///
/// Une recherche par chaîne à chaque appel : c'est le point à surveiller si une capture
/// Tracy montre le log dans le profil (M0.3, issue #8). La réponse sera alors un handle de
/// catégorie obtenu une fois, pas une optimisation de cette table.
std::shared_ptr<spdlog::logger> categoryLogger(std::string_view category)
{
    static std::mutex mutex;
    static std::map<std::string, std::shared_ptr<spdlog::logger>, std::less<>> loggers;

    const std::scoped_lock lock{mutex};

    if (const auto it = loggers.find(category); it != loggers.end())
    {
        return it->second;
    }

    auto logger = spdlog::stdout_color_mt(std::string{category});
    logger->set_pattern("[%T] [%^%l%$] [%n] %v");
    loggers.emplace(category, logger);

    return logger;
}

/// Un récepteur inscrit. Partagé : `dispatchToSinks` en garde une copie le temps de ses appels, que
/// `LogSinkHandle::reset` ait retiré l'inscription entre-temps ou non.
struct SinkSlot
{
    SinkSlot(std::uint64_t slotId, LogSink sink) : id(slotId), function(std::move(sink)) {}

    std::uint64_t id;
    LogSink function;
    /// Tenu pendant chaque appel de `function`, et par `reset` pour attendre la fin de celui d'un
    /// autre fil. Récursif : un récepteur qui se retire lui-même reprend son propre verrou.
    std::recursive_mutex running;
    /// Posé sous `running` : un fil qui avait copié l'inscription avant son retrait n'appelle plus
    /// rien.
    bool removed = false;
};

struct SinkRegistry
{
    std::mutex mutex; ///< Ne protège que la liste, jamais un appel de récepteur.
    std::vector<std::shared_ptr<SinkSlot>> slots;
    std::uint64_t nextId = 1;
};

SinkRegistry& sinkRegistry()
{
    // Jamais détruite : un message écrit par le destructeur d'un objet statique, après la fin de
    // `main`, ne doit pas trouver un verrou et une liste libérés.
    static SinkRegistry* const registry = new SinkRegistry;
    return *registry;
}

double secondsSinceStart()
{
    static const auto start = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

/// Vrai tant que ce fil est dans un récepteur (voir `SinkCallScope`).
thread_local bool insideSinkCall = false;

/// Le piège de la ré-entrée, nommé : un message journalisé depuis un récepteur ne repasse pas par
/// les récepteurs. Il irait sinon au récepteur qui l'a écrit (un écho sans fin), puis aux autres
/// sous le verrou du premier : deux récepteurs qui journalisent, sur deux fils, se
/// bloqueraient l'un l'autre (`LogSink`).
class SinkCallScope
{
public:
    SinkCallScope() { insideSinkCall = true; }

    SinkCallScope(const SinkCallScope&) = delete;
    SinkCallScope& operator=(const SinkCallScope&) = delete;

    ~SinkCallScope() { insideSinkCall = false; }
};

/// Appelle chaque récepteur, sans verrou de la liste (la règle de `LogSink`).
void dispatchToSinks(const LogRecord& record)
{
    if (insideSinkCall)
    {
        return;
    }
    SinkRegistry& registry = sinkRegistry();
    std::vector<std::shared_ptr<SinkSlot>> slots;
    {
        const std::scoped_lock lock{registry.mutex};
        if (registry.slots.empty())
        {
            return;
        }
        slots = registry.slots;
    }
    for (const std::shared_ptr<SinkSlot>& slot : slots)
    {
        const std::scoped_lock running{slot->running};
        if (!slot->removed)
        {
            const SinkCallScope inside;
            slot->function(record);
        }
    }
}

void removeLogSink(std::uint64_t id) noexcept
{
    SinkRegistry& registry = sinkRegistry();
    std::shared_ptr<SinkSlot> slot;
    {
        const std::scoped_lock lock{registry.mutex};
        const auto it = std::ranges::find(registry.slots, id, &SinkSlot::id);
        if (it == registry.slots.end())
        {
            return;
        }
        slot = std::move(*it);
        registry.slots.erase(it);
    }
    // Attend qu'un autre fil sorte du récepteur, puis ferme la porte à ceux qui en avaient copié
    // l'inscription.
    const std::scoped_lock running{slot->running};
    slot->removed = true;
}

} // namespace

void LogSinkHandle::reset() noexcept
{
    if (m_id != 0)
    {
        removeLogSink(std::exchange(m_id, 0));
    }
}

LogSinkHandle addLogSink(LogSink sink)
{
    LEVAIN_ASSERT(static_cast<bool>(sink),
                  "un récepteur de journal vide serait appelé à chaque message");
    if (!sink)
    {
        return {};
    }
    SinkRegistry& registry = sinkRegistry();
    const std::scoped_lock lock{registry.mutex};
    const std::uint64_t id = registry.nextId++;
    registry.slots.push_back(std::make_shared<SinkSlot>(id, std::move(sink)));
    return LogSinkHandle{id};
}

void setLogLevel(std::string_view category, LogLevel level)
{
    categoryLogger(category)->set_level(toSpdlog(level));
}

bool isLogEnabled(std::string_view category, LogLevel level)
{
    return categoryLogger(category)->should_log(toSpdlog(level));
}

void logMessage(std::string_view category, LogLevel level, std::string_view message)
{
    const std::shared_ptr<spdlog::logger> logger = categoryLogger(category);
    logger->log(toSpdlog(level), message);
    // Le même filtre que la console du processus : un récepteur ne voit pas ce qu'elle ne montre
    // pas.
    if (logger->should_log(toSpdlog(level)))
    {
        dispatchToSinks({.level = level,
                         .category = category,
                         .message = message,
                         .seconds = secondsSinceStart()});
    }
}

} // namespace levain::core
