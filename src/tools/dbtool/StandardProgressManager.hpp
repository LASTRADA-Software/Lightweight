// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <Lightweight/SqlBackup.hpp>

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace Lightweight::Tools
{

class StandardProgressManager: public Lightweight::SqlBackup::ErrorTrackingProgressManager
{
  public:
    explicit StandardProgressManager(bool useUnicode, std::ostream& out = std::cout);

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

    std::ostream& _out;
    std::mutex mutable _mutex;
    std::map<std::string, int> _tableLines;
    std::map<std::string, SqlBackup::Progress> _tableProgresses;
    std::vector<std::string> _lineTableMapping;
    int _numFinished = 0;
    int _nextLineIndex = 0;
    int _maxTableNameLength = 20;
    bool _useUnicode = false;
    std::vector<std::string> _warnings;
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
};

} // namespace Lightweight::Tools
