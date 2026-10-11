#pragma once

// L'anneau du journal (ADR-0036, décision 16) : les derniers messages, en nombre borné, avec le
// compte de tous ceux qui sont passés. Il alimente la fenêtre « Console » de l'éditeur ; il ne sait
// rien d'ImGui. Écrit de n'importe quel fil, lu par celui de l'interface.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "levain/core/log.hpp"

namespace levain::core
{

/// Le niveau tel que la console le montre : trois filtres, comme Unity (Info, Warning, Error).
enum class LogSeverity : std::uint8_t
{
    Info,
    Warning,
    Error,
};

/// Trace, Debug et Info vont à Info ; Critical va à Error. Le piège : filtrer sur `LogLevel`
/// laisserait un `Critical` sans case à cocher.
[[nodiscard]] constexpr LogSeverity severityOf(LogLevel level)
{
    switch (level)
    {
    case LogLevel::Trace:
    case LogLevel::Debug:
    case LogLevel::Info:
        return LogSeverity::Info;
    case LogLevel::Warning:
        return LogSeverity::Warning;
    case LogLevel::Error:
    case LogLevel::Critical:
        return LogSeverity::Error;
    }
    return LogSeverity::Info;
}

struct LogEntry
{
    LogLevel level = LogLevel::Info;
    std::string category;
    std::string text;
    /// Voir `LogRecord::seconds`.
    double seconds = 0.0;
};

/// Les messages reçus depuis le dernier `clearLogRing`, **ceux que l'anneau a déjà oubliés
/// compris** : une erreur sortie de l'anneau par un flot d'infos reste comptée.
struct LogCounts
{
    std::uint64_t infos = 0;
    std::uint64_t warnings = 0;
    std::uint64_t errors = 0;
};

[[nodiscard]] constexpr std::uint64_t countOf(const LogCounts& counts, LogSeverity severity)
{
    switch (severity)
    {
    case LogSeverity::Info:
        return counts.infos;
    case LogSeverity::Warning:
        return counts.warnings;
    case LogSeverity::Error:
        return counts.errors;
    }
    return 0;
}

/// L'anneau. Ses champs ne se touchent que sous `mutex` : les fonctions ci-dessous le font. Jamais
/// copié ni déplacé (le mutex).
struct LogRing
{
    explicit LogRing(std::size_t maxEntries) : capacity(maxEntries) {}

    mutable std::mutex mutex;
    std::size_t capacity;
    std::deque<LogEntry> entries; ///< Du plus ancien au plus récent.
    LogCounts counts;
    /// Change à chaque écriture : l'interface ne recopie l'anneau que s'il a bougé.
    std::uint64_t generation = 0;
};

/// Ajoute un message ; au-delà de la capacité, le plus ancien est oublié (le compte ne l'est pas).
void pushLogEntry(LogRing& ring, LogEntry entry);

/// Vide l'anneau et remet les comptes à zéro (« Effacer »).
void clearLogRing(LogRing& ring);

/// Une copie cohérente de l'anneau : les entrées, leurs comptes et la génération à laquelle on les
/// a lus.
struct LogSnapshot
{
    std::vector<LogEntry> entries;
    LogCounts counts;
    std::uint64_t generation = 0;
};

[[nodiscard]] LogSnapshot snapshotOf(const LogRing& ring);

[[nodiscard]] std::uint64_t generationOf(const LogRing& ring);

} // namespace levain::core
