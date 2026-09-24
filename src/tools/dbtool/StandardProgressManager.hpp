// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <Lightweight/SqlBackup.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace Lightweight::Tools
{

/// Type of issue reported during backup/restore operations.
enum class IssueType : std::uint8_t
{
    Error,
    Warning,
    Info
};

/// Represents a single issue (error, warning, or info) associated with a table.
struct TableIssue
{
    std::string message;
    IssueType type;
};

/// Whether the stream a StandardProgressManager draws to is a terminal.
///
/// A terminal gets each repaint bracketed in synchronized output (DEC mode 2026), so it never shows
/// a half-drawn frame; anything else gets the frame's bytes alone.
enum class ProgressDestination : std::uint8_t
{
    NotTerminal,
    Terminal,
};

class OStreamTerminalOutput;

class StandardProgressManager: public Lightweight::SqlBackup::ErrorTrackingProgressManager
{
  public:
    /// Draws to @p out, which is a terminal only if it is std::cout and the process's standard
    /// output is one.
    /// @param useUnicode Whether to draw the progress bars with FiraCode's progress glyphs.
    /// @param out The stream to draw to.
    explicit StandardProgressManager(bool useUnicode, std::ostream& out = std::cout);

    /// Draws to @p out, which is what @p destination says it is.
    /// @param useUnicode Whether to draw the progress bars with FiraCode's progress glyphs.
    /// @param out The stream to draw to.
    /// @param destination Whether @p out is a terminal.
    StandardProgressManager(bool useUnicode, std::ostream& out, ProgressDestination destination);

    StandardProgressManager(StandardProgressManager const&) = delete;
    StandardProgressManager(StandardProgressManager&&) = delete;
    StandardProgressManager& operator=(StandardProgressManager const&) = delete;
    StandardProgressManager& operator=(StandardProgressManager&&) = delete;
    ~StandardProgressManager() override;

    void Update(SqlBackup::Progress const& p) override;
    void AllDone() override;
    void SetMaxTableNameLength(size_t len) override;
    void SetTotalItems(size_t totalItems) override;
    void AddTotalItems(size_t additionalItems) override;
    void OnItemsProcessed(size_t count) override;

  private:
    void PrintSummaryLine();
    void PrintLine(int lineIndex, SqlBackup::Progress const& p) const;
    bool IsPinnedTable(std::string const& tableName) const;
    void InsertLineAbovePinned(std::string const& tableName);
    void RepaintAllLines();

    std::unique_ptr<OStreamTerminalOutput> _output;
    std::mutex mutable _mutex;
    std::map<std::string, int> _tableLines;
    std::map<std::string, SqlBackup::Progress> _tableProgresses;
    std::vector<std::string> _lineTableMapping;
    int _numFinished = 0;
    int _nextLineIndex = 0;
    int _maxTableNameLength = 20;
    bool _useUnicode = false;
    std::map<std::string, std::vector<TableIssue>> _issuesByTable;
    std::set<std::string> _tablesWithWarnings;
    std::map<std::string, std::chrono::steady_clock::time_point> _tableStartTimes;
    std::chrono::steady_clock::time_point _startTime = std::chrono::steady_clock::now();
    std::string _pinnedTableName; ///< Table name that should stay pinned at the bottom
    bool _hasPinnedLine = false;  ///< Whether a pinned line is currently active

    // Global progress tracking for ETA
    size_t _totalItems = 0;
    std::atomic<size_t> _processedItems { 0 };

    // Rate calculation (exponential moving average)
    std::chrono::steady_clock::time_point _lastRateSampleTime;
    size_t _lastRateSampleItems = 0;
    double _smoothedRate = 0.0; ///< items/second

    // Summary line for ETA display (always at the bottom, below all table lines)
    bool _hasSummaryLine = false;
    bool _summaryLineAllocated = false; // True once we've printed "\n" for the summary line
    bool _isFinished = false;           // True when AllDone() is called, allows showing 100%
};

} // namespace Lightweight::Tools
