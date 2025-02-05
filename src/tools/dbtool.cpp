// SPDX-License-Identifier: Apache-2.0

#include "utils.hpp"

#include <Lightweight/SqlColumnTypeDefinitions.hpp>
#include <Lightweight/SqlMigration.hpp>
#include <Lightweight/SqlQuery.hpp>
#include <Lightweight/SqlSchema.hpp>

#include <cstdlib>
#include <iostream>

int showHelp(int /*argc*/, char const* /*argv*/[])
{
    // DRAFT idea on how the CLI utility could look like
    // clang-format off
    std::cout << "Usage: dbtool <COMMAND ...>\n"
                 "\n"
                 "dbtool is a tool for managing database migrations in C++ projects.\n"
                 "\n"
                 "  dbtool help                   - prints this help message\n"
                 "  dbtool version                - prints the version of dbtool\n"
                 "\n"
                 "  Migration tasks\n"
                 "\n"
                 "      dbtool migrate                - applies pending migrations to the database\n"
                 "      dbtool list-pending           - lists pending migrations\n"
                 "      dbtool list-applied           - lists applied migrations\n"
                 "      dbtool create <NAME>          - creates a new migration with the given name\n"
                 "      dbtool apply <NAME>           - applies the migration with the given name\n"
                 "      dbtool rollback <NAME>        - rolls back the migration with the given name\n"
                 "\n"
                 "      Options:\n"
                 "\n"
                 "        -m, --module <MODULE_NAME>    - specifies the module name (DLL or .so file) to load migrations from\n"
                 "                                        Migration libraries are loaded automatically relative to the executables path\n"
                 "\n"
                 "  Databas eadministration tasks\n"
                 "\n"
                 "      dbtool dump-schema <DSN>        - Dumps the schema of a given database to stdout\n"
                 "      dbtool backup <DSN> to <FILE>   - Creates a backup of a given database\n"
                 "      dbtool restore <FILE> to <DSN>  - Restores a database from a backup file\n"
                 "\n"
                 "Examples:\n"
                 "  dbtool create add_users_table\n"
                 "  dbtool apply add_users_table\n"
                 "  dbtool rollback add_users_table\n"
                 "  dbtool migrate\n"
                 "\n";
    // clang-format on
    return EXIT_SUCCESS;
}

using namespace std::string_literals;
using namespace std::string_view_literals;

struct DumpSchemaConfiguration
{
    std::string connectionString;
    std::string database = "testdb";
    std::optional<std::string> schema;
    bool help = false;
};

int dumpSchema(int argc, char const* argv[])
{
    auto config = DumpSchemaConfiguration {};
    if (!parseCommandLineArguments(&config, argc, argv))
        return EXIT_FAILURE;

    if (config.help)
        return showHelp(argc, argv);

    SqlConnection::SetDefaultConnectionString(SqlConnectionString { config.connectionString });

    auto const* dialect = &SqlQueryFormatter::SqlServer(); // TODO: configurable

    auto connection = SqlConnection {};

    auto createStructureStmts =
        SqlSchema::BuildStructureFromSchema(connection, config.schema.value_or(""), *dialect);
    auto const planSql = createStructureStmts.GetPlan().ToSql();
    for (auto const& sql: planSql)
        std::cout << sql << '\n';

    return EXIT_SUCCESS;
}

auto const operationMap = std::map<std::string_view, int (*)(int, char const*[])> {
    { "help"sv, showHelp },
    { "--help"sv, showHelp },
    { "-h"sv, showHelp },
    { "dump-schema"sv, dumpSchema },
};

int main(int argc, char const* argv[])
{
    if (argc <= 1)
    {
        showHelp(argc, argv);
        return EXIT_FAILURE;
    }

    auto const operationName = std::string_view(argv[1]);
    if (auto const entry = operationMap.find(operationName); entry != operationMap.end())
        return entry->second(argc - 1, argv + 1);
    else
        std::cerr << "Unknown command: " << argv[1] << '\n';

    return EXIT_FAILURE;
}
