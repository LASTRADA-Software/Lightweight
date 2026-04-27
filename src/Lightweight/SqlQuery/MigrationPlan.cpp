// SPDX-License-Identifier: Apache-2.0

#include "../DataBinder/UnicodeConverter.hpp"
#include "../SqlLogger.hpp"
#include "../SqlQueryFormatter.hpp"
#include "MigrationPlan.hpp"

#include <format>
#include <string_view>

namespace Lightweight
{

namespace
{

    /// @brief Formats a SqlVariant value as a SQL literal string.
    std::string FormatSqlLiteral(SqlQueryFormatter const& formatter, SqlVariant const& value)
    {
        using namespace std::string_literals;

        // clang-format off
        return std::visit(detail::overloaded {
            [&](SqlNullType) { return "NULL"s; },
            [&](SqlGuid const& v) { return formatter.StringLiteral(std::format("{}", v)); },
            [&](bool v) { return std::string(formatter.BooleanLiteral(v)); },
            [&]<std::integral T>(T v) { return std::to_string(v); },
            [&](float v) { return std::format("{}", v); },
            [&](double v) { return std::format("{}", v); },
            [&](std::string_view v) { return formatter.StringLiteral(v); },
            [&](std::u16string_view v) {
                auto u8String = ToUtf8(v);
                return formatter.StringLiteral(std::string_view(reinterpret_cast<char const*>(u8String.data()), u8String.size()));
            },
            [&](std::u16string const& v) {
                auto u8String = ToUtf8(std::u16string_view(v));
                return formatter.StringLiteral(std::string_view(reinterpret_cast<char const*>(u8String.data()), u8String.size()));
            },
            [&](std::string const& v) { return formatter.StringLiteral(v); },
            [&](SqlText const& v) { return formatter.StringLiteral(v.value); },
            [&](SqlDate const& v) { return formatter.StringLiteral(std::format("{}", v)); },
            [&](SqlTime const& v) { return formatter.StringLiteral(std::format("{}", v)); },
            [&](SqlDateTime const& v) { return formatter.StringLiteral(std::format("{}", v)); }
        }, value.value);
        // clang-format on
    }

    /// @brief Formats the table name with optional schema prefix.
    std::string FormatTableName(std::string_view schemaName, std::string_view tableName)
    {
        if (schemaName.empty())
            return std::format(R"("{}")", tableName);
        return std::format(R"("{}"."{}")", schemaName, tableName);
    }

    std::vector<std::string> ToSqlInsert(SqlQueryFormatter const& formatter, SqlInsertDataPlan const& step)
    {
        auto const columns = [&] {
            std::string result;
            for (auto const& [columnName, columnValue]: step.columns)
            {
                if (!result.empty())
                    result += ", ";
                result += std::format(R"("{}")", columnName);
            }
            return result;
        }();

        auto const values = [&] {
            std::string result;
            for (auto const& [columnName, columnValue]: step.columns)
            {
                if (!result.empty())
                    result += ", ";
                result += FormatSqlLiteral(formatter, columnValue);
            }
            return result;
        }();

        auto tableName = FormatTableName(step.schemaName, step.tableName);
        return { std::format("INSERT INTO {} ({}) VALUES ({})", tableName, columns, values) };
    }

    std::vector<std::string> ToSqlUpdate(SqlQueryFormatter const& formatter, SqlUpdateDataPlan const& step)
    {
        auto const setClause = [&] {
            std::string result;
            for (auto const& [columnName, columnValue]: step.setColumns)
            {
                if (!result.empty())
                    result += ", ";
                result += std::format(R"("{}" = {})", columnName, FormatSqlLiteral(formatter, columnValue));
            }
            // Raw expression assignments (column-to-column copies, arithmetic, etc.) are
            // appended verbatim — the lup2dbtool emitter has already canonicalized
            // identifier quoting in the expression string.
            for (auto const& [columnName, expression]: step.setExpressions)
            {
                if (!result.empty())
                    result += ", ";
                result += std::format(R"("{}" = {})", columnName, expression);
            }
            return result;
        }();

        auto tableName = FormatTableName(step.schemaName, step.tableName);
        std::string sql = std::format("UPDATE {} SET {}", tableName, setClause);
        if (!step.whereExpression.empty())
        {
            // Pre-rendered composite condition; emit verbatim (the lup2dbtool parser
            // has already validated shape and canonicalized quoting).
            sql += std::format(" WHERE {}", step.whereExpression);
        }
        else if (!step.whereColumn.empty())
        {
            sql += std::format(
                R"( WHERE "{}" {} {})", step.whereColumn, step.whereOp, FormatSqlLiteral(formatter, step.whereValue));
        }
        return { std::move(sql) };
    }

    std::vector<std::string> ToSqlDelete(SqlQueryFormatter const& formatter, SqlDeleteDataPlan const& step)
    {
        auto tableName = FormatTableName(step.schemaName, step.tableName);
        std::string sql = std::format("DELETE FROM {}", tableName);
        if (!step.whereExpression.empty())
        {
            sql += std::format(" WHERE {}", step.whereExpression);
        }
        else if (!step.whereColumn.empty())
        {
            sql += std::format(
                R"( WHERE "{}" {} {})", step.whereColumn, step.whereOp, FormatSqlLiteral(formatter, step.whereValue));
        }
        return { std::move(sql) };
    }

    std::vector<std::string> ToSqlCreateIndex(SqlCreateIndexPlan const& step)
    {
        auto const columns = [&] {
            std::string result;
            for (auto const& col: step.columns)
            {
                if (!result.empty())
                    result += ", ";
                result += std::format(R"("{}")", col);
            }
            return result;
        }();

        auto tableName = FormatTableName(step.schemaName, step.tableName);
        std::string_view const uniqueStr = step.unique ? "UNIQUE " : "";
        std::string_view const ifNotExistsStr = step.ifNotExists ? "IF NOT EXISTS " : "";
        std::string indexName = step.schemaName.empty() ? std::format(R"("{}")", step.indexName)
                                                        : std::format(R"("{}"."{}")", step.schemaName, step.indexName);

        return { std::format("CREATE {}INDEX {}{} ON {} ({})", uniqueStr, ifNotExistsStr, indexName, tableName, columns) };
    }

    /// @brief Extracts the declared character width from a char/varchar-family type
    /// definition, or zero for non-character types (where truncation doesn't apply).
    /// `Char`/`Varchar` are treated as byte-counted (matches MSSQL `varchar` semantics),
    /// while `NChar`/`NVarchar` are character-counted. PostgreSQL/SQLite both ignore the
    /// distinction at runtime, so the conservative byte interpretation is harmless there.
    MigrationRenderContext::ColumnWidth DeclaredCharWidth(SqlColumnTypeDefinition const& type)
    {
        using Unit = MigrationRenderContext::WidthUnit;
        using W = MigrationRenderContext::ColumnWidth;
        return std::visit(detail::overloaded {
            [](SqlColumnTypeDefinitions::Char const& t) -> W { return { .value = t.size, .unit = Unit::Bytes }; },
            [](SqlColumnTypeDefinitions::Varchar const& t) -> W { return { .value = t.size, .unit = Unit::Bytes }; },
            [](SqlColumnTypeDefinitions::NChar const& t) -> W { return { .value = t.size, .unit = Unit::Characters }; },
            [](SqlColumnTypeDefinitions::NVarchar const& t) -> W {
                return { .value = t.size, .unit = Unit::Characters };
            },
            [](auto const&) -> W { return { .value = 0, .unit = Unit::Characters }; },
        }, type);
    }

    /// @brief Builds a `ColumnKey` from its parts; accepts `string_view` column names so
    /// callers can pass `SqlAlterTableCommands::DropColumn::columnName` (which is a view)
    /// without an extra copy at the call site.
    MigrationRenderContext::ColumnKey MakeColumnKey(std::string_view schema,
                                                     std::string_view table,
                                                     std::string_view column)
    {
        return MigrationRenderContext::ColumnKey { .schema = std::string(schema),
                                                    .table = std::string(table),
                                                    .column = std::string(column) };
    }

    /// @brief Populates the render context's column-width cache from a `CreateTable` step.
    void RememberColumnWidths(MigrationRenderContext& context, SqlCreateTablePlan const& step)
    {
        for (auto const& col: step.columns)
        {
            auto const width = DeclaredCharWidth(col.type);
            if (width.value == 0)
                continue;
            context.columnWidths[MakeColumnKey(step.schemaName, step.tableName, col.name)] = width;
        }
    }

    /// @brief Reacts to an `ALTER TABLE` step by updating the width cache for the columns
    /// it touches. Only add-column and alter-column commands carry a type definition.
    void RememberColumnWidths(MigrationRenderContext& context, SqlAlterTablePlan const& step)
    {
        for (auto const& cmd: step.commands)
        {
            std::visit(detail::overloaded {
                [&](SqlAlterTableCommands::AddColumn const& c) {
                    auto const width = DeclaredCharWidth(c.columnType);
                    if (width.value > 0)
                        context.columnWidths[MakeColumnKey(step.schemaName, step.tableName, c.columnName)] = width;
                },
                [&](SqlAlterTableCommands::AlterColumn const& c) {
                    auto const width = DeclaredCharWidth(c.columnType);
                    if (width.value > 0)
                        context.columnWidths[MakeColumnKey(step.schemaName, step.tableName, c.columnName)] = width;
                },
                [&](SqlAlterTableCommands::DropColumn const& c) {
                    context.columnWidths.erase(MakeColumnKey(step.schemaName, step.tableName, c.columnName));
                },
                [](auto const&) {}, // other commands don't change char widths we track
            }, cmd);
        }
    }

    /// @brief Forgets every cached column of `table` — used on `DROP TABLE`.
    void ForgetTableWidths(MigrationRenderContext& context,
                           std::string_view schemaName,
                           std::string_view tableName)
    {
        auto const lo = MakeColumnKey(schemaName, tableName, {});
        auto it = context.columnWidths.lower_bound(lo);
        while (it != context.columnWidths.end()
               && it->first.schema == schemaName
               && it->first.table == tableName)
        {
            it = context.columnWidths.erase(it);
        }
    }

    /// @brief Decodes the byte length of the UTF-8 sequence whose lead byte is `c`.
    /// A malformed lead byte (continuation byte appearing where a lead is expected) is
    /// treated as a single-byte sequence so the decoder makes forward progress.
    constexpr std::size_t Utf8SequenceLength(unsigned char c) noexcept
    {
        if (c < 0x80)
            return 1;
        if (c < 0xC0)
            return 1; // stray continuation byte: treat as single-byte so we advance
        if (c < 0xE0)
            return 2;
        if (c < 0xF0)
            return 3;
        return 4;
    }

    /// @brief Truncates a character string view so its representation respects a
    /// declared budget. In `Characters` mode the budget counts UTF-8 codepoints; in
    /// `Bytes` mode it counts UTF-8 bytes (and never splits a multi-byte sequence —
    /// we always include or exclude a codepoint whole). Returns the truncated view
    /// (zero-copy — the caller owns the backing storage).
    std::string_view TruncateUtf8(std::string_view s,
                                   std::size_t budget,
                                   MigrationRenderContext::WidthUnit unit)
    {
        if (unit == MigrationRenderContext::WidthUnit::Bytes && s.size() <= budget)
            return s;

        std::size_t chars = 0;
        std::size_t i = 0;
        while (i < s.size())
        {
            auto const len = Utf8SequenceLength(static_cast<unsigned char>(s[i]));
            if (i + len > s.size())
                break; // malformed tail; stop here
            if (unit == MigrationRenderContext::WidthUnit::Characters && chars >= budget)
                break;
            if (unit == MigrationRenderContext::WidthUnit::Bytes && i + len > budget)
                break;
            i += len;
            ++chars;
        }
        return s.substr(0, i);
    }

    /// @brief Truncates a string-valued `SqlVariant` to fit `width`.
    /// Returns true if truncation happened, and sets `originalSize` to the input length.
    /// Handles all string flavours `SqlVariant` can hold: `std::string`, `std::string_view`,
    /// `std::u16string`, `std::u16string_view`, and `SqlText`. String-view inputs are
    /// materialised into owning `std::string` / `std::u16string` on truncation so the
    /// variant no longer references caller-owned storage. Non-string variants are left
    /// untouched.
    ///
    /// UTF-8 strings respect `width.unit` — `Bytes` for byte-counted columns
    /// (`varchar`/`char` on MSSQL) so multi-byte source data stays within the server's
    /// budget. UTF-16 strings always count code units (`std::u16string::size`), which
    /// matches `nvarchar`/`nchar` semantics.
    bool TruncateIfOversize(SqlVariant& value,
                             MigrationRenderContext::ColumnWidth const width,
                             std::size_t& originalSize)
    {
        originalSize = 0;

        // Mutating the variant's active alternative from inside `std::visit` would
        // invalidate the reference the lambda holds, so for the `_view` alternatives
        // (which must be promoted to owning strings on truncation) we read first, then
        // reassign outside the visit.
        if (auto* v = std::get_if<std::string>(&value.value))
        {
            auto const truncated = TruncateUtf8(*v, width.value, width.unit);
            if (truncated.size() == v->size())
                return false;
            originalSize = v->size();
            v->resize(truncated.size());
            return true;
        }
        if (auto* v = std::get_if<std::string_view>(&value.value))
        {
            auto const truncated = TruncateUtf8(*v, width.value, width.unit);
            if (truncated.size() == v->size())
                return false;
            originalSize = v->size();
            auto owned = std::string(truncated);
            value.value = std::move(owned);
            return true;
        }
        if (auto* v = std::get_if<std::u16string>(&value.value))
        {
            if (v->size() <= width.value)
                return false;
            originalSize = v->size();
            v->resize(width.value);
            return true;
        }
        if (auto* v = std::get_if<std::u16string_view>(&value.value))
        {
            if (v->size() <= width.value)
                return false;
            originalSize = v->size();
            auto owned = std::u16string(v->substr(0, width.value));
            value.value = std::move(owned);
            return true;
        }
        if (auto* v = std::get_if<SqlText>(&value.value))
        {
            auto const truncated = TruncateUtf8(v->value, width.value, width.unit);
            if (truncated.size() == v->value.size())
                return false;
            originalSize = v->value.size();
            v->value.resize(truncated.size());
            return true;
        }
        return false;
    }

    /// @brief Records a single column-level truncation discovered while preparing
    /// an INSERT / UPDATE plan. Decoupled from logging so the warning can be
    /// emitted after the SQL has been rendered, with the rendered statement
    /// attached for investigation.
    struct LupTruncationEvent
    {
        std::string column;
        std::size_t originalSize = 0;
        std::size_t declaredWidth = 0;
        std::string_view unit;
    };

    /// @brief Applies `lup-truncate` to an INSERT / UPDATE plan's `(column, SqlVariant)`
    /// pairs. Returns one event per truncated column. Logging is deferred to the
    /// caller so the warning can include the rendered SQL.
    ///
    /// @param context Render context — mutable so the lazy `widthLookup` callback can
    /// populate cache entries on first miss for tables not declared by the current run.
    [[nodiscard]] std::vector<LupTruncationEvent> ApplyLupTruncate(
        MigrationRenderContext& context,
        std::string const& schemaName,
        std::string const& tableName,
        std::vector<std::pair<std::string, SqlVariant>>& columns)
    {
        auto events = std::vector<LupTruncationEvent> {};

        // Populate the cache from the live DB if this table hasn't been seen yet — covers
        // pre-existing tables (created by an earlier run) that no `CreateTable` step in
        // this run would otherwise teach us about.
        auto const tableKey = MigrationRenderContext::TableKey { .schema = schemaName, .table = tableName };
        if (context.widthLookup && !context.lookupAttempted.contains(tableKey))
        {
            context.lookupAttempted.insert(tableKey);
            context.widthLookup(context, schemaName, tableName);
        }

        for (auto& [columnName, value]: columns)
        {
            auto const it = context.columnWidths.find({ schemaName, tableName, columnName });
            if (it == context.columnWidths.end())
                continue;
            std::size_t originalSize = 0;
            if (TruncateIfOversize(value, it->second, originalSize))
            {
                events.push_back(LupTruncationEvent {
                    .column = columnName,
                    .originalSize = originalSize,
                    .declaredWidth = it->second.value,
                    .unit = it->second.unit == MigrationRenderContext::WidthUnit::Bytes ? "bytes" : "chars",
                });
            }
        }
        return events;
    }

    /// @brief Emits one `OnWarning` entry per truncation event. The warning carries
    /// the migration identity (so the user can locate the source migration) and the
    /// rendered SQL statements (so they can see exactly what was sent after clipping).
    void LogLupTruncationEvents(MigrationRenderContext const& context,
                                std::string_view operation,
                                std::string const& schemaName,
                                std::string const& tableName,
                                std::vector<LupTruncationEvent> const& events,
                                std::vector<std::string> const& renderedStatements)
    {
        if (events.empty())
            return;

        auto const migrationLabel = context.activeMigrationTimestamp != 0
            ? std::format("{} '{}'", context.activeMigrationTimestamp, context.activeMigrationTitle)
            : std::string { "<unknown>" };

        auto joinedSql = std::string {};
        for (auto const& sql: renderedStatements)
        {
            if (!joinedSql.empty())
                joinedSql += "; ";
            joinedSql += sql;
        }

        for (auto const& ev: events)
        {
            SqlLogger::GetLogger().OnWarning(std::format(
                "lup-truncate: migration {}: {} {}.{}.{}: value of size {} exceeded declared width {} {} "
                "— clipped; statement: {}",
                migrationLabel,
                operation,
                schemaName.empty() ? "<default>" : schemaName.c_str(),
                tableName,
                ev.column,
                ev.originalSize,
                ev.declaredWidth,
                ev.unit,
                joinedSql));
        }
    }

    /// @brief Dispatches a single plan element to the correct SQL-emitting helper. Shared
    /// between the context-less and context-aware `ToSql` overloads.
    std::vector<std::string> RenderStep(SqlQueryFormatter const& formatter,
                                         SqlMigrationPlanElement const& element)
    {
        return std::visit(
            [&](auto const& step) -> std::vector<std::string> {
                using T = std::decay_t<decltype(step)>;
                if constexpr (std::is_same_v<T, SqlCreateTablePlan>)
                    return formatter.CreateTable(
                        step.schemaName, step.tableName, step.columns, step.foreignKeys, step.ifNotExists);
                else if constexpr (std::is_same_v<T, SqlAlterTablePlan>)
                    return formatter.AlterTable(step.schemaName, step.tableName, step.commands);
                else if constexpr (std::is_same_v<T, SqlDropTablePlan>)
                    return formatter.DropTable(step.schemaName, step.tableName, step.ifExists, step.cascade);
                else if constexpr (std::is_same_v<T, SqlCreateIndexPlan>)
                    return ToSqlCreateIndex(step);
                else if constexpr (std::is_same_v<T, SqlRawSqlPlan>)
                    return { std::string(step.sql) };
                else if constexpr (std::is_same_v<T, SqlInsertDataPlan>)
                    return ToSqlInsert(formatter, step);
                else if constexpr (std::is_same_v<T, SqlUpdateDataPlan>)
                    return ToSqlUpdate(formatter, step);
                else if constexpr (std::is_same_v<T, SqlDeleteDataPlan>)
                    return ToSqlDelete(formatter, step);
                else
                    static_assert(detail::AlwaysFalse<T>, "non-exhaustive visitor");
            },
            element);
    }

} // namespace

std::vector<std::string> SqlMigrationPlan::ToSql() const
{
    std::vector<std::string> result;
    for (auto const& step: steps)
    {
        auto sql = Lightweight::ToSql(formatter, step);
        result.insert(result.end(), sql.begin(), sql.end());
    }
    return result;
}

std::vector<std::string> ToSql(SqlQueryFormatter const& formatter, SqlMigrationPlanElement const& element)
{
    return RenderStep(formatter, element);
}

std::vector<std::string> ToSql(SqlQueryFormatter const& formatter,
                                SqlMigrationPlanElement const& element,
                                MigrationRenderContext& context)
{
    // First: consume schema-affecting steps to keep the width cache current, so INSERT/
    // UPDATE steps that follow within the same migration plan see the column widths the
    // same CREATE/ALTER declared.
    std::visit(detail::overloaded {
        [&](SqlCreateTablePlan const& step) { RememberColumnWidths(context, step); },
        [&](SqlAlterTablePlan const& step) { RememberColumnWidths(context, step); },
        [&](SqlDropTablePlan const& step) { ForgetTableWidths(context, step.schemaName, step.tableName); },
        [](auto const&) {},
    }, element);

    // Then, for value-carrying steps, apply the active compat knobs. We mutate a local
    // copy when truncation is needed so the caller's plan stays observationally const.
    if (context.lupTruncate)
    {
        if (auto const* ins = std::get_if<SqlInsertDataPlan>(&element); ins && !ins->columns.empty())
        {
            SqlInsertDataPlan mutated = *ins;
            auto const events = ApplyLupTruncate(context, mutated.schemaName, mutated.tableName, mutated.columns);
            auto sql = ToSqlInsert(formatter, mutated);
            LogLupTruncationEvents(context, "INSERT", mutated.schemaName, mutated.tableName, events, sql);
            return sql;
        }
        if (auto const* upd = std::get_if<SqlUpdateDataPlan>(&element); upd && !upd->setColumns.empty())
        {
            SqlUpdateDataPlan mutated = *upd;
            auto const events = ApplyLupTruncate(context, mutated.schemaName, mutated.tableName, mutated.setColumns);
            auto sql = ToSqlUpdate(formatter, mutated);
            LogLupTruncationEvents(context, "UPDATE", mutated.schemaName, mutated.tableName, events, sql);
            return sql;
        }
    }

    return RenderStep(formatter, element);
}

std::vector<std::string> ToSql(std::vector<SqlMigrationPlan> const& plans)
{
    std::vector<std::string> result;

    for (auto const& plan: plans)
    {
        for (auto const& step: plan.steps)
        {
            auto sql = ToSql(plan.formatter, step);
            result.insert(result.end(), sql.begin(), sql.end());
        }
    }

    return result;
}

} // namespace Lightweight
