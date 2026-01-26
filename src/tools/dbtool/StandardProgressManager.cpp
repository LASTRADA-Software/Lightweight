// SPDX-License-Identifier: Apache-2.0

#include "StandardProgressManager.hpp"

#include <cmath>
#include <format>
#include <iostream>

#ifdef _WIN32
    #include <io.h> // for _isatty, _fileno
#else
    #include <unistd.h> // for isatty, fileno
#endif

namespace Lightweight::Tools
{

StandardProgressManager::StandardProgressManager(bool useUnicode, std::ostream& out):
    _out { out },
    _useUnicode { useUnicode }
{
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
bool StandardProgressManager::IsPinnedTable(std::string const& tableName) const
{
    // "Scanning schema" is the meta-task that should be pinned at the bottom
    return tableName == "Scanning schema";
}

void StandardProgressManager::InsertLineAbovePinned(std::string const& /*tableName*/)
{
    // Not used in the new pinning approach
}

struct SynchronizedOutputGuard
{
    std::ostream& _out;
    bool _enabled = false;

    SynchronizedOutputGuard(std::ostream& out):
        _out { out }
    {
        Begin();
    }

    ~SynchronizedOutputGuard()
    {
        End();
    }

    SynchronizedOutputGuard(SynchronizedOutputGuard const&) = delete;
    SynchronizedOutputGuard& operator=(SynchronizedOutputGuard const&) = delete;
    SynchronizedOutputGuard(SynchronizedOutputGuard&&) = delete;
    SynchronizedOutputGuard& operator=(SynchronizedOutputGuard&&) = delete;

    static bool IsStdoutTerminal()
    {
#ifdef _WIN32
        return _isatty(_fileno(stdout)) != 0;
#else
        return isatty(fileno(stdout)) != 0;
#endif
    }

    // Begin Synchronized Update (BSU) - prevents screen tearing
    // See: https://github.com/contour-terminal/vt-extensions/blob/master/synchronized-output.md
    void Begin()
    {
        if (_enabled || !IsStdoutTerminal())
            return;
        _out << "\033[?2026h";
        _out.flush();
        _enabled = true;
    }

    // End Synchronized Update (ESU) - terminal renders the complete frame now
    void End()
    {
        if (!_enabled)
            return;
        _out << "\033[?2026l";
        _out.flush();
        _enabled = false;
    }
};

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void StandardProgressManager::Update(SqlBackup::Progress const& p)
{
    std::scoped_lock lock(_mutex);

    ErrorTrackingProgressManager::Update(p);
    _tableProgresses[p.tableName] = p;

    if (p.state == SqlBackup::Progress::State::Warning)
    {
        _warnings.push_back(std::format("[{}] {}", p.tableName, p.message));
        _tablesWithWarnings.insert(p.tableName);
    }

    bool const isPinned = IsPinnedTable(p.tableName);

    // Use synchronized output to prevent screen tearing
    auto const synchronizedOutput = SynchronizedOutputGuard(_out);

    if (!_tableLines.contains(p.tableName))
    {
        _tableStartTimes[p.tableName] = std::chrono::steady_clock::now();

        // Check if this table name is longer than current max - will need to repaint all lines
        bool const needsRepaint = std::cmp_greater(p.tableName.size(), _maxTableNameLength);
        if (needsRepaint)
        {
            _maxTableNameLength = static_cast<int>(p.tableName.size());
        }

        if (isPinned)
        {
            // Pinned table: assign it a line index and mark it as pinned
            // It will always be rendered at the bottom
            _tableLines[p.tableName] = _nextLineIndex++;
            _lineTableMapping.push_back(p.tableName);
            _pinnedTableName = p.tableName;
            _hasPinnedLine = true;
            _out << "\n"; // Reserve a line

            // If we have a summary line and haven't allocated it yet, do so now
            if (_hasSummaryLine && !_summaryLineAllocated)
            {
                _out << "\n"; // Reserve a line for the summary
                _summaryLineAllocated = true;
                PrintSummaryLine();
            }
        }
        else if (_hasPinnedLine)
        {
            // Non-pinned table arriving while pinned is active
            // Insert it at the position just before pinned (visually above it)
            int const pinnedIndex = _tableLines[_pinnedTableName];
            int const newIndex = pinnedIndex; // Take pinned's current position

            // Shift pinned down by one
            _tableLines[_pinnedTableName] = _nextLineIndex;
            _lineTableMapping.push_back(_pinnedTableName);

            // Assign new table to the old pinned position
            _tableLines[p.tableName] = newIndex;
            _lineTableMapping[static_cast<size_t>(newIndex)] = p.tableName;

            _nextLineIndex++;
            _out << "\n"; // Reserve a new line

            // Handle summary line: allocate if needed, always repaint at new position
            if (_hasSummaryLine)
            {
                if (!_summaryLineAllocated)
                {
                    _out << "\n"; // Reserve a line for the summary
                    _summaryLineAllocated = true;
                }
            }

            // Repaint pinned at its new position (will be included in RepaintAllLines if needed)
            if (!needsRepaint)
            {
                PrintLine(_tableLines[_pinnedTableName], _tableProgresses[_pinnedTableName]);
                // Repaint summary line at its new position (below pinned)
                if (_hasSummaryLine)
                    PrintSummaryLine();
            }
        }
        else
        {
            // Normal case: no pinned line, just append
            _tableLines[p.tableName] = _nextLineIndex++;
            _lineTableMapping.push_back(p.tableName);
            _out << "\n"; // Reserve a line

            // Handle summary line: allocate if needed, always repaint at new position
            if (_hasSummaryLine)
            {
                if (!_summaryLineAllocated)
                {
                    _out << "\n"; // Reserve a line for the summary
                    _summaryLineAllocated = true;
                }
                PrintSummaryLine();
            }
        }

        // If table name was longer than current max, repaint all lines for alignment
        if (needsRepaint && _nextLineIndex > 1)
        {
            RepaintAllLines();
            return; // Current table was already painted by RepaintAllLines
        }
    }

    if (p.state == SqlBackup::Progress::State::Finished || p.state == SqlBackup::Progress::State::Error)
    {
        int const currentIndex = _tableLines[p.tableName];

        // Handle pinned line finishing
        if (isPinned && _hasPinnedLine)
        {
            _hasPinnedLine = false;
            _pinnedTableName.clear();
        }

        if (currentIndex >= _numFinished)
        {
            // Move to finished section
            int const targetIndex = _numFinished;
            if (currentIndex != targetIndex)
            {
                // Shift others down
                for (int i = currentIndex; i > targetIndex; --i)
                {
                    _lineTableMapping[static_cast<size_t>(i)] = _lineTableMapping[static_cast<size_t>(i - 1)];
                    _tableLines[_lineTableMapping[static_cast<size_t>(i)]] = i;
                }
                _lineTableMapping[static_cast<size_t>(targetIndex)] = p.tableName;
                _tableLines[p.tableName] = targetIndex;

                // Repaint affected lines
                for (int i = targetIndex; i <= currentIndex; ++i)
                {
                    PrintLine(i, _tableProgresses[_lineTableMapping[static_cast<size_t>(i)]]);
                }
            }
            else
            {
                PrintLine(currentIndex, p);
            }
            _numFinished++;
            return;
        }
    }

    int const lineIndex = _tableLines[p.tableName];
    PrintLine(lineIndex, p);
}

void StandardProgressManager::AllDone()
{
    std::scoped_lock lock(_mutex);

    auto const synchronizedOutput = SynchronizedOutputGuard(_out);

    // Final repaint of summary line to show 100% complete
    if (_hasSummaryLine && _summaryLineAllocated)
    {
        _processedItems = _totalItems; // Ensure we show 100%
        PrintSummaryLine();
    }

    auto const now = std::chrono::steady_clock::now();
    auto const elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - _startTime);
    auto const total_ms = elapsed.count();
    auto const h = total_ms / 3600000;
    auto const m = (total_ms % 3600000) / 60000;
    auto const s = (total_ms % 60000) / 1000;
    auto const ms = total_ms % 1000;

    _out << "\n";

    // Include total items processed if ETA tracking was enabled
    if (_totalItems > 0)
    {
        size_t const processed = _processedItems.load();
        auto const rate = static_cast<double>(processed) / (static_cast<double>(total_ms) / 1000.0);
        _out << std::format(
            "Total time: {:02}:{:02}:{:02}.{:03} | {} rows processed | {:.0f} rows/s\n", h, m, s, ms, processed, rate);
    }
    else
    {
        _out << std::format("Total time: {:02}:{:02}:{:02}.{:03}\n", h, m, s, ms);
    }

    if (_warnings.empty())
        return;

    _out << "\nWarnings:\n";
    for (auto const& w: _warnings)
        _out << std::format("  ⚠️  {}\n", w);
}

void StandardProgressManager::SetMaxTableNameLength(size_t len)
{
    std::scoped_lock lock(_mutex);
    _maxTableNameLength = static_cast<int>(len);
}

void StandardProgressManager::RepaintAllLines()
{
    for (int i = 0; std::cmp_less(i, _nextLineIndex); ++i)
    {
        // Ensure index is within bounds of _lineTableMapping
        if (std::cmp_greater_equal(i, _lineTableMapping.size()))
            continue;

        auto const& tableName = _lineTableMapping[static_cast<size_t>(i)];
        if (_tableProgresses.contains(tableName))
        {
            PrintLine(i, _tableProgresses[tableName]);
        }
    }

    // Also repaint summary line if active
    if (_hasSummaryLine)
    {
        PrintSummaryLine();
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void StandardProgressManager::PrintLine(int lineIndex, SqlBackup::Progress const& p) const
{
    // Calculate how many lines up we need to go
    // _nextLineIndex is the count of table lines allocated.
    // If we have a summary line, add 1 for it.
    // Current cursor is at the bottom (after all lines including summary).
    // To go to lineIndex (0-based from top of our block):
    int const totalLines = _nextLineIndex + (_hasSummaryLine ? 1 : 0);
    int const linesUp = totalLines - lineIndex;

    _out << "\033[" << linesUp << "A"; // Move up
    _out << "\r\033[K";                // Clear line

    // Render progress
    std::string statusIcon;
    std::string statusText;
    bool const hasWarning = _tablesWithWarnings.contains(p.tableName) || p.state == SqlBackup::Progress::State::Warning;

    switch (p.state)
    {
        case SqlBackup::Progress::State::Started:
            statusIcon = "⏳";
            statusText = "Started";
            break;
        case SqlBackup::Progress::State::InProgress:
            statusIcon = "🔄";
            statusText = "Processing";
            break;
        case SqlBackup::Progress::State::Finished:
            statusIcon = "✅";
            statusText = "Done";
            break;
        case SqlBackup::Progress::State::Error:
            statusIcon = "❌";
            statusText = "Error";
            break;
        case SqlBackup::Progress::State::Warning:
            statusIcon = "⚠️ "; // Extra space for alignment?
            statusText = "Warning";
            break;
    }

    if (hasWarning && p.state != SqlBackup::Progress::State::Error)
    {
        statusIcon = "⚠️ ";
    }

    std::string bar;
    double pct = 100;
    if (p.totalRows.has_value() && p.totalRows.value() > 0)
        pct = (static_cast<double>(p.currentRows) * 100.0) / static_cast<double>(p.totalRows.value());

    if (p.state == SqlBackup::Progress::State::Started || p.state == SqlBackup::Progress::State::InProgress
        || p.state == SqlBackup::Progress::State::Finished || hasWarning)
    {
        int const barWidth = 20;
        int const filled = static_cast<int>((pct * barWidth) / 100);

        if (_useUnicode)
        {
            // FiraCode progress bar:
            //   U+EE00 (Left empty)
            //   U+EE01 (Middle empty)
            //   U+EE02 (Right empty)
            //   U+EE03 (Left filled)
            //   U+EE04 (Middle filled)
            //   U+EE05 (Right filled)
            bar += (filled > 0) ? "\uEE03" : "\uEE00";
            for (int i = 0; i < barWidth; ++i)
                bar += (i < filled) ? "\uEE04" : "\uEE01";
            bar += (filled == barWidth) ? "\uEE05 " : "\uEE02 ";
        }
        else
        {
            bar += "[";
            for (int i = 0; i < barWidth; ++i)
                bar += (i < filled) ? "=" : ".";
            bar += "] ";
        }
    }

    auto const load = [&p, pct]() {
        if (p.totalRows.has_value())
            return std::format("{:.2f}% ({}/{})", pct, p.currentRows, p.totalRows.value_or(0));
        else
            return std::format("{} rows", p.currentRows);
    }();

    // Calculate elapsed time for this specific table
    auto const now = std::chrono::steady_clock::now();
    auto const tableStartIt = _tableStartTimes.find(p.tableName);
    auto const tableStart = (tableStartIt != _tableStartTimes.end()) ? tableStartIt->second : _startTime;
    auto const elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - tableStart);
    auto const total_ms = elapsed.count();
    auto const h = total_ms / 3600000;
    auto const m = (total_ms % 3600000) / 60000;
    auto const s = (total_ms % 60000) / 1000;
    auto const ms = total_ms % 1000;
    auto const timeStr = std::format("{:02}:{:02}:{:02}.{:03}", h, m, s, ms);

    // Icon + Time + Name + Bar + Load + Message
    _out << std::format("{} {} {:<{}} {}{} {}", statusIcon, timeStr, p.tableName, _maxTableNameLength, bar, load, p.message);
    _out << "\033[" << linesUp << "B"; // Move down
    _out << "\r";                      // Return carriage
}

void StandardProgressManager::SetTotalItems(size_t totalItems)
{
    std::scoped_lock lock(_mutex);

    _totalItems = totalItems;
    _processedItems = 0;
    _lastRateSampleTime = std::chrono::steady_clock::now();
    _lastRateSampleItems = 0;
    _smoothedRate = 0.0;

    if (_totalItems > 0)
    {
        _hasSummaryLine = true;
        // Note: We don't reserve a line here. The summary line will be printed
        // after all table lines. When the first table arrives, it will add its
        // line, and we'll track the summary position dynamically.
    }
}

void StandardProgressManager::AddTotalItems(size_t additionalItems)
{
    std::scoped_lock lock(_mutex);
    _totalItems += additionalItems;

    if (_totalItems > 0 && !_hasSummaryLine)
    {
        _hasSummaryLine = true;
    }
}

void StandardProgressManager::OnItemsProcessed(size_t count)
{
    size_t const newProcessed = _processedItems.fetch_add(count) + count;

    // Use a scoped_lock to safely update rate calculation and print summary
    std::scoped_lock lock(_mutex);

    // Enable summary line on first processed items (even if total unknown yet)
    if (!_hasSummaryLine)
        _hasSummaryLine = true;

    auto const now = std::chrono::steady_clock::now();
    auto const elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - _lastRateSampleTime).count();

    // Update rate calculation every ~200ms to avoid excessive updates
    if (elapsedMs >= 200)
    {
        size_t const itemsDelta = newProcessed - _lastRateSampleItems;
        double const elapsedSec = static_cast<double>(elapsedMs) / 1000.0;
        double const instantRate = static_cast<double>(itemsDelta) / elapsedSec;

        // Exponential moving average for smooth rate display
        if (_smoothedRate <= 0.0)
        {
            _smoothedRate = instantRate; // Initialize on first sample
        }
        else
        {
            _smoothedRate = (0.3 * instantRate) + (0.7 * _smoothedRate);
        }

        _lastRateSampleTime = now;
        _lastRateSampleItems = newProcessed;

        // Update summary line
        if (_hasSummaryLine)
        {
            auto const synchronizedOutput = SynchronizedOutputGuard(_out);
            PrintSummaryLine();
        }
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void StandardProgressManager::PrintSummaryLine()
{
    if (!_hasSummaryLine || !_summaryLineAllocated)
        return;

    // Summary line is always 1 line above the cursor (at the bottom)
    int const linesUp = 1;

    _out << "\033[" << linesUp << "A"; // Move up
    _out << "\r\033[K";                // Clear line

    size_t const processed = _processedItems.load();
    size_t const total = _totalItems;

    // Calculate percentage: 0% if total unknown, always capped at 99% here
    // We never show 100% in PrintSummaryLine because we can't know if counting is complete.
    // The 100% is only shown in AllDone() when everything is truly finished.
    double pct = 0.0;
    bool const totalKnown = total > 0;

    if (totalKnown)
    {
        // Cap at 99% - workers may be ahead of counter thread, and we can't know
        // when counting is truly complete from here
        pct = std::min(99.0, (static_cast<double>(processed) * 100.0) / static_cast<double>(total));
    }
    // else pct stays at 0.0 (total not yet known)

    // Build progress bar
    std::string bar;
    int const barWidth = 20;
    // Use rounding to match the displayed percentage (which also rounds)
    int const filled = std::min(barWidth, static_cast<int>(std::lround((pct * barWidth) / 100.0)));

    if (_useUnicode)
    {
        bar += (filled > 0) ? "\uEE03" : "\uEE00";
        for (int i = 0; i < barWidth; ++i)
            bar += (i < filled) ? "\uEE04" : "\uEE01";
        bar += (filled == barWidth) ? "\uEE05 " : "\uEE02 ";
    }
    else
    {
        bar += "[";
        for (int i = 0; i < barWidth; ++i)
            bar += (i < filled) ? "=" : ".";
        bar += "] ";
    }

    // Format ETA
    // Note: We never show "done" here - that's only shown in AllDone()
    std::string etaStr;
    if (!totalKnown)
    {
        etaStr = "counting...";
    }
    else if (_smoothedRate > 0.0 && total > processed)
    {
        size_t const remaining = total - processed;
        double const etaSec = static_cast<double>(remaining) / _smoothedRate;

        if (etaSec < 1.0)
        {
            etaStr = "< 1s";
        }
        else if (etaSec < 60.0)
        {
            etaStr = std::format("{:.0f}s", etaSec);
        }
        else if (etaSec < 3600.0)
        {
            int const mins = static_cast<int>(etaSec) / 60;
            int const secs = static_cast<int>(etaSec) % 60;
            etaStr = std::format("{:02}:{:02}", mins, secs);
        }
        else
        {
            int const hours = static_cast<int>(etaSec) / 3600;
            int const mins = (static_cast<int>(etaSec) % 3600) / 60;
            int const secs = static_cast<int>(etaSec) % 60;
            etaStr = std::format("{:02}:{:02}:{:02}", hours, mins, secs);
        }
    }
    else
    {
        etaStr = "calculating...";
    }

    // Format rate
    std::string rateStr;
    if (_smoothedRate > 0.0)
    {
        if (_smoothedRate >= 1000.0)
        {
            rateStr = std::format("{:.1f}k rows/s", _smoothedRate / 1000.0);
        }
        else
        {
            rateStr = std::format("{:.0f} rows/s", _smoothedRate);
        }
    }
    else
    {
        rateStr = "-- rows/s";
    }

    // Calculate elapsed time (same format as table lines)
    auto const now = std::chrono::steady_clock::now();
    auto const elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - _startTime);
    auto const total_ms = elapsed.count();
    auto const h = total_ms / 3600000;
    auto const m = (total_ms % 3600000) / 60000;
    auto const s = (total_ms % 60000) / 1000;
    auto const ms = total_ms % 1000;
    auto const timeStr = std::format("{:02}:{:02}:{:02}.{:03}", h, m, s, ms);

    _out << std::format(
        "📊 {} {:<{}} {}{:5.2f}% ({}, ETA: {})", timeStr, "progress ", _maxTableNameLength, bar, pct, rateStr, etaStr);

    _out << "\033[" << linesUp << "B"; // Move down
    _out << "\r";                      // Return carriage
}

} // namespace Lightweight::Tools
