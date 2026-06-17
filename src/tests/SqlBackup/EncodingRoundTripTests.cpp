// SPDX-License-Identifier: Apache-2.0

#include "TestHelpers.hpp"

#include <Lightweight/SqlBackup.hpp>
#include <Lightweight/SqlColumnTypeDefinitions.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlStatement.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

using namespace Lightweight;
using Lightweight::SqlBackup::Tests::LambdaProgressManager;
using Lightweight::SqlBackup::Tests::ScopedFileRemoved;

namespace
{

std::filesystem::path const EncodingBackupFile = "encoding_roundtrip_backup.zip";

SqlConnectionString const& GetConnectionString()
{
    return SqlConnection::DefaultConnectionString();
}

// Pure ASCII in a narrow (ASCII-typed) column.
std::string const AsciiValue = "Plain ASCII text 0123456789 !?.,;:";

// Non-ASCII bytes living in a *narrow* (Varchar) column — the "code page inside an ASCII type"
// case. These bytes are the UTF-8 encoding of "café €" and are simultaneously a sequence of
// individually-defined Windows-1252 code points, so the cell round-trips byte-for-byte whether the
// backend stores the column as UTF-8 (SQLite, PostgreSQL) or as a single-byte code page (MS SQL
// Server VARCHAR with its CP1252 collation). The backup decode path reads such narrow columns as
// raw SQL_C_CHAR bytes and the restore path writes them back unchanged, so this asserts the bytes
// are never silently transcoded or truncated.
std::string const CodepageValue = "caf\xC3\xA9 \xE2\x82\xAC"; // "café €" as UTF-8 / CP1252 bytes

// Genuine Unicode in a *wide* (NVarchar) column — the UTF-8 path. Mixes BMP umlauts with a
// supplementary-plane emoji (surrogate pair) so the UTF-16 <-> UTF-8 conversion in both the backup
// decode and the restore re-encode is exercised on multi-unit code points.
std::u16string const WideValue = u"Grüße — café 😀"; // U+1F600 is a surrogate pair in UTF-16

} // namespace

// Backs up and restores a table carrying one cell of each text-encoding class and asserts the cell
// values survive the round-trip exactly (not merely that the row count is preserved). Covers:
//   - ASCII text in a narrow Varchar column,
//   - non-ASCII / code-page bytes in a narrow Varchar column (byte-exact), and
//   - Unicode (incl. a supplementary-plane code point) in a wide NVarchar column.
TEST_CASE("SqlBackup: round-trip preserves ASCII / code-page / Unicode text", "[SqlBackup][Encoding]")
{
    using namespace SqlColumnTypeDefinitions;

    auto const backupFileCleaner = ScopedFileRemoved { EncodingBackupFile };

    // Create the table and insert the row through a dedicated connection (mirrors the working
    // "Complex Types" round-trip; keeping setup, backup, drop and restore on separate connections
    // avoids the catalog-visibility quirk that forces other tests to skip MS SQL Server here).
    {
        SqlConnection conn;
        REQUIRE(conn.Connect(GetConnectionString()));
        SqlStatement stmt { conn };

        stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
            migration.DropTableIfExists("encoding_rt");
            migration.CreateTable("encoding_rt")
                .PrimaryKey("id", Integer {})
                .Column("ascii_v", Varchar { 64 })
                .Column("cp_v", Varchar { 64 })
                .Column("wide_v", NVarchar { 64 });
        });

        stmt.Prepare(R"(INSERT INTO "encoding_rt" ("id", "ascii_v", "cp_v", "wide_v") VALUES (?, ?, ?, ?))");
        (void) stmt.Execute(1, AsciiValue, CodepageValue, WideValue);
    }

    LambdaProgressManager pm { [](SqlBackup::Progress const& p) {
        if (p.state == SqlBackup::Progress::State::Error)
            FAIL_CHECK("Backup/Restore error: " << p.message);
    } };

    REQUIRE_NOTHROW(SqlBackup::Backup(EncodingBackupFile, GetConnectionString(), 1, pm));
    REQUIRE(std::filesystem::exists(EncodingBackupFile));
    REQUIRE(std::filesystem::file_size(EncodingBackupFile) > 0);

    // Drop the table so Restore has to recreate it from the backup's metadata and re-insert the row.
    {
        SqlConnection conn;
        REQUIRE(conn.Connect(GetConnectionString()));
        SqlStatement stmt { conn };
        stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) { migration.DropTable("encoding_rt"); });
    }

    REQUIRE_NOTHROW(SqlBackup::Restore(EncodingBackupFile, GetConnectionString(), 1, pm));

    // Read every cell back and compare to the originals.
    {
        SqlConnection conn;
        REQUIRE(conn.Connect(GetConnectionString()));
        SqlStatement stmt { conn };

        // Compare the optional directly to the expected value: this is null-safe (a NULL/absent
        // cell never compares equal to a populated expected value) and matches the project's
        // existing round-trip assertions.
        auto const count = stmt.ExecuteDirectScalar<int64_t>(R"(SELECT COUNT(*) FROM "encoding_rt")");
        CHECK(count == 1);

        auto const ascii = stmt.ExecuteDirectScalar<std::string>(R"(SELECT "ascii_v" FROM "encoding_rt" WHERE "id" = 1)");
        CHECK(ascii == AsciiValue);

        // Byte-exact: the narrow column must come back with the same raw bytes it went in with.
        auto const cp = stmt.ExecuteDirectScalar<std::string>(R"(SELECT "cp_v" FROM "encoding_rt" WHERE "id" = 1)");
        CHECK(cp == CodepageValue);

        auto const wide = stmt.ExecuteDirectScalar<std::u16string>(R"(SELECT "wide_v" FROM "encoding_rt" WHERE "id" = 1)");
        CHECK(wide == WideValue);
    }

    {
        SqlConnection conn;
        REQUIRE(conn.Connect(GetConnectionString()));
        SqlStatement stmt { conn };
        stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) { migration.DropTableIfExists("encoding_rt"); });
    }
}
