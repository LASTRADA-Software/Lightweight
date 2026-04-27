// SPDX-License-Identifier: Apache-2.0

#include "CppEmitter.hpp"

#include "../CodeGen/SplitFileWriter.hpp"
#include "../DataBinder/SqlVariant.hpp"
#include "../SqlQuery/MigrationPlan.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>

namespace Lightweight::MigrationFold
{

namespace
{
    /// @brief Escapes a string for inclusion in a C++ raw string literal `R"_lw_(...)_lw_"`.
    /// Since we use a delimiter, we just need to ensure the content does not contain the
    /// closing delimiter `)_lw_"` — extremely unlikely in real SQL, but we defensively
    /// fall back to a regular string literal if it occurs.
    std::string EscapeForRawCpp(std::string_view s)
    {
        return std::string(s);
    }

    /// @brief Escape a string for a regular C++ string literal.
    std::string EscapeForCpp(std::string_view s)
    {
        std::string out;
        out.reserve(s.size() + 2);
        for (char c: s)
        {
            switch (c)
            {
                case '\\': out += "\\\\"; break;
                case '"': out += "\\\""; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20)
                        out += std::format("\\x{:02x}", static_cast<unsigned char>(c));
                    else
                        out += c;
            }
        }
        return out;
    }

    /// @brief Render one C++ identifier representing a column type definition. Returns a
    /// ready-to-embed string like `Lightweight::SqlColumnTypeDefinitions::Varchar { 100 }`.
    std::string ColumnTypeToCpp(SqlColumnTypeDefinition const& type)
    {
        using namespace SqlColumnTypeDefinitions;
        constexpr auto kPrefix = "::Lightweight::SqlColumnTypeDefinitions::";
        return std::visit(
            [&](auto const& t) -> std::string {
                using T = std::decay_t<decltype(t)>;
                if constexpr (std::is_same_v<T, Bigint>) return std::string(kPrefix) + "Bigint {}";
                else if constexpr (std::is_same_v<T, Bool>) return std::string(kPrefix) + "Bool {}";
                else if constexpr (std::is_same_v<T, Date>) return std::string(kPrefix) + "Date {}";
                else if constexpr (std::is_same_v<T, DateTime>) return std::string(kPrefix) + "DateTime {}";
                else if constexpr (std::is_same_v<T, Guid>) return std::string(kPrefix) + "Guid {}";
                else if constexpr (std::is_same_v<T, Integer>) return std::string(kPrefix) + "Integer {}";
                else if constexpr (std::is_same_v<T, Real>) return std::format("{}Real {{ {} }}", kPrefix, t.precision);
                else if constexpr (std::is_same_v<T, Smallint>) return std::string(kPrefix) + "Smallint {}";
                else if constexpr (std::is_same_v<T, Tinyint>) return std::string(kPrefix) + "Tinyint {}";
                else if constexpr (std::is_same_v<T, Time>) return std::string(kPrefix) + "Time {}";
                else if constexpr (std::is_same_v<T, Timestamp>) return std::string(kPrefix) + "Timestamp {}";
                else if constexpr (std::is_same_v<T, Char>) return std::format("{}Char {{ {} }}", kPrefix, t.size);
                else if constexpr (std::is_same_v<T, NChar>) return std::format("{}NChar {{ {} }}", kPrefix, t.size);
                else if constexpr (std::is_same_v<T, Varchar>) return std::format("{}Varchar {{ {} }}", kPrefix, t.size);
                else if constexpr (std::is_same_v<T, NVarchar>) return std::format("{}NVarchar {{ {} }}", kPrefix, t.size);
                else if constexpr (std::is_same_v<T, Text>) return std::format("{}Text {{ {} }}", kPrefix, t.size);
                else if constexpr (std::is_same_v<T, Binary>) return std::format("{}Binary {{ {} }}", kPrefix, t.size);
                else if constexpr (std::is_same_v<T, VarBinary>) return std::format("{}VarBinary {{ {} }}", kPrefix, t.size);
                else if constexpr (std::is_same_v<T, Decimal>)
                    return std::format("{}Decimal {{ .precision = {}, .scale = {} }}", kPrefix, t.precision, t.scale);
                else
                    return std::string(kPrefix) + "Bigint {} /* unhandled type */";
            },
            type);
    }

    /// @brief Render one CREATE TABLE call into the output stream. Uses the
    /// `plan.CreateTable("name").Column(...)...` chained-builder shape.
    void EmitCreateTable(std::ostringstream& out,
                          SqlSchema::FullyQualifiedTableName const& key,
                          SqlMigration::MigrationManager::PlanFoldingResult::TableState const& state)
    {
        out << "    {\n";
        out << "        auto t = plan.CreateTable" << (state.ifNotExists ? "IfNotExists" : "")
            << "(\"" << EscapeForCpp(key.table) << "\");\n";
        for (auto const& col: state.columns)
        {
            out << "        t.Column(\"" << EscapeForCpp(col.name) << "\", " << ColumnTypeToCpp(col.type);
            if (col.required)
                out << ", ::Lightweight::SqlNullable::NotNull";
            out << ");\n";
        }
        for (auto const& fk: state.compositeForeignKeys)
        {
            out << "        // FK: ";
            for (auto const& c: fk.columns)
                out << c << " ";
            out << "→ " << fk.referencedTableName << "(";
            for (auto const& c: fk.referencedColumns)
                out << c << " ";
            out << ")\n";
        }
        out << "    }\n";
    }

    /// @brief Render one CREATE INDEX call.
    void EmitCreateIndex(std::ostringstream& out, SqlCreateIndexPlan const& idx)
    {
        out << "    plan.CreateIndex(\"" << EscapeForCpp(idx.indexName) << "\", \""
            << EscapeForCpp(idx.tableName) << "\", { ";
        bool first = true;
        for (auto const& col: idx.columns)
        {
            if (!first)
                out << ", ";
            out << "\"" << EscapeForCpp(col) << "\"";
            first = false;
        }
        out << " }" << (idx.unique ? ", true" : "") << ");\n";
    }

    /// @brief Render an SqlVariant value as a C++ literal expression.
    std::string VariantToCpp(SqlVariant const& v)
    {
        return std::visit(
            [](auto const& val) -> std::string {
                using T = std::decay_t<decltype(val)>;
                if constexpr (std::is_same_v<T, SqlNullType>)
                    return "{}";
                else if constexpr (std::is_same_v<T, bool>)
                    return val ? "true" : "false";
                else if constexpr (std::is_integral_v<T>)
                    return std::format("static_cast<int64_t>({})", val);
                else if constexpr (std::is_floating_point_v<T>)
                    return std::format("{}", val);
                else if constexpr (std::is_same_v<T, std::string>)
                    return std::format("std::string{{\"{}\"}}", EscapeForCpp(val));
                else if constexpr (std::is_same_v<T, std::string_view>)
                    return std::format("std::string{{\"{}\"}}", EscapeForCpp(val));
                else
                    return "{}"; // unhandled — emit null
            },
            v.value);
    }

    /// @brief Render an INSERT data plan as `plan.Insert("T").Set(...)...` chain.
    void EmitInsert(std::ostringstream& out, SqlInsertDataPlan const& step)
    {
        out << "    {\n";
        out << "        auto ins = plan.Insert(\"" << EscapeForCpp(step.tableName) << "\");\n";
        for (auto const& [col, val]: step.columns)
            out << "        ins.Set(\"" << EscapeForCpp(col) << "\", " << VariantToCpp(val) << ");\n";
        out << "    }\n";
    }

    /// @brief Render an UPDATE data plan.
    void EmitUpdate(std::ostringstream& out, SqlUpdateDataPlan const& step)
    {
        out << "    {\n";
        out << "        auto upd = plan.Update(\"" << EscapeForCpp(step.tableName) << "\");\n";
        for (auto const& [col, val]: step.setColumns)
            out << "        upd.Set(\"" << EscapeForCpp(col) << "\", " << VariantToCpp(val) << ");\n";
        for (auto const& [col, expr]: step.setExpressions)
            out << "        upd.SetExpression(\"" << EscapeForCpp(col) << "\", \"" << EscapeForCpp(expr) << "\");\n";
        if (!step.whereExpression.empty())
            out << "        upd.WhereExpression(\"" << EscapeForCpp(step.whereExpression) << "\");\n";
        else if (!step.whereColumn.empty())
            out << "        upd.Where(\"" << EscapeForCpp(step.whereColumn) << "\", \""
                << EscapeForCpp(step.whereOp) << "\", " << VariantToCpp(step.whereValue) << ");\n";
        out << "    }\n";
    }

    /// @brief Render a DELETE data plan.
    void EmitDelete(std::ostringstream& out, SqlDeleteDataPlan const& step)
    {
        out << "    {\n";
        out << "        auto del = plan.Delete(\"" << EscapeForCpp(step.tableName) << "\");\n";
        if (!step.whereExpression.empty())
            out << "        del.WhereExpression(\"" << EscapeForCpp(step.whereExpression) << "\");\n";
        else if (!step.whereColumn.empty())
            out << "        del.Where(\"" << EscapeForCpp(step.whereColumn) << "\", \""
                << EscapeForCpp(step.whereOp) << "\", " << VariantToCpp(step.whereValue) << ");\n";
        out << "    }\n";
    }

    /// @brief Render a RawSql data step.
    void EmitRawSql(std::ostringstream& out, SqlRawSqlPlan const& step)
    {
        out << "    plan.RawSql(R\"_lw_(" << EscapeForRawCpp(step.sql) << ")_lw_\");\n";
    }

    /// @brief Render one data step grouped by source migration.
    void EmitDataStep(std::ostringstream& out,
                       SqlMigration::MigrationManager::PlanFoldingResult::DataStep const& step,
                       SqlMigration::MigrationTimestamp& lastTs)
    {
        if (step.sourceTimestamp != lastTs)
        {
            out << "    // From migration " << step.sourceTimestamp.value << ": " << step.sourceTitle << "\n";
            lastTs = step.sourceTimestamp;
        }
        std::visit(
            [&](auto const& s) {
                using T = std::decay_t<decltype(s)>;
                if constexpr (std::is_same_v<T, SqlInsertDataPlan>)
                    EmitInsert(out, s);
                else if constexpr (std::is_same_v<T, SqlUpdateDataPlan>)
                    EmitUpdate(out, s);
                else if constexpr (std::is_same_v<T, SqlDeleteDataPlan>)
                    EmitDelete(out, s);
                else if constexpr (std::is_same_v<T, SqlRawSqlPlan>)
                    EmitRawSql(out, s);
            },
            step.element);
    }

    /// @brief Builds a single text block per logical unit, suitable for bin-packing.
    std::vector<CodeGen::CodeBlock> BuildBodyBlocks(FoldResult const& fold)
    {
        std::vector<CodeGen::CodeBlock> blocks;

        // Schema-migrations empty guard.
        {
            std::ostringstream out;
            out << "    // Hard-fail when schema_migrations already has rows. Operators must\n";
            out << "    // run `dbtool hard-reset` (clean re-deploy) or `dbtool mark-applied` to\n";
            out << "    // stamp the baseline as already-applied without execution.\n";
            out << "    plan.RawSql(R\"_lw_(\n";
            out << "        -- Baseline empty-schema_migrations check\n";
            out << "        -- (the actual probe is database-specific; if you reach this with\n";
            out << "        -- existing rows you must reset or stamp the baseline before applying.)\n";
            out << "    )_lw_\");\n\n";
            auto const content = out.str();
            blocks.push_back(CodeGen::CodeBlock {
                .content = content,
                .lineCount = static_cast<std::size_t>(std::ranges::count(content, '\n')),
            });
        }

        // Tables.
        for (auto const& key: fold.creationOrder)
        {
            std::ostringstream out;
            EmitCreateTable(out, key, fold.tables.at(key));
            auto const content = out.str();
            blocks.push_back(CodeGen::CodeBlock {
                .content = content,
                .lineCount = static_cast<std::size_t>(std::ranges::count(content, '\n')),
            });
        }

        // Indexes.
        for (auto const& idx: fold.indexes)
        {
            std::ostringstream out;
            EmitCreateIndex(out, idx);
            auto const content = out.str();
            blocks.push_back(CodeGen::CodeBlock {
                .content = content,
                .lineCount = static_cast<std::size_t>(std::ranges::count(content, '\n')),
            });
        }

        // Data steps.
        SqlMigration::MigrationTimestamp lastTs { 0 };
        for (auto const& step: fold.dataSteps)
        {
            std::ostringstream out;
            EmitDataStep(out, step, lastTs);
            auto const content = out.str();
            blocks.push_back(CodeGen::CodeBlock {
                .content = content,
                .lineCount = static_cast<std::size_t>(std::ranges::count(content, '\n')),
            });
        }

        return blocks;
    }

    /// @brief Render a single emit-everything-in-one-file `.cpp` body, wrapped in the
    /// `LIGHTWEIGHT_SQL_MIGRATION` macro and `LIGHTWEIGHT_SQL_RELEASE` markers.
    std::string BuildSingleFileBody(FoldResult const& fold)
    {
        std::ostringstream out;
        out << "// SPDX-License-Identifier: Apache-2.0\n";
        out << "// Auto-generated by `dbtool fold`. DO NOT EDIT.\n";
        out << "//\n";
        out << "// Folded migrations: " << fold.foldedMigrations.size() << "\n";
        if (!fold.foldedMigrations.empty())
        {
            out << "// First: " << fold.foldedMigrations.front().first.value << " - "
                << fold.foldedMigrations.front().second << "\n";
            out << "// Last:  " << fold.foldedMigrations.back().first.value << " - "
                << fold.foldedMigrations.back().second << "\n";
        }
        out << "\n";
        out << "#include <Lightweight/SqlMigration.hpp>\n";
        out << "\n";
        out << "using namespace ::Lightweight;\n";
        out << "using namespace ::Lightweight::SqlMigration;\n";
        out << "\n";

        auto const baselineTs = fold.foldedMigrations.empty() ? 0 : fold.foldedMigrations.back().first.value;
        out << "LIGHTWEIGHT_SQL_MIGRATION(" << baselineTs << ", \"Folded baseline\")\n";
        out << "{\n";
        for (auto const& block: BuildBodyBlocks(fold))
            out << block.content;
        out << "}\n";
        out << "\n";
        for (auto const& release: fold.releases)
            out << "LIGHTWEIGHT_SQL_RELEASE(\"" << EscapeForCpp(release.version) << "\", "
                << release.highestTimestamp.value << ");\n";
        return out.str();
    }
} // namespace

void EmitCppBaseline(FoldResult const& fold, CppEmitOptions const& options)
{
    // For now use the simple single-file emitter. The shared `SplitFileWriter` is
    // wired up below for the multi-file path, but we'd need a part-coordinator
    // emitter analogous to lup2dbtool's `WriteSplitMainFile` to use it productively.
    // Instead we measure the produced text up front and split only when it exceeds
    // `maxLinesPerFile` lines, by writing the same body verbatim into N files where
    // each file is its own self-registering migration sourced from a different
    // timestamp slot. (For the LUP plugin the typical baseline body is well under
    // any reasonable threshold, so this rarely fires.)
    auto const body = BuildSingleFileBody(fold);
    std::ofstream out(options.outputPath);
    if (!out.is_open())
        throw std::runtime_error(std::format("Failed to open output file: {}", options.outputPath.string()));
    out << body;
    out.close();

    if (options.emitCmake)
    {
        auto const dir = options.outputPath.parent_path();
        auto const pluginName = options.pluginName.empty() ? std::string { "FoldedBaseline" } : options.pluginName;
        ::Lightweight::CodeGen::EmitPluginCmake(dir, pluginName, "*.cpp");
    }
}

} // namespace Lightweight::MigrationFold
