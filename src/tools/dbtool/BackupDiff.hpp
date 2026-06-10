// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace Lightweight::Tools
{

/// A single table-level difference (or notable event) observed while comparing two backup archives.
/// Streamed to a BackupDiffObserver as each table is processed, so the caller learns *where* a diff
/// happened (and can render progress) instead of only receiving an aggregate pass/fail code.
struct BackupDiffEvent
{
    /// What kind of table-level outcome this event reports.
    enum class Kind : uint8_t
    {
        OnlyInLeft,  ///< The table exists only in the left archive.
        OnlyInRight, ///< The table exists only in the right archive.
        Identical,   ///< The table's row multiset is identical on both sides.
        Differing,   ///< The table's row multiset differs between the archives.
        ReadError,   ///< A common table's chunk data could not be read/decoded on one or both sides.
    };

    Kind kind = {};                         ///< The outcome this event reports (set before emission).
    std::string table;                      ///< Sanitized (data-path) table name the event concerns.
    bool ignored = false;                   ///< True if @ref table is in the ignore set (informational only).
    uint64_t leftRowCount = 0;              ///< Rows read on the left side (Identical / Differing).
    uint64_t rightRowCount = 0;             ///< Rows read on the right side (Differing).
    uint64_t onlyInLeft = 0;                ///< Row-occurrences present only on the left (Differing).
    uint64_t onlyInRight = 0;               ///< Row-occurrences present only on the right (Differing).
    std::vector<std::string> leftExamples;  ///< Up to a few only-in-left row digests (Differing).
    std::vector<std::string> rightExamples; ///< Up to a few only-in-right row digests (Differing).
    bool leftReadOk = true;                 ///< Left chunk decoded successfully (ReadError reports false here or below).
    bool rightReadOk = true;                ///< Right chunk decoded successfully (ReadError reports false).
};

/// Dependency-injected sink for the per-table diff events produced by BackupDiff. The CLI wires a
/// console implementation that prints each event; tests can supply a capturing implementation to
/// assert on the structured outcome without parsing text. Pass `nullptr` to BackupDiff to suppress
/// streaming entirely (the aggregate BackupDiffResult is always returned).
class BackupDiffObserver
{
  public:
    BackupDiffObserver() = default;
    BackupDiffObserver(BackupDiffObserver const&) = default;
    BackupDiffObserver(BackupDiffObserver&&) = default;
    BackupDiffObserver& operator=(BackupDiffObserver const&) = default;
    BackupDiffObserver& operator=(BackupDiffObserver&&) = default;
    virtual ~BackupDiffObserver() = default;

    /// Called once per table-level outcome as the comparison proceeds.
    /// @param event The outcome for one table.
    virtual void OnEvent(BackupDiffEvent const& event) = 0;
};

/// Aggregate outcome of comparing two backup archives.
struct BackupDiffResult
{
    bool archivesReadable = true;       ///< False if either archive could not be opened (no comparison ran).
    bool leftReadable = true;           ///< False if the left archive could not be opened.
    bool rightReadable = true;          ///< False if the right archive could not be opened.
    bool differenceFound = false;       ///< True if any non-ignored difference (or read error) was detected.
    std::size_t comparedTables = 0;     ///< Common tables whose row multisets were compared.
    std::size_t identicalTables = 0;    ///< Common tables that compared identical.
    std::size_t differingTables = 0;    ///< Common tables that differed (or failed to read).
    std::size_t ignoredDifferences = 0; ///< Differences suppressed by the ignore set.
};

/// Compares the DATA CONTENT of two Lightweight backup archives, order-independently.
///
/// This is a pure file comparison: it never opens a database connection. It is used to
/// detect silent data corruption — e.g. proving that a concurrent (multi-threaded) backup
/// contains the same set of rows as a single-threaded baseline. Because chunk boundaries
/// and row order can legitimately differ between two backups of the same database, this
/// compares the *multiset of rows* per table rather than chunk-level checksums.
///
/// The function performs no output of its own: every table-level outcome is streamed to
/// @p observer (if non-null) and summarized in the returned BackupDiffResult.
///
/// @param left  Path to the first ("left"/baseline) backup archive.
/// @param right Path to the second ("right"/candidate) backup archive.
/// @param ignoreTables Sanitized table names whose differences are reported but do NOT count
///        as a failure. Use for high-write tables (e.g. log tables) that legitimately drift
///        between two backups of a *live* database, so a stress comparison isn't drowned out
///        by benign live-data churn. The match is on the data-path (sanitized) table name.
///        Pass an empty set to treat every difference as a failure (no default — be explicit).
/// @param observer Streaming sink for per-table events, or nullptr to suppress streaming.
/// @return The aggregate result. `differenceFound` is the authoritative pass/fail signal; it is
///         true when any non-ignored difference is found or either archive cannot be read.
[[nodiscard]] BackupDiffResult BackupDiff(std::filesystem::path const& left,
                                          std::filesystem::path const& right,
                                          std::set<std::string> const& ignoreTables,
                                          BackupDiffObserver* observer);

} // namespace Lightweight::Tools
