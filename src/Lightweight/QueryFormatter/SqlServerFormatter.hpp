// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../SqlQueryFormatter.hpp"
#include "SQLiteFormatter.hpp"

#include <reflection-cpp/reflection.hpp>

#include <cassert>
#include <format>

namespace Lightweight
{

class SqlServerQueryFormatter final: public SQLiteQueryFormatter
{
  protected:
    [[nodiscard]] static std::string FormatFromTable(std::string_view table)
    {
        // If already quoted (starts with [ or "), return as-is
        if (!table.empty() && (table.front() == '[' || table.front() == '"'))
            return std::string(table);
        // For backward compatibility, use double quotes for simple table names
        // Square brackets are used for qualified names via QualifiedTableName
        return std::format(R"("{}")", table);
    }

  public:
    [[nodiscard]] StringList DropTable(std::string_view schemaName,
                                       std::string_view const& tableName,
                                       bool ifExists = false,
                                       bool cascade = false) const override
    {
        StringList result;

        if (cascade)
        {
            // Drop all FK constraints referencing this table first using dynamic SQL
            std::string const schemaFilter = schemaName.empty() ? "dbo" : std::string(schemaName);

            result.emplace_back(std::format(
                R"(DECLARE @sql NVARCHAR(MAX) = N'';
SELECT @sql = @sql + 'ALTER TABLE ' + QUOTENAME(OBJECT_SCHEMA_NAME(fk.parent_object_id)) + '.' + QUOTENAME(OBJECT_NAME(fk.parent_object_id)) + ' DROP CONSTRAINT ' + QUOTENAME(fk.name) + '; '
FROM sys.foreign_keys fk
WHERE OBJECT_NAME(fk.referenced_object_id) = '{}' AND OBJECT_SCHEMA_NAME(fk.referenced_object_id) = '{}';
EXEC sp_executesql @sql;)",
                tableName,
                schemaFilter));
        }

        // Then drop the table
        if (ifExists)
            result.emplace_back(std::format("DROP TABLE IF EXISTS {};", FormatTableName(schemaName, tableName)));
        else
            result.emplace_back(std::format("DROP TABLE {};", FormatTableName(schemaName, tableName)));

        return result;
    }

    [[nodiscard]] std::string BinaryLiteral(std::span<uint8_t const> data) const override
    {
        std::string result;
        result.reserve((data.size() * 2) + 2);
        result += "0x";
        for (uint8_t byte: data)
            result += std::format("{:02X}", byte);
        return result;
    }

    /// @brief Emits a SQL Server string literal that round-trips arbitrary UTF-8
    /// content correctly under any database/connection code page.
    ///
    /// MSSQL parses the bytes between `'...'` (and `N'...'`) using the client/connection
    /// ANSI code page — *not* UTF-8. A UTF-8 multi-byte sequence like `0xC3 0xBC` (`ü`)
    /// embedded raw in `N'...'` is decoded as two separate CP-1252 characters (`Ã¼`),
    /// which:
    ///   1. garbles the stored value, and
    ///   2. inflates the perceived character count, causing legitimate 100-codepoint
    ///      strings to overflow `NVARCHAR(100)` with `String or binary data would be
    ///      truncated` (error 2628).
    ///
    /// The `N'...'` prefix alone is *not* enough: it tells MSSQL to store as Unicode,
    /// but doesn't change how the source bytes are decoded.
    ///
    /// To get exact round-tripping we split the literal at every non-ASCII codepoint
    /// and concatenate `NCHAR(N)` calls for each one:
    ///
    /// ```
    /// "EK-Min für x"  →  N'EK-Min f' + NCHAR(252) + N'r x'
    /// ```
    ///
    /// `NCHAR()` takes the Unicode codepoint as an integer and returns the matching
    /// `nchar(1)`. Concatenation with `+` glues the slices into one Unicode value
    /// MSSQL counts at exactly the codepoint length our application sees, so
    /// `lup-truncate`'s codepoint accounting matches the server's char accounting.
    ///
    /// Supplementary-plane codepoints (> U+FFFF) are emitted as a UTF-16 surrogate
    /// pair (two `NCHAR()` calls) — required for any potential emoji or rare CJK in
    /// the data.
    [[nodiscard]] std::string StringLiteral(std::string_view value) const noexcept override
    {
        return EncodeUnicodeLiteral(value);
    }

    [[nodiscard]] std::string StringLiteral(char value) const noexcept override
    {
        // Single-char path goes through the same encoder so the escaping rules
        // stay in one place. ASCII fast-paths produce `N'x'` directly.
        char const buf[1] = { value };
        return EncodeUnicodeLiteral(std::string_view { buf, 1 });
    }

  private:
    /// @brief Decodes the byte length of the UTF-8 sequence starting at `s[i]`.
    /// Returns `1` (and treats the byte as ASCII) for malformed lead bytes so the
    /// encoder always makes forward progress.
    static constexpr std::size_t Utf8SequenceLength(unsigned char c) noexcept
    {
        if (c < 0x80) return 1;
        if (c < 0xC0) return 1; // stray continuation; treat as 1 to advance
        if (c < 0xE0) return 2;
        if (c < 0xF0) return 3;
        return 4;
    }

    /// @brief Decodes one UTF-8 codepoint at `s[i]`. Returns the codepoint and the
    /// number of bytes consumed. Malformed sequences yield the lead byte verbatim
    /// as a single-byte codepoint so the encoder still produces *some* output.
    static constexpr std::pair<char32_t, std::size_t> DecodeUtf8(std::string_view s, std::size_t i) noexcept
    {
        auto const len = Utf8SequenceLength(static_cast<unsigned char>(s[i]));
        if (len == 1 || i + len > s.size())
            return { static_cast<char32_t>(static_cast<unsigned char>(s[i])), 1 };
        char32_t cp = 0;
        auto const lead = static_cast<unsigned char>(s[i]);
        switch (len)
        {
            case 2: cp = lead & 0x1F; break;
            case 3: cp = lead & 0x0F; break;
            case 4: cp = lead & 0x07; break;
            default: cp = lead; break; // unreachable given the early-return above
        }
        for (std::size_t k = 1; k < len; ++k)
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        return { cp, len };
    }

    /// @brief Encodes one codepoint into the running output. ASCII goes verbatim
    /// (with `'` doubled per SQL escaping); BMP non-ASCII becomes `NCHAR(N)`;
    /// supplementary-plane codepoints become a surrogate pair.
    static void AppendCodepoint(std::string& out, bool& inQuotedRun, char32_t cp)
    {
        auto const closeRun = [&] {
            if (inQuotedRun)
            {
                out += '\'';
                inQuotedRun = false;
            }
        };
        auto const openRun = [&] {
            if (!inQuotedRun)
            {
                if (!out.empty())
                    out += " + ";
                out += "N'";
                inQuotedRun = true;
            }
        };

        if (cp < 0x80)
        {
            openRun();
            if (cp == '\'')
                out += "''";
            else
                out += static_cast<char>(cp);
            return;
        }
        closeRun();
        if (!out.empty())
            out += " + ";
        if (cp <= 0xFFFF)
        {
            out += std::format("NCHAR({})", static_cast<unsigned>(cp));
            return;
        }
        // Supplementary plane: emit a UTF-16 surrogate pair.
        char32_t const adjusted = cp - 0x10000;
        char32_t const hi = 0xD800 + (adjusted >> 10);
        char32_t const lo = 0xDC00 + (adjusted & 0x3FF);
        out += std::format("NCHAR({}) + NCHAR({})", static_cast<unsigned>(hi), static_cast<unsigned>(lo));
    }

    /// @brief Top-level encoder: walks the UTF-8 input and emits a T-SQL expression
    /// that evaluates to the input string under any client code page. Empty inputs
    /// produce `N''` (the canonical empty Unicode literal).
    static std::string EncodeUnicodeLiteral(std::string_view value)
    {
        if (value.empty())
            return "N''";
        std::string out;
        out.reserve(value.size() + 4);
        bool inQuotedRun = false;
        std::size_t i = 0;
        while (i < value.size())
        {
            auto const [cp, len] = DecodeUtf8(value, i);
            AppendCodepoint(out, inQuotedRun, cp);
            i += len;
        }
        if (inQuotedRun)
            out += '\'';
        return out;
    }

  public:

    [[nodiscard]] std::string QualifiedTableName(std::string_view schema, std::string_view table) const override
    {
        if (schema.empty())
            return std::format("[{}]", table);
        return std::format("[{}].[{}]", schema, table);
    }

    [[nodiscard]] std::string QueryLastInsertId(std::string_view /*tableName*/) const override
    {
        // TODO: Figure out how to get the last insert id in SQL Server for a given table.
        return std::format("SELECT @@IDENTITY");
    }

    [[nodiscard]] std::string_view BooleanLiteral(bool literalValue) const noexcept override
    {
        return literalValue ? "1" : "0";
    }

    [[nodiscard]] std::string_view DateFunction() const noexcept override
    {
        return "GETDATE()";
    }

    [[nodiscard]] std::string SelectFirst(bool distinct,
                                          // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
                                          std::string_view fields,
                                          std::string_view fromTable,
                                          std::string_view fromTableAlias,
                                          std::string_view tableJoins,
                                          std::string_view whereCondition,
                                          std::string_view orderBy,
                                          size_t count) const override
    {
        std::stringstream sqlQueryString;
        sqlQueryString << "SELECT";
        if (distinct)
            sqlQueryString << " DISTINCT";
        sqlQueryString << " TOP " << count;
        sqlQueryString << ' ' << fields;
        sqlQueryString << " FROM " << FormatFromTable(fromTable);
        if (!fromTableAlias.empty())
            sqlQueryString << " AS [" << fromTableAlias << ']';
        sqlQueryString << tableJoins;
        sqlQueryString << whereCondition;
        sqlQueryString << orderBy;
        return sqlQueryString.str();
    }

    [[nodiscard]] std::string SelectRange(bool distinct,
                                          // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
                                          std::string_view fields,
                                          std::string_view fromTable,
                                          std::string_view fromTableAlias,
                                          std::string_view tableJoins,
                                          std::string_view whereCondition,
                                          std::string_view orderBy,
                                          std::string_view groupBy,
                                          std::size_t offset,
                                          std::size_t limit) const override
    {
        assert(!orderBy.empty());
        std::stringstream sqlQueryString;
        sqlQueryString << "SELECT " << fields;
        if (distinct)
            sqlQueryString << " DISTINCT";
        sqlQueryString << " FROM " << FormatFromTable(fromTable);
        if (!fromTableAlias.empty())
            sqlQueryString << " AS [" << fromTableAlias << ']';
        sqlQueryString << tableJoins;
        sqlQueryString << whereCondition;
        sqlQueryString << groupBy;
        sqlQueryString << orderBy;
        sqlQueryString << " OFFSET " << offset << " ROWS FETCH NEXT " << limit << " ROWS ONLY";
        return sqlQueryString.str();
    }

    [[nodiscard]] std::string ColumnType(SqlColumnTypeDefinition const& type) const override
    {
        using namespace SqlColumnTypeDefinitions;
        return std::visit(detail::overloaded {
                              [](Bigint const&) -> std::string { return "BIGINT"; },
                              [](Binary const& type) -> std::string {
                                  if (type.size == 0 || type.size > 8000)
                                      return "VARBINARY(MAX)";
                                  else
                                      return std::format("VARBINARY({})", type.size);
                              },
                              [](Bool const&) -> std::string { return "BIT"; },
                              [](Char const& type) -> std::string { return std::format("CHAR({})", type.size); },
                              [](Date const&) -> std::string { return "DATE"; },
                              [](DateTime const&) -> std::string { return "DATETIME"; },
                              [](Decimal const& type) -> std::string {
                                  return std::format("DECIMAL({}, {})", type.precision, type.scale);
                              },
                              [](Guid const&) -> std::string { return "UNIQUEIDENTIFIER"; },
                              [](Integer const&) -> std::string { return "INTEGER"; },
                              [](NChar const& type) -> std::string { return std::format("NCHAR({})", type.size); },
                              [](NVarchar const& type) -> std::string {
                                  if (type.size == 0 || type.size > SqlOptimalMaxColumnSize)
                                      return "NVARCHAR(MAX)";
                                  else
                                      return std::format("NVARCHAR({})", type.size);
                              },
                              [](Real const&) -> std::string { return "REAL"; },
                              [](Smallint const&) -> std::string { return "SMALLINT"; },
                              [](Text const&) -> std::string { return "VARCHAR(MAX)"; },
                              [](Time const&) -> std::string { return "TIME"; },
                              [](Timestamp const&) -> std::string { return "TIMESTAMP"; },
                              [](Tinyint const&) -> std::string { return "TINYINT"; },
                              [](VarBinary const& type) -> std::string {
                                  if (type.size == 0 || type.size > 8000)
                                      return "VARBINARY(MAX)";
                                  else
                                      return std::format("VARBINARY({})", type.size);
                              },
                              [](Varchar const& type) -> std::string {
                                  if (type.size == 0 || type.size > SqlOptimalMaxColumnSize)
                                      return "VARCHAR(MAX)";
                                  else
                                      return std::format("VARCHAR({})", type.size);
                              },
                          },
                          type);
    }

    [[nodiscard]] std::string BuildColumnDefinition(SqlColumnDeclaration const& column) const override
    {
        std::stringstream sqlQueryString;
        sqlQueryString << '"' << column.name << "\" " << ColumnType(column.type);

        if (column.required)
            sqlQueryString << " NOT NULL";

        if (column.primaryKey == SqlPrimaryKeyType::AUTO_INCREMENT)
            sqlQueryString << " IDENTITY(1,1) PRIMARY KEY";
        else if (column.primaryKey == SqlPrimaryKeyType::NONE && !column.index && column.unique)
            sqlQueryString << " UNIQUE";

        if (!column.defaultValue.empty())
            sqlQueryString << " DEFAULT " << column.defaultValue;

        return sqlQueryString.str();
    }

    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
    [[nodiscard]] StringList CreateTable(std::string_view schema,
                                         std::string_view tableName,
                                         std::vector<SqlColumnDeclaration> const& columns,
                                         std::vector<SqlCompositeForeignKeyConstraint> const& foreignKeys,
                                         bool ifNotExists = false) const override
    {
        std::stringstream ss;

        // SQL Server doesn't have CREATE TABLE IF NOT EXISTS, use conditional block
        if (ifNotExists)
        {
            std::string schemaFilter = schema.empty() ? "dbo" : std::string(schema);
            ss << std::format("IF NOT EXISTS (SELECT * FROM sys.tables WHERE name = '{}' AND schema_id = SCHEMA_ID('{}'))\n",
                              tableName,
                              schemaFilter);
        }

        ss << std::format("CREATE TABLE {} (", FormatTableName(schema, tableName));

        bool first = true;
        for (auto const& column: columns)
        {
            if (!first)
                ss << ",";
            first = false;
            ss << "\n    " << BuildColumnDefinition(column);
        }

        auto const primaryKeys = [&]() -> std::vector<std::string> {
            std::vector<std::pair<uint16_t, std::string>> indexedPrimaryKeys;
            for (auto const& col: columns)
                if (col.primaryKey != SqlPrimaryKeyType::NONE)
                    indexedPrimaryKeys.emplace_back(col.primaryKeyIndex, col.name);
            std::ranges::sort(indexedPrimaryKeys, [](auto const& a, auto const& b) { return a.first < b.first; });

            std::vector<std::string> primaryKeys;
            primaryKeys.reserve(indexedPrimaryKeys.size());
            for (auto const& [index, name]: indexedPrimaryKeys)
                primaryKeys.push_back(name);
            return primaryKeys;
        }();

        if (!primaryKeys.empty())
        {
            // If primary key is AUTO_INCREMENT, it's already defined inline in BuildColumnDefinition.
            // Only add explicit PRIMARY KEY constraint if NOT AUTO_INCREMENT?
            // SQLiteFormatter logic:
            // if (!primaryKeys.empty()) ss << ", PRIMARY KEY (" << Join(primaryKeys, ", ") << ")";
            // But BuildColumnDefinition adds "PRIMARY KEY" for AUTO_INCREMENT!
            // Double primary key definition is invalid.

            // Check if any column is AUTO_INCREMENT
            bool hasIdentity = false;
            for (auto const& col: columns)
                if (col.primaryKey == SqlPrimaryKeyType::AUTO_INCREMENT)
                    hasIdentity = true;

            if (!hasIdentity)
            {
                ss << ",\n    PRIMARY KEY (";
                bool firstPk = true;
                for (auto const& pk: primaryKeys)
                {
                    if (!firstPk)
                        ss << ", ";
                    firstPk = false;
                    ss << '"' << pk << '"';
                }
                ss << ")";
            }
        }

        if (!foreignKeys.empty())
        {
            for (auto const& fk: foreignKeys)
            {
                ss << ",\n    CONSTRAINT \""
                   << BuildForeignKeyConstraintName(tableName, fk.columns) << '"'
                   << " FOREIGN KEY (";

                size_t i = 0;
                for (auto const& col: fk.columns)
                {
                    if (i++ > 0)
                        ss << ", ";
                    ss << '"' << col << '"';
                }

                ss << ") REFERENCES " << FormatTableName(schema, fk.referencedTableName) << " (";

                i = 0;
                for (auto const& col: fk.referencedColumns)
                {
                    if (i++ > 0)
                        ss << ", ";
                    ss << '"' << col << '"';
                }
                ss << ")";
            }
        }

        // Add single-column foreign keys that were defined inline in SQLite but need to be table-constraints here
        // or just appended if we didn't add them in BuildColumnDefinition (which we didn't).
        for (auto const& column: columns)
        {
            if (column.foreignKey)
            {
                ss << ",\n    " << BuildForeignKeyConstraint(tableName, column.name, *column.foreignKey);
            }
        }

        ss << "\n);";

        StringList result;
        result.emplace_back(ss.str());

        // Create Indexes
        for (SqlColumnDeclaration const& column: columns)
        {
            if (column.index && column.primaryKey == SqlPrimaryKeyType::NONE)
            {
                // primary keys are always indexed
                if (column.unique)
                {
                    if (schema.empty())
                        result.emplace_back(std::format(R"(CREATE UNIQUE INDEX "{}_{}_index" ON "{}" ("{}");)",
                                                        tableName,
                                                        column.name,
                                                        tableName,
                                                        column.name));
                    else
                        result.emplace_back(std::format(R"(CREATE UNIQUE INDEX "{}_{}_index" ON "{}"."{}" ("{}");)",
                                                        tableName,
                                                        column.name,
                                                        schema,
                                                        tableName,
                                                        column.name));
                }
                else
                {
                    if (schema.empty())
                        result.emplace_back(std::format(
                            R"(CREATE INDEX "{}_{}_index" ON "{}" ("{}");)", tableName, column.name, tableName, column.name));
                    else
                        result.emplace_back(std::format(R"(CREATE INDEX "{}_{}_index" ON "{}"."{}" ("{}");)",
                                                        tableName,
                                                        column.name,
                                                        schema,
                                                        tableName,
                                                        column.name));
                }
            }
        }

        return result;
    }

    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
    [[nodiscard]] StringList AlterTable(std::string_view schemaName,
                                        std::string_view tableName,
                                        std::vector<SqlAlterTableCommand> const& commands) const override
    {
        std::stringstream sqlQueryString;

        int currentCommand = 0;
        for (SqlAlterTableCommand const& command: commands)
        {
            if (currentCommand > 0)
                sqlQueryString << '\n';
            ++currentCommand;

            using namespace SqlAlterTableCommands;
            sqlQueryString << std::visit(
                detail::overloaded {
                    [schemaName, tableName](RenameTable const& actualCommand) -> std::string {
                        return std::format(R"(ALTER TABLE {} RENAME TO "{}";)",
                                           FormatTableName(schemaName, tableName),
                                           actualCommand.newTableName);
                    },
                    [schemaName, tableName, this](AddColumn const& actualCommand) -> std::string {
                        return std::format(R"(ALTER TABLE {} ADD "{}" {} {};)",
                                           FormatTableName(schemaName, tableName),
                                           actualCommand.columnName,
                                           ColumnType(actualCommand.columnType),
                                           actualCommand.nullable == SqlNullable::NotNull ? "NOT NULL" : "NULL");
                    },
                    [schemaName, tableName, this](AlterColumn const& actualCommand) -> std::string {
                        return std::format(R"(ALTER TABLE {} ALTER COLUMN "{}" {} {};)",
                                           FormatTableName(schemaName, tableName),
                                           actualCommand.columnName,
                                           ColumnType(actualCommand.columnType),
                                           actualCommand.nullable == SqlNullable::NotNull ? "NOT NULL" : "NULL");
                    },
                    [schemaName, tableName](RenameColumn const& actualCommand) -> std::string {
                        return std::format(R"(ALTER TABLE {} RENAME COLUMN "{}" TO "{}";)",
                                           FormatTableName(schemaName, tableName),
                                           actualCommand.oldColumnName,
                                           actualCommand.newColumnName);
                    },
                    [schemaName, tableName](DropColumn const& actualCommand) -> std::string {
                        return std::format(R"(ALTER TABLE {} DROP COLUMN "{}";)",
                                           FormatTableName(schemaName, tableName),
                                           actualCommand.columnName);
                    },
                    [schemaName, tableName](AddIndex const& actualCommand) -> std::string {
                        using namespace std::string_view_literals;
                        auto const uniqueStr = actualCommand.unique ? "UNIQUE "sv : ""sv;
                        if (schemaName.empty())
                            return std::format(R"(CREATE {2}INDEX "{0}_{1}_index" ON "{0}" ("{1}");)",
                                               tableName,
                                               actualCommand.columnName,
                                               uniqueStr);
                        else
                            return std::format(R"(CREATE {3}INDEX "{0}_{1}_{2}_index" ON "{0}"."{1}" ("{2}");)",
                                               schemaName,
                                               tableName,
                                               actualCommand.columnName,
                                               uniqueStr);
                    },
                    [schemaName, tableName](DropIndex const& actualCommand) -> std::string {
                        if (schemaName.empty())
                            return std::format(R"(DROP INDEX "{0}_{1}_index";)", tableName, actualCommand.columnName);
                        else
                            return std::format(
                                R"(DROP INDEX "{0}_{1}_{2}_index";)", schemaName, tableName, actualCommand.columnName);
                    },
                    [schemaName, tableName](AddForeignKey const& actualCommand) -> std::string {
                        // Guard with `IF NOT EXISTS (sys.foreign_keys WHERE name=…)` so the
                        // statement is idempotent. LUP migrations re-add foreign keys that
                        // earlier ALTER scripts already created (the 4_7_6 vs 5_0_0 overlap),
                        // and SQL Server has no `ADD CONSTRAINT IF NOT EXISTS`.
                        auto const fkName = std::format("FK_{}_{}", tableName, actualCommand.columnName);
                        return std::format(
                            R"(IF NOT EXISTS (SELECT 1 FROM sys.foreign_keys WHERE name = '{0}') ALTER TABLE {1} ADD {2};)",
                            fkName,
                            FormatTableName(schemaName, tableName),
                            BuildForeignKeyConstraint(
                                tableName, actualCommand.columnName, actualCommand.referencedColumn));
                    },
                    [schemaName, tableName](DropForeignKey const& actualCommand) -> std::string {
                        return std::format(R"(ALTER TABLE {} DROP CONSTRAINT "{}";)",
                                           FormatTableName(schemaName, tableName),
                                           std::format("FK_{}_{}", tableName, actualCommand.columnName));
                    },
                    [schemaName, tableName](AddCompositeForeignKey const& actualCommand) -> std::string {
                        std::stringstream ss;
                        ss << "ALTER TABLE " << FormatTableName(schemaName, tableName) << " ADD CONSTRAINT \""
                           << BuildForeignKeyConstraintName(tableName, actualCommand.columns)
                           << "\" FOREIGN KEY (";

                        size_t i = 0;
                        for (auto const& col: actualCommand.columns)
                        {
                            if (i++ > 0)
                                ss << ", ";
                            ss << '"' << col << '"';
                        }
                        ss << ") REFERENCES " << FormatTableName(schemaName, actualCommand.referencedTableName) << " (";

                        i = 0;
                        for (auto const& col: actualCommand.referencedColumns)
                        {
                            if (i++ > 0)
                                ss << ", ";
                            ss << '"' << col << '"';
                        }
                        ss << ");";
                        return ss.str();
                    },
                    [schemaName, tableName, this](AddColumnIfNotExists const& actualCommand) -> std::string {
                        // SQL Server uses conditional IF NOT EXISTS
                        return std::format(
                            R"(IF NOT EXISTS (SELECT * FROM sys.columns WHERE object_id = OBJECT_ID('{}') AND name = '{}')
ALTER TABLE {} ADD "{}" {} {};)",
                            FormatTableName(schemaName, tableName),
                            actualCommand.columnName,
                            FormatTableName(schemaName, tableName),
                            actualCommand.columnName,
                            ColumnType(actualCommand.columnType),
                            actualCommand.nullable == SqlNullable::NotNull ? "NOT NULL" : "NULL");
                    },
                    [schemaName, tableName](DropColumnIfExists const& actualCommand) -> std::string {
                        // SQL Server uses conditional IF EXISTS
                        return std::format(
                            R"(IF EXISTS (SELECT * FROM sys.columns WHERE object_id = OBJECT_ID('{}') AND name = '{}')
ALTER TABLE {} DROP COLUMN "{}";)",
                            FormatTableName(schemaName, tableName),
                            actualCommand.columnName,
                            FormatTableName(schemaName, tableName),
                            actualCommand.columnName);
                    },
                    [schemaName, tableName](DropIndexIfExists const& actualCommand) -> std::string {
                        if (schemaName.empty())
                            return std::format(
                                R"(IF EXISTS (SELECT * FROM sys.indexes WHERE name = '{0}_{1}_index' AND object_id = OBJECT_ID('{0}'))
DROP INDEX "{0}_{1}_index" ON "{0}";)",
                                tableName,
                                actualCommand.columnName);
                        else
                            return std::format(
                                R"(IF EXISTS (SELECT * FROM sys.indexes WHERE name = '{0}_{1}_{2}_index')
DROP INDEX "{0}_{1}_{2}_index" ON "{0}"."{1}";)",
                                schemaName,
                                tableName,
                                actualCommand.columnName);
                    },
                },
                command);
        }

        return { sqlQueryString.str() };
    }

    [[nodiscard]] std::string QueryServerVersion() const override
    {
        return "SELECT @@VERSION";
    }
};

} // namespace Lightweight
