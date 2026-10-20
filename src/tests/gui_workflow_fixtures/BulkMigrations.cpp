// SPDX-License-Identifier: Apache-2.0
//
// Fixture plugin that registers LIGHTWEIGHT_TEST_BULK_MIGRATIONS migrations (none when unset), each
// running `SELECT 1`, so tests can load a plugin with many migrations. Its file name lacks the `Plugin`
// suffix, so plugin discovery skips it; tests opt in by copying.

#include <Lightweight/SqlMigration.hpp>
#include <Lightweight/SqlQuery/Migrate.hpp>

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <format>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>

#if defined(_MSC_VER)
    // C4996 flags getenv as unsafe; the plugin only reads test-harness variables.
    #pragma warning(disable : 4996)
#endif

using namespace Lightweight;

LIGHTWEIGHT_MIGRATION_PLUGIN()

namespace
{

constexpr std::uint64_t FirstBulkTimestamp = 20240601000001;

/// A migration with a timestamp chosen at run time; its title is owned by `BulkRegistry`.
class BulkMigration final: public SqlMigration::MigrationBase
{
  public:
    BulkMigration(std::uint64_t timestamp, std::string const& title):
        SqlMigration::MigrationBase(SqlMigration::MigrationTimestamp { timestamp }, title)
    {
    }

    BulkMigration(BulkMigration const&) = delete;
    BulkMigration(BulkMigration&&) = delete;
    BulkMigration& operator=(BulkMigration const&) = delete;
    BulkMigration& operator=(BulkMigration&&) = delete;
    ~BulkMigration() override = default;

    void Up(SqlMigrationQueryBuilder& plan) const override
    {
        plan.RawSql("SELECT 1");
    }
};

/// The requested migration count; 0 when the variable is unset or not a non-negative number.
int RequestedCount()
{
    auto const* const value = std::getenv("LIGHTWEIGHT_TEST_BULK_MIGRATIONS");
    if (value == nullptr)
        return 0;
    auto const text = std::string_view { value };
    auto count = 0;
    auto const [end, error] = std::from_chars(text.data(), text.data() + text.size(), count);
    return error == std::errc {} && end == text.data() + text.size() && count >= 0 ? count : 0;
}

/// Owns the titles and the migrations; members are destroyed in reverse order, so the titles outlive the migrations.
struct BulkRegistry
{
    std::deque<std::string> titles;
    std::deque<BulkMigration> migrations;

    BulkRegistry()
    {
        for (auto const index: std::views::iota(0, RequestedCount()))
        {
            titles.push_back(std::format("Bulk migration {}", index + 1));
            migrations.emplace_back(FirstBulkTimestamp + static_cast<std::uint64_t>(index), titles.back());
        }
    }
};

BulkRegistry const bulkRegistry;

} // namespace
