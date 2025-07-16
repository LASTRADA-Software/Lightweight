// SPDX-License-Identifier: Apache-2.0

#include "utils.hpp"

#include <Lightweight/Lightweight.hpp>

#include <print>

using namespace std::string_view_literals;

struct Configuration
{
    std::string from;
    std::string schema;
    std::string to;
    bool help = false;
    bool quiet = false;
};

namespace
{

void PrintHelp()
{
    std::println("dbcopy [--help] [--quiet] --from=DSN [--schema=NAME] --to=DSN");
    std::println();
}

std::vector<std::string> ToSql(SqlQueryFormatter const& formatter,
                               std::vector<SqlCreateTablePlan> const& createTableMigrationPlan)
{
    auto result = std::vector<std::string> {};
    for (SqlCreateTablePlan const& createTablePlan: createTableMigrationPlan)
        std::ranges::move(ToSql(formatter, SqlMigrationPlanElement { createTablePlan }), std::back_inserter(result));
    return result;
}

} // end namespace

int main(int argc, char const* argv[])
{
    auto config = Configuration {};
    if (!ParseCommandLineArguments(&config, argc, argv) && !config.help)
    {
        std::println("Error: Invalid command line arguments");
        return EXIT_FAILURE;
    }

    if (config.help)
    {
        PrintHelp();
        return EXIT_SUCCESS;
    }

    auto const sourceConnectionString = SqlConnectionString { config.from };
    auto sourceConnection = SqlConnection { sourceConnectionString };

    SqlStatement sourceStmt { sourceConnection };
    SqlSchema::TableList tableSchemas =
        SqlSchema::ReadAllTables(sourceStmt, {}, config.schema, [&](std::string_view table, size_t current, size_t total) {
            if (!config.quiet)
                std::println("Processing table {}/{}: {}", current, total, table);
        });

    if (!config.quiet)
        std::println("Read {} tables from source database", tableSchemas.size());

    auto const createTableMigrationPlan = MakeCreateTablePlan(tableSchemas);

    if (config.to.empty() || config.to == "-"sv)
    {
        // Dump all SQL queries to stdout for now
        std::println("SQL queries for target database:");
        auto const sqlTargetServerType = SqlServerType::SQLITE; // TODO(pr) determine automatically
        auto const& formatter = *SqlQueryFormatter::Get(sqlTargetServerType);
        auto const sqlQueries = ToSql(formatter, createTableMigrationPlan);
        for (auto const& sqlQuery: sqlQueries)
            std::println("{}\n", sqlQuery);
    }
    else
    {
        // Implement writing to target database
        auto targetConnection = SqlConnection { SqlConnectionString { config.to } };
        auto const sqlQueries = ToSql(targetConnection.QueryFormatter(), createTableMigrationPlan);

        // TODO(pr): Execute the SQL queries on the target connection
    }

    // TODO(pr): Copy data from source to target database

    return EXIT_SUCCESS;
}
