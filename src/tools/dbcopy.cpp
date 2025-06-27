// SPDX-License-Identifier: Apache-2.0

#include "utils.hpp"

#include <Lightweight/Lightweight.hpp>

#include <print>

using namespace std::string_view_literals;

struct Configuration
{
    std::string from;
    std::string to;
    bool help = false;
    bool quiet = false;
};

namespace
{

void PrintHelp()
{
    std::println("dbcopy [--help] [--quiet] --from <DSN> --to <DSN>");
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
    if (!ParseCommandLineArguments(&config, argc, argv))
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
        SqlSchema::ReadAllTables(sourceStmt, {}, {}, [](std::string_view table, size_t current, size_t total) {
            std::println("Processing table {}/{}: {}", current, total, table);
        });
    std::println("Read {} tables from source database", tableSchemas.size());

    auto const createTableMigrationPlan = MakeCreateTablePlan(tableSchemas);
    auto const sqlTargetServerType = SqlServerType::SQLITE; // TODO(pr) determine automatically
    auto const& formatter = *SqlQueryFormatter::Get(sqlTargetServerType);
    auto const sqlQueries = ToSql(formatter, createTableMigrationPlan);

    for (auto const& sqlQuery: sqlQueries)
    {
        std::println("{}\n", sqlQuery);
    }

    // auto targetConnection = SqlConnection { SqlConnectionString { config.to } };

    return EXIT_SUCCESS;
}
