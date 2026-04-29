// SPDX-License-Identifier: Apache-2.0

#include "../CodeGen/SplitFileWriter.hpp"
#include "../DataBinder/SqlVariant.hpp"
#include "../SqlQuery/MigrationPlan.hpp"
#include "CppEmitter.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace Lightweight::MigrationFold
{

namespace
{
    using ::Lightweight::detail::overloaded;

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
                case '\\':
                    out += "\\\\";
                    break;
                case '"':
                    out += "\\\"";
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\t':
                    out += "\\t";
                    break;
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
        return std::visit(overloaded {
                              [&](Bigint const&) { return std::format("{}Bigint {{}}", kPrefix); },
                              [&](Bool const&) { return std::format("{}Bool {{}}", kPrefix); },
                              [&](Date const&) { return std::format("{}Date {{}}", kPrefix); },
                              [&](DateTime const&) { return std::format("{}DateTime {{}}", kPrefix); },
                              [&](Guid const&) { return std::format("{}Guid {{}}", kPrefix); },
                              [&](Integer const&) { return std::format("{}Integer {{}}", kPrefix); },
                              [&](Smallint const&) { return std::format("{}Smallint {{}}", kPrefix); },
                              [&](Tinyint const&) { return std::format("{}Tinyint {{}}", kPrefix); },
                              [&](Time const&) { return std::format("{}Time {{}}", kPrefix); },
                              [&](Timestamp const&) { return std::format("{}Timestamp {{}}", kPrefix); },
                              [&](Real const& t) { return std::format("{}Real {{ {} }}", kPrefix, t.precision); },
                              [&](Char const& t) { return std::format("{}Char {{ {} }}", kPrefix, t.size); },
                              [&](NChar const& t) { return std::format("{}NChar {{ {} }}", kPrefix, t.size); },
                              [&](Varchar const& t) { return std::format("{}Varchar {{ {} }}", kPrefix, t.size); },
                              [&](NVarchar const& t) { return std::format("{}NVarchar {{ {} }}", kPrefix, t.size); },
                              [&](Text const& t) { return std::format("{}Text {{ {} }}", kPrefix, t.size); },
                              [&](Binary const& t) { return std::format("{}Binary {{ {} }}", kPrefix, t.size); },
                              [&](VarBinary const& t) { return std::format("{}VarBinary {{ {} }}", kPrefix, t.size); },
                              [&](Decimal const& t) {
                                  return std::format(
                                      "{}Decimal {{ .precision = {}, .scale = {} }}", kPrefix, t.precision, t.scale);
                              },
                          },
                          type);
    }

    /// @brief Render one CREATE TABLE call into the output buffer. Uses the
    /// `plan.CreateTable("name").Column(...)...` chained-builder shape.
    void AppendCreateTable(std::string& out,
                           SqlSchema::FullyQualifiedTableName const& key,
                           SqlMigration::MigrationManager::PlanFoldingResult::TableState const& state)
    {
        out += "    {\n";
        out += std::format("        auto t = plan.CreateTable{}(\"{}\");\n",
                           state.ifNotExists ? "IfNotExists" : "",
                           EscapeForCpp(key.table));
        for (auto const& col: state.columns)
        {
            out += std::format("        t.Column(\"{}\", {}", EscapeForCpp(col.name), ColumnTypeToCpp(col.type));
            if (col.required)
                out += ", ::Lightweight::SqlNullable::NotNull";
            out += ");\n";
        }
        for (auto const& fk: state.compositeForeignKeys)
        {
            out += "        // FK: ";
            for (auto const& c: fk.columns)
            {
                out += c;
                out += ' ';
            }
            out += std::format("→ {}(", fk.referencedTableName);
            for (auto const& c: fk.referencedColumns)
            {
                out += c;
                out += ' ';
            }
            out += ")\n";
        }
        out += "    }\n";
    }

    /// @brief Render one CREATE INDEX call.
    void AppendCreateIndex(std::string& out, SqlCreateIndexPlan const& idx)
    {
        out +=
            std::format(R"(    plan.CreateIndex("{}", "{}", {{ )", EscapeForCpp(idx.indexName), EscapeForCpp(idx.tableName));
        bool first = true;
        for (auto const& col: idx.columns)
        {
            if (!first)
                out += ", ";
            out += std::format("\"{}\"", EscapeForCpp(col));
            first = false;
        }
        out += " }";
        if (idx.unique)
            out += ", true";
        out += ");\n";
    }

    /// @brief Render an SqlVariant value as a C++ literal expression.
    ///
    /// Every alternative of `SqlVariant::InnerType` is matched explicitly so adding a
    /// new variant member becomes a compile-time prompt to decide how it should be
    /// emitted, rather than silently falling through to a null literal.
    std::string VariantToCpp(SqlVariant const& v)
    {
        auto const intLit = [](auto val) {
            return std::format("static_cast<int64_t>({})", val);
        };
        auto const strLit = [](std::string_view s) {
            return std::format("std::string{{\"{}\"}}", EscapeForCpp(s));
        };
        return std::visit(overloaded {
                              [](SqlNullType const&) -> std::string { return "{}"; },
                              [](bool b) -> std::string { return b ? "true" : "false"; },
                              [&](int8_t v) { return intLit(v); },
                              [&](short v) { return intLit(v); },
                              [&](unsigned short v) { return intLit(v); },
                              [&](int v) { return intLit(v); },
                              [&](unsigned int v) { return intLit(v); },
                              [&](long long v) { return intLit(v); },
                              [&](unsigned long long v) { return intLit(v); },
                              [](float v) { return std::format("{}", v); },
                              [](double v) { return std::format("{}", v); },
                              [&](std::string const& s) { return strLit(s); },
                              [&](std::string_view s) { return strLit(s); },
                              // Wide / typed values are not yet expressible as compile-time C++ DSL
                              // literals here; they are intentionally rendered as null. Adding any
                              // of these requires a deliberate emitter mapping.
                              [](std::u16string const&) -> std::string { return "{}"; },
                              [](std::u16string_view) -> std::string { return "{}"; },
                              [](SqlGuid const&) -> std::string { return "{}"; },
                              [](SqlText const&) -> std::string { return "{}"; },
                              [](SqlDate const&) -> std::string { return "{}"; },
                              [](SqlTime const&) -> std::string { return "{}"; },
                              [](SqlDateTime const&) -> std::string { return "{}"; },
                          },
                          v.value);
    }

    /// @brief Render an INSERT data plan as `plan.Insert("T").Set(...)...` chain.
    void AppendInsert(std::string& out, SqlInsertDataPlan const& step)
    {
        out += "    {\n";
        out += std::format("        auto ins = plan.Insert(\"{}\");\n", EscapeForCpp(step.tableName));
        for (auto const& [col, val]: step.columns)
            out += std::format("        ins.Set(\"{}\", {});\n", EscapeForCpp(col), VariantToCpp(val));
        out += "    }\n";
    }

    /// @brief Render an UPDATE data plan.
    void AppendUpdate(std::string& out, SqlUpdateDataPlan const& step)
    {
        out += "    {\n";
        out += std::format("        auto upd = plan.Update(\"{}\");\n", EscapeForCpp(step.tableName));
        for (auto const& [col, val]: step.setColumns)
            out += std::format("        upd.Set(\"{}\", {});\n", EscapeForCpp(col), VariantToCpp(val));
        if (!step.whereColumn.empty())
            out += std::format("        upd.Where(\"{}\", \"{}\", {});\n",
                               EscapeForCpp(step.whereColumn),
                               EscapeForCpp(step.whereOp),
                               VariantToCpp(step.whereValue));
        out += "    }\n";
    }

    /// @brief Render a DELETE data plan.
    void AppendDelete(std::string& out, SqlDeleteDataPlan const& step)
    {
        out += "    {\n";
        out += std::format("        auto del = plan.Delete(\"{}\");\n", EscapeForCpp(step.tableName));
        if (!step.whereColumn.empty())
            out += std::format("        del.Where(\"{}\", \"{}\", {});\n",
                               EscapeForCpp(step.whereColumn),
                               EscapeForCpp(step.whereOp),
                               VariantToCpp(step.whereValue));
        out += "    }\n";
    }

    /// @brief Render a RawSql data step.
    void AppendRawSql(std::string& out, SqlRawSqlPlan const& step)
    {
        out += std::format("    plan.RawSql(R\"_lw_({})_lw_\");\n", EscapeForRawCpp(step.sql));
    }

    /// @brief Render one data step grouped by source migration.
    void AppendDataStep(std::string& out,
                        SqlMigration::MigrationManager::PlanFoldingResult::DataStep const& step,
                        SqlMigration::MigrationTimestamp& lastTs)
    {
        if (step.sourceTimestamp != lastTs)
        {
            out += std::format("    // From migration {}: {}\n", step.sourceTimestamp.value, step.sourceTitle);
            lastTs = step.sourceTimestamp;
        }
        // Schema-shape elements (CreateTable / AlterTable / DropTable / CreateIndex) are
        // emitted by the schema-section path, not as data steps — list them explicitly as
        // no-ops so `std::visit` requires every variant alternative to be handled here.
        std::visit(overloaded {
                       [&](SqlInsertDataPlan const& s) { AppendInsert(out, s); },
                       [&](SqlUpdateDataPlan const& s) { AppendUpdate(out, s); },
                       [&](SqlDeleteDataPlan const& s) { AppendDelete(out, s); },
                       [&](SqlRawSqlPlan const& s) { AppendRawSql(out, s); },
                       [](SqlCreateTablePlan const&) {},
                       [](SqlAlterTablePlan const&) {},
                       [](SqlDropTablePlan const&) {},
                       [](SqlCreateIndexPlan const&) {},
                   },
                   step.element);
    }

    /// @brief Builds a single text block per logical unit, suitable for bin-packing.
    std::vector<CodeGen::CodeBlock> BuildBodyBlocks(FoldResult const& fold)
    {
        std::vector<CodeGen::CodeBlock> blocks;

        auto const pushBlock = [&](std::string content) {
            auto const lineCount = static_cast<std::size_t>(std::ranges::count(content, '\n'));
            blocks.push_back(CodeGen::CodeBlock {
                .content = std::move(content),
                .lineCount = lineCount,
            });
        };

        // Schema-migrations empty guard.
        {
            std::string out;
            out += "    // Hard-fail when schema_migrations already has rows. Operators must\n";
            out += "    // run `dbtool hard-reset` (clean re-deploy) or `dbtool mark-applied` to\n";
            out += "    // stamp the baseline as already-applied without execution.\n";
            out += "    plan.RawSql(R\"_lw_(\n";
            out += "        -- Baseline empty-schema_migrations check\n";
            out += "        -- (the actual probe is database-specific; if you reach this with\n";
            out += "        -- existing rows you must reset or stamp the baseline before applying.)\n";
            out += "    )_lw_\");\n\n";
            pushBlock(std::move(out));
        }

        // Tables.
        for (auto const& key: fold.creationOrder)
        {
            std::string out;
            AppendCreateTable(out, key, fold.tables.at(key));
            pushBlock(std::move(out));
        }

        // Indexes.
        for (auto const& idx: fold.indexes)
        {
            std::string out;
            AppendCreateIndex(out, idx);
            pushBlock(std::move(out));
        }

        // Data steps.
        SqlMigration::MigrationTimestamp lastTs { 0 };
        for (auto const& step: fold.dataSteps)
        {
            std::string out;
            AppendDataStep(out, step, lastTs);
            pushBlock(std::move(out));
        }

        return blocks;
    }

    /// @brief Render a single emit-everything-in-one-file `.cpp` body, wrapped in the
    /// `LIGHTWEIGHT_SQL_MIGRATION` macro and `LIGHTWEIGHT_SQL_RELEASE` markers.
    std::string BuildSingleFileBody(FoldResult const& fold)
    {
        std::string out;
        out += "// SPDX-License-Identifier: Apache-2.0\n";
        out += "// Auto-generated by `dbtool fold`. DO NOT EDIT.\n";
        out += "//\n";
        out += std::format("// Folded migrations: {}\n", fold.foldedMigrations.size());
        if (!fold.foldedMigrations.empty())
        {
            out += std::format(
                "// First: {} - {}\n", fold.foldedMigrations.front().first.value, fold.foldedMigrations.front().second);
            out += std::format(
                "// Last:  {} - {}\n", fold.foldedMigrations.back().first.value, fold.foldedMigrations.back().second);
        }
        out += '\n';
        out += "#include <Lightweight/SqlMigration.hpp>\n";
        out += '\n';
        out += "using namespace ::Lightweight;\n";
        out += "using namespace ::Lightweight::SqlMigration;\n";
        out += '\n';

        auto const baselineTs = fold.foldedMigrations.empty() ? 0 : fold.foldedMigrations.back().first.value;
        out += std::format("LIGHTWEIGHT_SQL_MIGRATION({}, \"Folded baseline\")\n", baselineTs);
        out += "{\n";
        for (auto const& block: BuildBodyBlocks(fold))
            out += block.content;
        out += "}\n";
        out += '\n';
        for (auto const& release: fold.releases)
            out += std::format(
                "LIGHTWEIGHT_SQL_RELEASE(\"{}\", {});\n", EscapeForCpp(release.version), release.highestTimestamp.value);
        return out;
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
