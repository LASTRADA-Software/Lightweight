// SPDX-License-Identifier: Apache-2.0

#include "../DataBinder/UnicodeConverter.hpp"
#include "../SqlLogger.hpp"
#include "../SqlQueryFormatter.hpp"
#include "MigrationPlan.hpp"

#include <algorithm>
#include <format>
#include <optional>
#include <ranges>
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
            // An exact decimal is emitted unquoted: quoting it would make the backend parse a string
            // back into a number, reintroducing the rounding this type exists to avoid.
            [&](SqlDynamicNumeric const& v) { return v.ToString(); },
            [&](SqlBinary const& v) { return formatter.BinaryLiteral(v); },
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

    /// @brief Renders the trailing ` WHERE …` for an UPDATE/DELETE plan, or empty
    /// when neither a raw expression nor a structured `(column, op, value)` triple
    /// has been supplied.
    ///
    /// `whereExpression` (a pre-rendered clause body) takes precedence; the structured
    /// form is the fallback for the simple `Where(col, op, value)` builder API.
    std::string FormatWhereClause(SqlQueryFormatter const& formatter,
                                  std::string_view whereExpression,
                                  std::string_view whereColumn,
                                  std::string_view whereOp,
                                  SqlVariant const& whereValue)
    {
        if (!whereExpression.empty())
            return std::format(" WHERE {}", whereExpression);
        if (!whereColumn.empty())
            return std::format(R"( WHERE "{}" {} {})", whereColumn, whereOp, FormatSqlLiteral(formatter, whereValue));
        return {};
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
        sql += FormatWhereClause(formatter, step.whereExpression, step.whereColumn, step.whereOp, step.whereValue);
        return { std::move(sql) };
    }

    std::vector<std::string> ToSqlDelete(SqlQueryFormatter const& formatter, SqlDeleteDataPlan const& step)
    {
        auto tableName = FormatTableName(step.schemaName, step.tableName);
        std::string sql = std::format("DELETE FROM {}", tableName);
        sql += FormatWhereClause(formatter, step.whereExpression, step.whereColumn, step.whereOp, step.whereValue);
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

        auto const whereClause =
            step.whereExpression.empty() ? std::string {} : std::format(" WHERE {}", step.whereExpression);

        return { std::format(
            "CREATE {}INDEX {}{} ON {} ({}){}", uniqueStr, ifNotExistsStr, indexName, tableName, columns, whereClause) };
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
        return std::visit(
            detail::overloaded {
                [](SqlColumnTypeDefinitions::Char const& t) -> W { return { .value = t.size, .unit = Unit::Bytes }; },
                [](SqlColumnTypeDefinitions::Varchar const& t) -> W { return { .value = t.size, .unit = Unit::Bytes }; },
                [](SqlColumnTypeDefinitions::NChar const& t) -> W { return { .value = t.size, .unit = Unit::Characters }; },
                [](SqlColumnTypeDefinitions::NVarchar const& t) -> W {
                    return { .value = t.size, .unit = Unit::Characters };
                },
                [](auto const&) -> W { return { .value = 0, .unit = Unit::Characters }; },
            },
            type);
    }

    /// @brief Builds a `ColumnKey` from its parts; accepts `string_view` column names so
    /// callers can pass `SqlAlterTableCommands::DropColumn::columnName` (which is a view)
    /// without an extra copy at the call site.
    MigrationRenderContext::ColumnKey MakeColumnKey(std::string_view schema, std::string_view table, std::string_view column)
    {
        return MigrationRenderContext::ColumnKey { .schema = std::string(schema),
                                                   .table = std::string(table),
                                                   .column = std::string(column) };
    }

    /// @brief True for the text types whose narrow/wide kind `fkMatchReferencedText` tracks.
    bool IsTrackedTextType(SqlColumnTypeDefinition const& type) noexcept
    {
        return std::holds_alternative<SqlColumnTypeDefinitions::Char>(type)
               || std::holds_alternative<SqlColumnTypeDefinitions::NChar>(type)
               || std::holds_alternative<SqlColumnTypeDefinitions::Varchar>(type)
               || std::holds_alternative<SqlColumnTypeDefinitions::NVarchar>(type);
    }

    /// @brief Records what the render context needs to know about one declared column:
    /// its character width (for `lup-truncate`) and its text type (for `fkMatchReferencedText`).
    void RememberColumn(MigrationRenderContext& context,
                        MigrationRenderContext::ColumnKey const& key,
                        SqlColumnTypeDefinition const& type)
    {
        if (auto const width = DeclaredCharWidth(type); width.value > 0)
            context.columnWidths[key] = width;
        if (IsTrackedTextType(type))
            context.textColumnTypes[key] = type;
        else
            context.textColumnTypes.erase(key);
    }

    /// @brief Populates the render context's column caches from a `CreateTable` step.
    void RememberColumnWidths(MigrationRenderContext& context, SqlCreateTablePlan const& step)
    {
        for (auto const& col: step.columns)
            RememberColumn(context, MakeColumnKey(step.schemaName, step.tableName, col.name), col.type);
    }

    /// @brief Reacts to an `ALTER TABLE` step by updating the column caches for the columns
    /// it touches. Only add-column and alter-column commands carry a type definition.
    void RememberColumnWidths(MigrationRenderContext& context, SqlAlterTablePlan const& step)
    {
        for (auto const& cmd: step.commands)
        {
            std::visit(
                detail::overloaded {
                    [&](SqlAlterTableCommands::AddColumn const& c) {
                        RememberColumn(context, MakeColumnKey(step.schemaName, step.tableName, c.columnName), c.columnType);
                    },
                    [&](SqlAlterTableCommands::AlterColumn const& c) {
                        RememberColumn(context, MakeColumnKey(step.schemaName, step.tableName, c.columnName), c.columnType);
                    },
                    [&](SqlAlterTableCommands::DropColumn const& c) {
                        auto const key = MakeColumnKey(step.schemaName, step.tableName, c.columnName);
                        context.columnWidths.erase(key);
                        context.textColumnTypes.erase(key);
                    },
                    [](auto const&) {}, // other commands don't change the column types we track
                },
                cmd);
        }
    }

    /// @brief Erases every entry of `table` from a `ColumnKey`-ordered cache.
    template <typename Map>
    void ForgetTableColumns(Map& cache, std::string_view schemaName, std::string_view tableName)
    {
        auto it = cache.lower_bound(MakeColumnKey(schemaName, tableName, {}));
        while (it != cache.end() && it->first.schema == schemaName && it->first.table == tableName)
            it = cache.erase(it);
    }

    /// @brief Forgets every cached column of `table` — used on `DROP TABLE`.
    void ForgetTableWidths(MigrationRenderContext& context, std::string_view schemaName, std::string_view tableName)
    {
        ForgetTableColumns(context.columnWidths, schemaName, tableName);
        ForgetTableColumns(context.textColumnTypes, schemaName, tableName);
    }

    /// @brief The narrow/wide counterpart of `declared` that `referenced` requires, if the
    /// two differ only in that respect: `CHAR(n)`<->`NCHAR(n)` or `VARCHAR(n)`<->`NVARCHAR(n)`.
    /// Anything else — other types, different lengths, already matching — yields nullopt,
    /// so a genuine mismatch still reaches the server and fails loudly.
    std::optional<SqlColumnTypeDefinition> NarrowWideCounterpart(SqlColumnTypeDefinition const& declared,
                                                                 SqlColumnTypeDefinition const& referenced)
    {
        using namespace SqlColumnTypeDefinitions;
        auto const sameSize = [](auto const& a, auto const& b) {
            return a.size == b.size;
        };
        if (auto const* d = std::get_if<NChar>(&declared); d)
            if (auto const* r = std::get_if<Char>(&referenced); r && sameSize(*d, *r))
                return referenced;
        if (auto const* d = std::get_if<Char>(&declared); d)
            if (auto const* r = std::get_if<NChar>(&referenced); r && sameSize(*d, *r))
                return referenced;
        if (auto const* d = std::get_if<NVarchar>(&declared); d)
            if (auto const* r = std::get_if<Varchar>(&referenced); r && sameSize(*d, *r))
                return referenced;
        if (auto const* d = std::get_if<Varchar>(&declared); d)
            if (auto const* r = std::get_if<NVarchar>(&referenced); r && sameSize(*d, *r))
                return referenced;
        return std::nullopt;
    }

    /// @brief Looks up the referenced column's text type: from columns rendered earlier in
    /// this run, else via the live-schema `widthLookup` (queried once per table).
    std::optional<SqlColumnTypeDefinition> ReferencedTextType(MigrationRenderContext& context,
                                                              std::string_view schemaName,
                                                              std::string_view tableName,
                                                              std::string_view columnName)
    {
        auto const key = MakeColumnKey(schemaName, tableName, columnName);
        auto const tableKey =
            MigrationRenderContext::TableKey { .schema = std::string(schemaName), .table = std::string(tableName) };
        if (!context.textColumnTypes.contains(key) && context.widthLookup && !context.lookupAttempted.contains(tableKey))
        {
            context.lookupAttempted.insert(tableKey);
            context.widthLookup(context, schemaName, tableName);
        }
        if (auto const it = context.textColumnTypes.find(key); it != context.textColumnTypes.end())
            return it->second;
        return std::nullopt;
    }

    /// @brief One foreign-key column to reconcile with the column it references.
    struct ForeignKeyColumnRef
    {
        std::string_view schemaName;       ///< Schema of the referencing (and referenced) table.
        std::string_view tableName;        ///< Referencing table.
        std::string_view columnName;       ///< Referencing column.
        std::string_view referencedTable;  ///< Referenced table.
        std::string_view referencedColumn; ///< Referenced column.
    };

    /// @brief Gives `type` (declared for `ref.columnName`) the referenced column's text kind
    /// when they differ only in narrow vs. wide, logging the change.
    void MatchReferencedText(SqlQueryFormatter const& formatter,
                             MigrationRenderContext& context,
                             ForeignKeyColumnRef const& ref,
                             SqlColumnTypeDefinition& type)
    {
        auto const referencedType = ReferencedTextType(context, ref.schemaName, ref.referencedTable, ref.referencedColumn);
        if (!referencedType)
            return;
        auto const counterpart = NarrowWideCounterpart(type, *referencedType);
        if (!counterpart)
            return;

        SqlLogger::GetLogger().OnWarning(
            std::format("{}: migration {} ({}): {}.{} is declared {} but references {}.{} of type {}; creating it as {}",
                        CompatFlagFkMatchReferencedTextName,
                        context.activeMigrationTimestamp,
                        context.activeMigrationTitle,
                        ref.tableName,
                        ref.columnName,
                        formatter.ColumnType(type),
                        ref.referencedTable,
                        ref.referencedColumn,
                        formatter.ColumnType(*referencedType),
                        formatter.ColumnType(*counterpart)));
        type = *counterpart;
    }

    /// @brief `CreateTable`: adapts FK columns declared with an inline reference or in a
    /// composite `ForeignKey({...}, table, {...})` constraint.
    void MatchReferencedText(SqlQueryFormatter const& formatter, MigrationRenderContext& context, SqlCreateTablePlan& step)
    {
        for (auto& column: step.columns)
            if (column.foreignKey)
                MatchReferencedText(formatter,
                                    context,
                                    { .schemaName = step.schemaName,
                                      .tableName = step.tableName,
                                      .columnName = column.name,
                                      .referencedTable = column.foreignKey->tableName,
                                      .referencedColumn = column.foreignKey->columnName },
                                    column.type);

        for (auto const& fk: step.foreignKeys)
            for (auto const& [columnName, referencedColumn]: std::views::zip(fk.columns, fk.referencedColumns))
                if (auto const it = std::ranges::find(step.columns, columnName, &SqlColumnDeclaration::name);
                    it != step.columns.end())
                    MatchReferencedText(formatter,
                                        context,
                                        { .schemaName = step.schemaName,
                                          .tableName = step.tableName,
                                          .columnName = it->name,
                                          .referencedTable = fk.referencedTableName,
                                          .referencedColumn = referencedColumn },
                                        it->type);
    }

    /// @brief `AlterTable`: adapts columns added by this same step that a foreign key in the
    /// step references. Columns that already exist in the database are left alone — changing
    /// them would need an `ALTER COLUMN` (see `MigrationManager::UnicodeUpgradeTables`).
    void MatchReferencedText(SqlQueryFormatter const& formatter, MigrationRenderContext& context, SqlAlterTablePlan& step)
    {
        auto const adapt =
            [&](std::string_view columnName, std::string_view referencedTable, std::string_view referencedColumn) {
                for (auto& command: step.commands)
                    if (auto* add = std::get_if<SqlAlterTableCommands::AddColumn>(&command);
                        add && add->columnName == columnName)
                        MatchReferencedText(formatter,
                                            context,
                                            { .schemaName = step.schemaName,
                                              .tableName = step.tableName,
                                              .columnName = add->columnName,
                                              .referencedTable = referencedTable,
                                              .referencedColumn = referencedColumn },
                                            add->columnType);
            };
        for (auto const& command: step.commands)
        {
            if (auto const* fk = std::get_if<SqlAlterTableCommands::AddForeignKey>(&command))
                adapt(fk->columnName, fk->referencedColumn.tableName, fk->referencedColumn.columnName);
            else if (auto const* composite = std::get_if<SqlAlterTableCommands::AddCompositeForeignKey>(&command))
                for (auto const& [columnName, referencedColumn]:
                     std::views::zip(composite->columns, composite->referencedColumns))
                    adapt(columnName, composite->referencedTableName, referencedColumn);
        }
    }

    /// @brief Applies `fkMatchReferencedText` to a schema step. Returns the step to render
    /// instead of `element` — an adapted copy — or nullopt when nothing applies.
    std::optional<SqlMigrationPlanElement> AdaptForeignKeyTextTypes(SqlQueryFormatter const& formatter,
                                                                    MigrationRenderContext& context,
                                                                    SqlMigrationPlanElement const& element)
    {
        if (!context.fkMatchReferencedText || !formatter.DistinguishesNarrowAndWideText())
            return std::nullopt;
        if (auto const* create = std::get_if<SqlCreateTablePlan>(&element))
        {
            auto copy = *create;
            MatchReferencedText(formatter, context, copy);
            return copy;
        }
        if (auto const* alter = std::get_if<SqlAlterTablePlan>(&element))
        {
            auto copy = *alter;
            MatchReferencedText(formatter, context, copy);
            return copy;
        }
        return std::nullopt;
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
    std::string_view TruncateUtf8(std::string_view s, std::size_t budget, MigrationRenderContext::WidthUnit unit)
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
    bool TruncateIfOversize(SqlVariant& value, MigrationRenderContext::ColumnWidth const width, std::size_t& originalSize)
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
    [[nodiscard]] std::vector<LupTruncationEvent> ApplyLupTruncate(MigrationRenderContext& context,
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
            auto const it = context.columnWidths.find(
                MigrationRenderContext::ColumnKey { .schema = schemaName, .table = tableName, .column = columnName });
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

        auto const migrationLabel =
            context.activeMigrationTimestamp != 0
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
            SqlLogger::GetLogger().OnWarning(
                std::format("lup-truncate: migration {}: {} {}.{}.{}: value of size {} exceeded declared width {} {} "
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
    std::vector<std::string> RenderStep(SqlQueryFormatter const& formatter, SqlMigrationPlanElement const& element)
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

namespace
{
    std::vector<std::string> ToSqlRemembering(SqlQueryFormatter const& formatter,
                                              SqlMigrationPlanElement const& element,
                                              MigrationRenderContext& context);
} // namespace

std::vector<std::string> ToSql(SqlQueryFormatter const& formatter, SqlMigrationPlanElement const& element)
{
    return RenderStep(formatter, element);
}

std::vector<std::string> ToSql(SqlQueryFormatter const& formatter,
                               SqlMigrationPlanElement const& element,
                               MigrationRenderContext& context)
{
    // First: let foreign-key columns follow the referenced column's narrow/wide text kind
    // (`fk-match-referenced-text`). The adapted copy is what gets rendered — and remembered.
    auto const adapted = AdaptForeignKeyTextTypes(formatter, context, element);
    if (adapted)
        return ToSqlRemembering(formatter, *adapted, context);
    return ToSqlRemembering(formatter, element, context);
}

namespace
{
    /// @brief Second half of the context-aware `ToSql`: updates the column caches from the
    /// step about to be rendered, then renders it with the value-level compat knobs applied.
    std::vector<std::string> ToSqlRemembering(SqlQueryFormatter const& formatter,
                                              SqlMigrationPlanElement const& element,
                                              MigrationRenderContext& context)
    {
        // Consume schema-affecting steps to keep the width cache current, so INSERT/
        // UPDATE steps that follow within the same migration plan see the column widths the
        // same CREATE/ALTER declared.
        std::visit(detail::overloaded {
                       [&](SqlCreateTablePlan const& step) { RememberColumnWidths(context, step); },
                       [&](SqlAlterTablePlan const& step) { RememberColumnWidths(context, step); },
                       [&](SqlDropTablePlan const& step) { ForgetTableWidths(context, step.schemaName, step.tableName); },
                       [](auto const&) {},
                   },
                   element);

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
} // namespace

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
