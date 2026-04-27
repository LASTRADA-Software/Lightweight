// SPDX-License-Identifier: Apache-2.0

#include "DataBinder/SqlVariant.hpp"
#include "SqlConnection.hpp"
#include "SqlDataDiff.hpp"
#include "SqlServerType.hpp"
#include "SqlStatement.hpp"

#include <chrono>
#include <format>
#include <ranges>
#include <string>

namespace Lightweight::SqlSchema
{

namespace
{

    /// Returns the dialect-appropriate delimiter pair for identifiers.
    [[nodiscard]] std::pair<char, char> IdentifierDelimiters(SqlServerType server) noexcept
    {
        switch (server)
        {
            case SqlServerType::MICROSOFT_SQL:
                return { '[', ']' };
            case SqlServerType::MYSQL:
                return { '`', '`' };
            case SqlServerType::POSTGRESQL:
            case SqlServerType::SQLITE:
            case SqlServerType::UNKNOWN:
                break;
        }
        return { '"', '"' };
    }

    /// Quotes an identifier and escapes embedded delimiter chars by doubling them
    /// (the standard SQL convention; works for all dialects we target).
    [[nodiscard]] std::string Quote(std::string_view identifier, SqlServerType server)
    {
        auto const [open, close] = IdentifierDelimiters(server);
        auto out = std::string {};
        out.reserve(identifier.size() + 2);
        out += open;
        for (auto const c: identifier)
        {
            out += c;
            if (c == close)
                out += close; // escape by doubling
        }
        out += close;
        return out;
    }

    /// Reads one row from the cursor as a vector of string-formatted column values.
    /// Returns std::nullopt at end of result set.
    ///
    /// We fetch each column as `SqlVariant` (which lets the data binder pick the correct
    /// ODBC C type per column) and then format with `SqlVariant::ToString()`. Going via
    /// the typed variant avoids letting the ODBC driver coerce wide numeric columns
    /// (`BIGINT`, large `DECIMAL`, `MONEY`, …) to string itself, which can fail with
    /// `Numeric value out of range` on some drivers (e.g. ODBC Driver 18 for SQL Server).
    [[nodiscard]] std::optional<std::vector<std::string>> FetchRowAsStrings(SqlResultCursor& cursor, std::size_t numColumns)
    {
        if (!cursor.FetchRow())
            return std::nullopt;
        auto values = std::vector<std::string> {};
        values.reserve(numColumns);
        for (auto const i: std::views::iota(std::size_t { 1 }, numColumns + 1))
        {
            auto const v = cursor.GetColumn<SqlVariant>(static_cast<SQLUSMALLINT>(i));
            values.emplace_back(v.IsNull() ? std::string { "(null)" } : v.ToString());
        }
        return values;
    }

    /// Lex-compares two PK tuples (using the leading @c numKeyCols entries of each row).
    [[nodiscard]] int ComparePk(std::vector<std::string> const& a,
                                std::vector<std::string> const& b,
                                std::size_t numKeyCols) noexcept
    {
        for (auto const i: std::views::iota(std::size_t { 0 }, numKeyCols))
        {
            if (a[i] < b[i])
                return -1;
            if (a[i] > b[i])
                return 1;
        }
        return 0;
    }

    /// Extracts the PK columns of a row as a separate vector (for RowDiff::primaryKey).
    [[nodiscard]] std::vector<std::string> ExtractPk(std::vector<std::string> const& row, std::size_t numKeyCols)
    {
        return { row.begin(), row.begin() + static_cast<std::ptrdiff_t>(numKeyCols) };
    }

    /// Returns indexes of columns that should appear after the PK columns in the SELECT list,
    /// so the row layout is `[pk0..pkN-1, nonpk0..nonpkM-1]`. The schema-driven SELECT order
    /// is fixed; this maps each non-PK column index to its name for diff reporting.
    struct ColumnLayout
    {
        std::vector<std::string> orderedColumnNames; ///< In the order used by the SELECT.
        std::size_t numKeyCols = 0;
    };

    [[nodiscard]] ColumnLayout BuildColumnLayout(Table const& tableSchema)
    {
        // Stable column order: PK columns first (in the schema's primaryKeys order),
        // then everything else in the schema's column order.
        auto const& pkSet = tableSchema.primaryKeys;
        auto layout = ColumnLayout {};
        layout.numKeyCols = pkSet.size();
        for (auto const& pk: pkSet)
            layout.orderedColumnNames.push_back(pk);
        for (auto const& col: tableSchema.columns)
        {
            auto const isPk = std::ranges::find(pkSet, col.name) != pkSet.end();
            if (!isPk)
                layout.orderedColumnNames.push_back(col.name);
        }
        return layout;
    }

    /// Builds the SELECT used to scan one side of the diff.
    ///
    /// Column list and PK ordering come from @p layout (which is derived from the side
    /// whose schema we trust as authoritative — see @ref BuildColumnLayout). The schema
    /// label and table name are taken from @p tableOnThisSide so cross-engine pairs
    /// (e.g. `public.X` ↔ `dbo.X`) each get a query that resolves on their own server.
    [[nodiscard]] std::string BuildScanQueryFromLayout(Table const& tableOnThisSide,
                                                       ColumnLayout const& layout,
                                                       SqlServerType server)
    {
        auto const tableRef =
            tableOnThisSide.schema.empty()
                ? Quote(tableOnThisSide.name, server)
                : std::format("{}.{}", Quote(tableOnThisSide.schema, server), Quote(tableOnThisSide.name, server));

        auto cols = std::string {};
        for (auto const& [i, name]: std::views::enumerate(layout.orderedColumnNames))
        {
            if (i != 0)
                cols += ", ";
            cols += Quote(name, server);
        }

        auto orderBy = std::string {};
        // The PK columns are exactly the first `numKeyCols` entries of the layout.
        for (auto const i: std::views::iota(std::size_t { 0 }, layout.numKeyCols))
        {
            if (i != 0)
                orderBy += ", ";
            orderBy += Quote(layout.orderedColumnNames[i], server);
        }

        return std::format("SELECT {} FROM {} ORDER BY {}", cols, tableRef, orderBy);
    }

    [[nodiscard]] std::vector<std::tuple<std::string, std::string, std::string>> DiffRowValues(
        std::vector<std::string> const& a, std::vector<std::string> const& b, ColumnLayout const& layout)
    {
        auto cells = std::vector<std::tuple<std::string, std::string, std::string>> {};
        // Skip PK columns — by definition they match (we paired by them).
        for (auto const i: std::views::iota(layout.numKeyCols, layout.orderedColumnNames.size()))
            if (a[i] != b[i])
                cells.emplace_back(layout.orderedColumnNames[i], a[i], b[i]);
        return cells;
    }

    /// Records a single OnlyInA / OnlyInB row diff. The caller is responsible for
    /// bumping `aRowCount` / `bRowCount` for the side just consumed.
    void RecordOnlyOnOneSide(TableDataDiff& result,
                             std::vector<std::string> const& row,
                             ColumnLayout const& layout,
                             DiffKind side)
    {
        result.rows.emplace_back(RowDiff {
            .kind = side,
            .primaryKey = ExtractPk(row, layout.numKeyCols),
        });
    }

    /// Handles one merge step when both sides still have rows. Returns true if @p rowA
    /// should be advanced, and the second bool when @p rowB should be advanced.
    struct AdvanceFlags
    {
        bool advanceA;
        bool advanceB;
    };

    [[nodiscard]] AdvanceFlags MergeStep(TableDataDiff& result,
                                         std::vector<std::string> const& rowA,
                                         std::vector<std::string> const& rowB,
                                         ColumnLayout const& layout)
    {
        auto const cmp = ComparePk(rowA, rowB, layout.numKeyCols);
        if (cmp == 0)
        {
            auto changes = DiffRowValues(rowA, rowB, layout);
            if (!changes.empty())
            {
                result.rows.emplace_back(RowDiff {
                    .kind = DiffKind::Changed,
                    .primaryKey = ExtractPk(rowA, layout.numKeyCols),
                    .changedCells = std::move(changes),
                });
            }
            ++result.aRowCount;
            ++result.bRowCount;
            return { .advanceA = true, .advanceB = true };
        }
        if (cmp < 0)
        {
            RecordOnlyOnOneSide(result, rowA, layout, DiffKind::OnlyInA);
            ++result.aRowCount;
            return { .advanceA = true, .advanceB = false };
        }
        RecordOnlyOnOneSide(result, rowB, layout, DiffKind::OnlyInB);
        ++result.bRowCount;
        return { .advanceA = false, .advanceB = true };
    }

} // namespace

TableDataDiff DiffTableData(SqlConnection& a,
                            SqlConnection& b,
                            Table const& tableA,
                            Table const& tableB,
                            std::size_t maxRows,
                            DiffProgressCallback onProgress)
{
    auto result = TableDataDiff { .tableName = tableA.name };

    if (tableA.primaryKeys.empty())
    {
        result.skipReason = "no primary key";
        return result;
    }

    auto const layout = BuildColumnLayout(tableA);
    // Each side qualifies the SELECT with its own schema label — postgres uses `public`,
    // MSSQL uses `dbo`, SQLite has none. Cross-engine pairs reach this function with
    // mismatched labels but identical column shape (the schema diff already verified that),
    // so a single column layout is fine for both queries.
    auto const queryA = BuildScanQueryFromLayout(tableA, layout, a.ServerType());
    auto const queryB = BuildScanQueryFromLayout(tableB, layout, b.ServerType());

    auto stmtA = SqlStatement { a };
    auto stmtB = SqlStatement { b };

    auto cursorA = stmtA.ExecuteDirect(queryA);
    auto cursorB = stmtB.ExecuteDirect(queryB);

    auto const numCols = layout.orderedColumnNames.size();
    auto rowA = FetchRowAsStrings(cursorA, numCols);
    auto rowB = FetchRowAsStrings(cursorB, numCols);

    auto lastReport = std::chrono::steady_clock::now();
    auto const reportInterval = std::chrono::milliseconds { 500 };

    auto report = [&](bool force) {
        if (!onProgress)
            return;
        auto const now = std::chrono::steady_clock::now();
        if (!force && now - lastReport < reportInterval)
            return;
        lastReport = now;
        onProgress(DiffProgressEvent {
            .tableName = tableA.name,
            .rowsScannedA = result.aRowCount,
            .rowsScannedB = result.bRowCount,
        });
    };

    while (rowA || rowB)
    {
        if (maxRows != 0 && (result.aRowCount >= maxRows || result.bRowCount >= maxRows))
        {
            result.truncated = true;
            break;
        }

        if (rowA && rowB)
        {
            auto const flags = MergeStep(result, *rowA, *rowB, layout);
            if (flags.advanceA)
                rowA = FetchRowAsStrings(cursorA, numCols);
            if (flags.advanceB)
                rowB = FetchRowAsStrings(cursorB, numCols);
        }
        else if (rowA)
        {
            RecordOnlyOnOneSide(result, *rowA, layout, DiffKind::OnlyInA);
            ++result.aRowCount;
            rowA = FetchRowAsStrings(cursorA, numCols);
        }
        else
        {
            RecordOnlyOnOneSide(result, *rowB, layout, DiffKind::OnlyInB);
            ++result.bRowCount;
            rowB = FetchRowAsStrings(cursorB, numCols);
        }

        report(false);
    }

    report(true);
    return result;
}

} // namespace Lightweight::SqlSchema
