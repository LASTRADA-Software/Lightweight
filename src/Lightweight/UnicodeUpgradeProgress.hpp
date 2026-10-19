// SPDX-License-Identifier: Apache-2.0
//
// How `MigrationManager::UnicodeUpgradeTables` reports its progress. Internal to the library;
// header-only so the tests can drive it without a database.

#pragma once

#include "ProgressTask.hpp"
#include "SqlConnection.hpp"
#include "SqlError.hpp"
#include "SqlMigration.hpp"
#include "SqlQueryFormatter.hpp"
#include "SqlSchema.hpp"
#include "SqlStatement.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Lightweight::detail
{

/// One table of a Unicode upgrade, as its progress shows it.
struct UnicodeUpgradeTableProgress
{
    std::string name;        ///< Display name of the table.
    std::uint64_t rows = 0;  ///< Its estimated rows; 0 when unknown.
    std::size_t columns = 0; ///< Columns the upgrade alters.
};

/// @brief Reports a Unicode upgrade to a ProgressTask.
///
/// First an indeterminate planning task. Then, unless nothing is to be done or it is a dry run, one
/// overall task whose total is the planned tables' rows (their number when all counts are 0); under it
/// one task per unit, whose total is its tables; under that one task per table of the running unit,
/// whose total is its rows. A table reaches 100 % once its ALTER statements are done, and the overall
/// task then advances by its rows. Tasks end in reverse order of their opening.
class UnicodeUpgradeProgress
{
  public:
    /// Title of the planning task.
    static constexpr std::string_view PlanningTitle = "Plan the Unicode upgrade";
    /// Title of the overall task.
    static constexpr std::string_view OverallTitle = "Upgrade text columns to Unicode";

    /// Opens the planning task under @p parent.
    explicit UnicodeUpgradeProgress(ProgressTask& parent):
        _parent { parent },
        _planning { parent.BeginSubTask(PlanningTitle, 0) }
    {
    }

    UnicodeUpgradeProgress(UnicodeUpgradeProgress const&) = delete;
    UnicodeUpgradeProgress(UnicodeUpgradeProgress&&) = delete;
    UnicodeUpgradeProgress& operator=(UnicodeUpgradeProgress const&) = delete;
    UnicodeUpgradeProgress& operator=(UnicodeUpgradeProgress&&) = delete;

    /// Ends the tasks still open, innermost first.
    ~UnicodeUpgradeProgress()
    {
        EndTables();
        _unit.reset();
        _overall.reset();
        _planning.reset();
    }

    /// Ends planning and, for a real run with work, opens the overall task.
    /// @param units The plan's tables, grouped into units in upgrade order.
    /// @param mode Whether the call only reports its plan.
    void Planned(std::vector<std::vector<UnicodeUpgradeTableProgress>> units, SqlMigration::ExecutionMode mode)
    {
        _planning.reset();
        _units = std::move(units);
        auto tables = std::size_t {};
        auto columns = std::size_t {};
        auto rows = std::uint64_t {};
        for (auto const& unit: _units)
            for (auto const& table: unit)
            {
                ++tables;
                columns += table.columns;
                rows += table.rows;
            }
        if (tables == 0)
        {
            _parent.Log(ProgressTask::LogLevel::Info, "No text columns to upgrade.");
            return;
        }
        if (mode == SqlMigration::ExecutionMode::DryRun)
        {
            _parent.Log(
                ProgressTask::LogLevel::Info,
                std::format("Would upgrade {} column(s) across {} table(s) in {} unit(s).", columns, tables, _units.size()));
            return;
        }
        _weightByRows = rows > 0;
        _overall = _parent.BeginSubTask(OverallTitle, _weightByRows ? rows : tables);
    }

    /// Opens the task of unit @p index and of each of its tables.
    void BeginUnit(std::size_t index)
    {
        _unitIndex = index;
        auto const& unit = _units.at(index);
        _unit = _overall->BeginSubTask(std::format("Unit {} of {}", index + 1, _units.size()), unit.size());
        for (auto const& table: unit)
            _tables.push_back(_unit->BeginSubTask(table.name, table.rows));
    }

    /// Names a step that runs for the whole unit, such as dropping its dependent objects, on each of its tables.
    void UnitStep(std::string_view detail)
    {
        for (auto const& table: _tables)
            table->Advance(0, detail);
    }

    /// The ALTER statements of table @p tableIndex of the running unit start.
    void TableAltering(std::size_t tableIndex)
    {
        _runningTable = tableIndex;
        _tables.at(tableIndex)->Advance(0, std::format("alter {} column(s)", _units.at(_unitIndex).at(tableIndex).columns));
    }

    /// The ALTER statements of table @p tableIndex are done.
    void TableAltered(std::size_t tableIndex)
    {
        auto const& table = _units.at(_unitIndex).at(tableIndex);
        _tables.at(tableIndex)->Advance(table.rows, "done");
        _unit->Advance(1);
        _overall->Advance(_weightByRows ? table.rows : 1);
        _runningTable.reset();
    }

    /// The running unit committed; its tasks end.
    void UnitCommitted()
    {
        _runningTable.reset();
        EndTables();
        _unit.reset();
    }

    /// The upgrade failed. During planning that task is marked failed; afterwards the table whose ALTER
    /// statements run (if any), the running unit and the overall task are.
    /// @param reason What went wrong, UTF-8.
    void Failed(std::string_view reason)
    {
        if (_planning)
        {
            _planning->Fail(reason);
            return;
        }
        if (_runningTable)
            _tables.at(*_runningTable)->Fail(reason);
        if (_unit)
            _unit->Fail(reason);
        if (_overall)
            _overall->Fail(reason);
    }

    /// @return Whether the parent task asked to cancel.
    [[nodiscard]] bool IsCancellationRequested() const
    {
        return _parent.IsCancellationRequested();
    }

  private:
    void EndTables()
    {
        while (!_tables.empty())
            _tables.pop_back();
    }

    ProgressTask& _parent;
    std::unique_ptr<ProgressTask> _planning;
    std::unique_ptr<ProgressTask> _overall;
    std::unique_ptr<ProgressTask> _unit;
    std::vector<std::unique_ptr<ProgressTask>> _tables;
    std::vector<std::vector<UnicodeUpgradeTableProgress>> _units;
    std::size_t _unitIndex = 0;
    std::optional<std::size_t> _runningTable;
    bool _weightByRows = false;
};

/// @brief The rows of @p table for a progress total: the dialect's catalog estimate, else `COUNT(*)`.
/// @return The count, or 0 when it cannot be read (e.g. a login without permission on the catalog
///         view); the table then shows as indeterminate.
[[nodiscard]] inline std::uint64_t RowCountForProgress(SqlConnection& connection,
                                                       SqlSchema::FullyQualifiedTableName const& table)
{
    try
    {
        auto const& formatter = connection.QueryFormatter();
        auto stmt = SqlStatement { connection };
        auto const estimate = formatter.TableRowCountEstimateQuery();
        if (estimate.empty() || table.schema.empty())
        {
            auto const rows = stmt.ExecuteDirectScalar<std::int64_t>(
                std::format("SELECT COUNT(*) FROM {}", formatter.QualifiedTableName(table.schema, table.table)));
            return static_cast<std::uint64_t>(std::max<std::int64_t>(rows.value_or(0), 0));
        }
        stmt.Prepare(estimate);
        auto cursor = stmt.Execute(table.schema, table.table);
        if (!cursor.FetchRow())
            return 0;
        return static_cast<std::uint64_t>(std::max<std::int64_t>(cursor.GetNullableColumn<std::int64_t>(1).value_or(0), 0));
    }
    catch (SqlException const&)
    {
        return 0;
    }
}

} // namespace Lightweight::detail
