#pragma once

// L'anneau du journal (ADR-0036, décision 16) : les derniers messages, en nombre borné, avec le
// compte de tous ceux qui sont passés. Il alimente la fenêtre « Console » de l'éditeur ; il ne sait
// rien d'ImGui. Écrit de n'importe quel fil (le récepteur de `captureLog`), lu par celui de
// l'interface.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
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
/// copié ni déplacé (le mutex) : `LogCapture` le tient sur le tas.
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

/// Le message le plus récent, ou rien si l'anneau est vide : sous le mutex, sans recopier les
/// entrées (la barre d'état d'une image n'en veut qu'une).
[[nodiscard]] std::optional<LogEntry> lastEntryOf(const LogRing& ring);

/// Les comptes seuls, sous le mutex. Lus à part de `lastEntryOf` : un message écrit entre les deux
/// ne fait qu'avancer le compte d'un cran sur le texte, ce que l'image suivante corrige.
[[nodiscard]] LogCounts countsOf(const LogRing& ring);

/// Un anneau qui reçoit le journal tant qu'il vit. Le destructeur retire le récepteur *avant* que
/// les membres ne partent : aucun appel ne peut plus atteindre l'anneau une fois libéré, quel que
/// soit l'ordre des déclarations. Le piège est de déplacer *sur* une capture : l'affectation par
/// défaut libérerait l'ancien anneau pendant que l'ancien récepteur est encore inscrit, et un fil
/// qui journalise écrirait dans de la mémoire libérée. Elle est donc interdite ; remplacer une
/// capture se fait en la détruisant (`std::optional::emplace`, ou un `shared_ptr` neuf).
struct LogCapture
{
    LogCapture() = default;
    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;
    /// L'anneau reste à la même adresse (sur le tas) : le récepteur déplacé le vise encore.
    LogCapture(LogCapture&&) noexcept = default;
    LogCapture& operator=(LogCapture&&) = delete;

    ~LogCapture() { sink.reset(); }

    std::unique_ptr<LogRing> ring;
    LogSinkHandle sink;
};

[[nodiscard]] LogCapture captureLog(std::size_t capacity);

} // namespace levain::core
