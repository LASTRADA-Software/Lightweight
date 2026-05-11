// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/SqlColumnTypeDefinitions.hpp>
#include <Lightweight/SqlError.hpp>
#include <Lightweight/SqlLogger.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <ranges>
#include <source_location>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include <sql.h>
#include <sqlext.h>

using namespace Lightweight;

// ================================================================================================
// SqlLogger::Null logger
// ================================================================================================

TEST_CASE("SqlLogger::NullLogger swallows everything", "[SqlLogger]")
{
    auto& nullLogger = SqlLogger::NullLogger();

    SECTION("warnings, errors, fetch lifecycle accept calls without crashing")
    {
        nullLogger.OnWarning("a warning");
        SqlLogger& base = nullLogger;
        base.OnError(SqlError::FAILURE);
        SqlErrorInfo const info { .nativeErrorCode = 42, .sqlState = "08001", .message = "boom" };
        base.OnError(info);
        nullLogger.OnExecuteDirect("SELECT 1");
        nullLogger.OnPrepare("SELECT 1");
        nullLogger.OnBind("p", "v");
        nullLogger.OnExecute("SELECT 1");
        nullLogger.OnExecuteBatch();
        nullLogger.OnFetchRow();
        nullLogger.OnFetchEnd();
        nullLogger.OnScopedTimerStart("tag");
        nullLogger.OnScopedTimerStop("tag");
    }
}

// ================================================================================================
// SqlLogger::SetLogger / GetLogger round-trip
// ================================================================================================

namespace
{

class CapturingLogger: public SqlLogger::Null
{
  public:
    std::vector<std::string> warnings;
    std::vector<SqlErrorInfo> errorInfos;
    std::vector<SqlError> errors;
    std::vector<std::string> directQueries;
    std::vector<std::string> preparedQueries;
    std::vector<std::pair<std::string, std::string>> binds;
    int rowCount = 0;
    int fetchEnded = 0;
    int batchExecuted = 0;
    std::vector<std::string> startedTimers;
    std::vector<std::string> stoppedTimers;

    void OnWarning(std::string_view const& message) override
    {
        warnings.emplace_back(message);
    }
    void OnError(SqlError errorCode, std::source_location /*loc*/) override
    {
        errors.push_back(errorCode);
    }
    void OnError(SqlErrorInfo const& errorInfo, std::source_location /*loc*/) override
    {
        errorInfos.push_back(errorInfo);
    }
    void OnExecuteDirect(std::string_view const& query) override
    {
        directQueries.emplace_back(query);
    }
    void OnPrepare(std::string_view const& query) override
    {
        preparedQueries.emplace_back(query);
    }
    void OnBind(std::string_view const& name, std::string value) override
    {
        binds.emplace_back(std::string { name }, std::move(value));
    }
    void OnExecute(std::string_view const& query) override
    {
        preparedQueries.emplace_back(query);
    }
    void OnExecuteBatch() override
    {
        ++batchExecuted;
    }
    void OnFetchRow() override
    {
        ++rowCount;
    }
    void OnFetchEnd() override
    {
        ++fetchEnded;
    }
    void OnScopedTimerStart(std::string const& tag) override
    {
        startedTimers.push_back(tag);
    }
    void OnScopedTimerStop(std::string const& tag) override
    {
        stoppedTimers.push_back(tag);
    }
};

struct LoggerSwap
{
    SqlLogger* previous;
    explicit LoggerSwap(SqlLogger& replacement):
        previous { &SqlLogger::GetLogger() }
    {
        SqlLogger::SetLogger(replacement);
    }
    ~LoggerSwap()
    {
        SqlLogger::SetLogger(*previous);
    }
    LoggerSwap(LoggerSwap const&) = delete;
    LoggerSwap& operator=(LoggerSwap const&) = delete;
    LoggerSwap(LoggerSwap&&) = delete;
    LoggerSwap& operator=(LoggerSwap&&) = delete;
};

} // namespace

TEST_CASE("SqlLogger::SetLogger / GetLogger swap", "[SqlLogger]")
{
    CapturingLogger capture;
    LoggerSwap const swap { capture };

    REQUIRE(&SqlLogger::GetLogger() == &capture);
    SqlLogger::GetLogger().OnWarning("hello");
    SqlLogger::GetLogger().OnFetchRow();
    SqlLogger::GetLogger().OnFetchEnd();

    CHECK(capture.warnings == std::vector<std::string> { "hello" });
    CHECK(capture.rowCount == 1);
    CHECK(capture.fetchEnded == 1);
}

// ================================================================================================
// SqlScopedTimeLogger forwards to current logger
// ================================================================================================

TEST_CASE("SqlScopedTimeLogger pairs start / stop on the current logger", "[SqlLogger]")
{
    CapturingLogger capture;
    LoggerSwap const swap { capture };

    {
        SqlScopedTimeLogger const timer { "compute" };
        CHECK(capture.startedTimers == std::vector<std::string> { "compute" });
        CHECK(capture.stoppedTimers.empty());
    }

    CHECK(capture.stoppedTimers == std::vector<std::string> { "compute" });
}

// ================================================================================================
// SqlLogger::StandardLogger / TraceLogger via custom MessageWriter
// ================================================================================================

TEST_CASE("SqlLogger::StandardLogger emits formatted error messages via the configured sink", "[SqlLogger]")
{
    auto& logger = SqlLogger::StandardLogger();
    std::vector<std::string> captured;
    logger.SetLoggingSink([&](std::string message) { captured.push_back(std::move(message)); });

    logger.OnWarning("this is a warning");
    logger.OnError(SqlError::FAILURE, std::source_location::current());
    SqlErrorInfo const info { .nativeErrorCode = 99, .sqlState = "23505", .message = "duplicate key" };
    logger.OnError(info, std::source_location::current());

    REQUIRE(captured.size() >= 3);
    CHECK(captured[0].contains("this is a warning"));
    CHECK(captured[1].contains("SQL_ERROR"));
    CHECK(captured[2].contains("23505"));
    CHECK(captured[2].contains("99"));
    CHECK(captured[2].contains("duplicate key"));

    logger.SetLoggingSink({});
}

TEST_CASE("SqlLogger::TraceLogger logs scoped timers and prepare/execute", "[SqlLogger]")
{
    auto& logger = SqlLogger::TraceLogger();
    std::vector<std::string> captured;
    logger.SetLoggingSink([&](std::string message) { captured.push_back(std::move(message)); });

    logger.OnScopedTimerStart("region");
    logger.OnScopedTimerStop("region");

    logger.OnPrepare("SELECT * FROM T WHERE id = ?");
    logger.OnBind("id", "42");
    logger.OnExecute("SELECT * FROM T WHERE id = ?");
    logger.OnFetchRow();
    logger.OnFetchEnd();

    auto containsAny = [&](std::string_view needle) {
        return std::ranges::any_of(captured, [&](std::string const& m) { return m.contains(needle); });
    };

    CHECK(containsAny("Scoped timer started"));
    CHECK(containsAny("Scoped timer finished"));
    CHECK(containsAny("SELECT * FROM T WHERE id = ?"));
    CHECK(containsAny("[1 row]"));

    logger.SetLoggingSink({});
}

TEST_CASE("SqlLogger::TraceLogger emits '[N rows]' plural form past the first row", "[SqlLogger]")
{
    auto& logger = SqlLogger::TraceLogger();
    std::vector<std::string> captured;
    logger.SetLoggingSink([&](std::string message) { captured.push_back(std::move(message)); });

    logger.OnExecute("SELECT * FROM T");
    logger.OnFetchRow();
    logger.OnFetchRow();
    logger.OnFetchRow();
    logger.OnFetchEnd();

    auto containsAny = [&](std::string_view needle) {
        return std::ranges::any_of(captured, [&](std::string const& m) { return m.contains(needle); });
    };
    CHECK(containsAny("[3 rows]"));
    CHECK_FALSE(containsAny("[1 row]"));

    logger.SetLoggingSink({});
}

TEST_CASE("SqlLogger::TraceLogger appends 'WITH [...]' when binds are present", "[SqlLogger]")
{
    auto& logger = SqlLogger::TraceLogger();
    std::vector<std::string> captured;
    logger.SetLoggingSink([&](std::string message) { captured.push_back(std::move(message)); });

    logger.OnPrepare("SELECT * FROM T WHERE id = ? AND name = ?");
    logger.OnBind("id", "42");
    // Positional bind — empty name, formatter must render just the value, not `name=value`.
    logger.OnBind("", "'foo'");
    logger.OnExecute("SELECT * FROM T WHERE id = ? AND name = ?");
    logger.OnFetchEnd();

    REQUIRE_FALSE(captured.empty());
    auto const& last = captured.back();
    CHECK(last.contains("WITH ["));
    CHECK(last.contains("id=42"));
    CHECK(last.contains("'foo'"));
    // The positional bind must NOT include a "=" with empty name.
    CHECK_FALSE(last.contains(", =")); // no name on positional bind
}

TEST_CASE("SqlLogger::TraceLogger.OnExecuteBatch writes the executed query upfront", "[SqlLogger]")
{
    auto& logger = SqlLogger::TraceLogger();
    std::vector<std::string> captured;
    logger.SetLoggingSink([&](std::string message) { captured.push_back(std::move(message)); });

    logger.OnPrepare("INSERT INTO T VALUES (?, ?)");
    logger.OnExecuteBatch();
    logger.OnFetchEnd();

    auto containsAny = [&](std::string_view needle) {
        return std::ranges::any_of(captured, [&](std::string const& m) { return m.contains(needle); });
    };
    CHECK(containsAny("ExecuteBatch"));
    CHECK(containsAny("INSERT INTO T VALUES (?, ?)"));
}

TEST_CASE("SqlLogger::TraceLogger.OnExecuteDirect flushes the previous execute", "[SqlLogger]")
{
    auto& logger = SqlLogger::TraceLogger();
    std::vector<std::string> captured;
    logger.SetLoggingSink([&](std::string message) { captured.push_back(std::move(message)); });

    logger.OnExecute("SELECT 1");
    logger.OnFetchRow();
    // No OnFetchEnd between — OnExecuteDirect must auto-flush.
    logger.OnExecuteDirect("SELECT 2");
    logger.OnFetchEnd();

    auto countContaining = [&](std::string_view needle) {
        return std::ranges::count_if(captured, [&](std::string const& m) { return m.contains(needle); });
    };
    CHECK(countContaining("SELECT 1") >= 1);
    CHECK(countContaining("SELECT 2") >= 1);
}

TEST_CASE("SqlLogger::TraceLogger.OnPrepare flushes a still-running execute", "[SqlLogger]")
{
    auto& logger = SqlLogger::TraceLogger();
    std::vector<std::string> captured;
    logger.SetLoggingSink([&](std::string message) { captured.push_back(std::move(message)); });

    logger.OnExecute("SELECT * FROM T");
    logger.OnFetchRow();
    // OnPrepare arriving mid-fetch must flush the prior execute first.
    logger.OnPrepare("INSERT INTO U VALUES (?)");
    logger.OnExecute("INSERT INTO U VALUES (?)");
    logger.OnFetchEnd();

    auto countContaining = [&](std::string_view needle) {
        return std::ranges::count_if(captured, [&](std::string const& m) { return m.contains(needle); });
    };
    CHECK(countContaining("SELECT * FROM T") >= 1);
    CHECK(countContaining("INSERT INTO U VALUES (?)") >= 1);
}

TEST_CASE("SqlLogger::TraceLogger.OnError logs the error and includes source-location details", "[SqlLogger]")
{
    auto& logger = SqlLogger::TraceLogger();
    std::vector<std::string> captured;
    logger.SetLoggingSink([&](std::string message) { captured.push_back(std::move(message)); });

    // Set up a prior prepared query so WriteDetails has something to attach.
    logger.OnPrepare("SELECT * FROM Broken");
    logger.OnError(SqlError::FAILURE, std::source_location::current());

    auto containsAny = [&](std::string_view needle) {
        return std::ranges::any_of(captured, [&](std::string const& m) { return m.contains(needle); });
    };
    CHECK(containsAny("SQL_ERROR"));
    CHECK(containsAny("Source:"));
}

TEST_CASE("SqlLogger::TraceLogger.OnError(SqlErrorInfo) logs SQLSTATE and message", "[SqlLogger]")
{
    auto& logger = SqlLogger::TraceLogger();
    std::vector<std::string> captured;
    logger.SetLoggingSink([&](std::string message) { captured.push_back(std::move(message)); });

    SqlErrorInfo const info { .nativeErrorCode = 17, .sqlState = "08006", .message = "connection lost" };
    logger.OnError(info, std::source_location::current());

    auto containsAny = [&](std::string_view needle) {
        return std::ranges::any_of(captured, [&](std::string const& m) { return m.contains(needle); });
    };
    CHECK(containsAny("08006"));
    CHECK(containsAny("connection lost"));
    CHECK(containsAny("Source:"));
}

// ================================================================================================
// SqlColumnTypeDefinitions::MakeColumnTypeFromNative
// ================================================================================================

namespace
{

template <typename Expected>
bool HoldsAlternative(SqlColumnTypeDefinition const& def)
{
    return std::holds_alternative<Expected>(def);
}

// Local helper: invoke `MakeColumnTypeFromNative` and `REQUIRE` the optional has a value,
// returning the contained `SqlColumnTypeDefinition`. The explicit `if`-with-throw wrapper
// is what clang-tidy's `bugprone-unchecked-optional-access` analysis recognizes as a check —
// Catch2's `REQUIRE` is a macro it cannot reason about.
SqlColumnTypeDefinition RequireMakeColumnTypeFromNative(int value, std::size_t size, std::size_t precision)
{
    auto const def = MakeColumnTypeFromNative(value, size, precision);
    REQUIRE(def.has_value());
    if (!def.has_value())
        throw std::runtime_error("REQUIRE failed but flow continued"); // unreachable
    return *def;
}

} // namespace

TEST_CASE("MakeColumnTypeFromNative maps integer-family ODBC types", "[SqlColumnTypeDefinitions]")
{
    using namespace SqlColumnTypeDefinitions;
    REQUIRE(HoldsAlternative<Bigint>(RequireMakeColumnTypeFromNative(SQL_BIGINT, 0, 0)));
    REQUIRE(HoldsAlternative<Integer>(RequireMakeColumnTypeFromNative(SQL_INTEGER, 0, 0)));
    REQUIRE(HoldsAlternative<Smallint>(RequireMakeColumnTypeFromNative(SQL_SMALLINT, 0, 0)));
    REQUIRE(HoldsAlternative<Tinyint>(RequireMakeColumnTypeFromNative(SQL_TINYINT, 0, 0)));
    REQUIRE(HoldsAlternative<Bool>(RequireMakeColumnTypeFromNative(SQL_BIT, 0, 0)));
}

TEST_CASE("MakeColumnTypeFromNative maps date/time types", "[SqlColumnTypeDefinitions]")
{
    using namespace SqlColumnTypeDefinitions;
    REQUIRE(HoldsAlternative<Date>(RequireMakeColumnTypeFromNative(SQL_DATE, 0, 0)));
    REQUIRE(HoldsAlternative<Date>(RequireMakeColumnTypeFromNative(SQL_TYPE_DATE, 0, 0)));
    REQUIRE(HoldsAlternative<Time>(RequireMakeColumnTypeFromNative(SQL_TIME, 0, 0)));
    REQUIRE(HoldsAlternative<Time>(RequireMakeColumnTypeFromNative(SQL_TYPE_TIME, 0, 0)));
    REQUIRE(HoldsAlternative<Time>(RequireMakeColumnTypeFromNative(SQL_SS_TIME2, 0, 0)));
    REQUIRE(HoldsAlternative<DateTime>(RequireMakeColumnTypeFromNative(SQL_TIMESTAMP, 0, 0)));
    REQUIRE(HoldsAlternative<DateTime>(RequireMakeColumnTypeFromNative(SQL_TYPE_TIMESTAMP, 0, 0)));
}

TEST_CASE("MakeColumnTypeFromNative maps numeric types with precision/scale", "[SqlColumnTypeDefinitions]")
{
    using namespace SqlColumnTypeDefinitions;
    {
        auto const def = RequireMakeColumnTypeFromNative(SQL_DECIMAL, 18, 2);
        auto const& d = std::get<Decimal>(def);
        CHECK(d.precision == 18);
        CHECK(d.scale == 2);
    }
    {
        auto const def = RequireMakeColumnTypeFromNative(SQL_NUMERIC, 10, 4);
        auto const& d = std::get<Decimal>(def);
        CHECK(d.precision == 10);
        CHECK(d.scale == 4);
    }
    {
        auto const def = RequireMakeColumnTypeFromNative(SQL_REAL, 0, 0);
        CHECK(std::get<Real>(def).precision == 24);
    }
    {
        auto const def = RequireMakeColumnTypeFromNative(SQL_DOUBLE, 0, 0);
        CHECK(std::get<Real>(def).precision == 53);
    }
    {
        auto const def = RequireMakeColumnTypeFromNative(SQL_FLOAT, 0, 7);
        CHECK(std::get<Real>(def).precision == 7);
    }
}

TEST_CASE("MakeColumnTypeFromNative maps character types preserving size", "[SqlColumnTypeDefinitions]")
{
    using namespace SqlColumnTypeDefinitions;
    CHECK(std::get<Char>(RequireMakeColumnTypeFromNative(SQL_CHAR, 32, 0)).size == 32);
    CHECK(std::get<Varchar>(RequireMakeColumnTypeFromNative(SQL_VARCHAR, 64, 0)).size == 64);
    CHECK(std::get<Varchar>(RequireMakeColumnTypeFromNative(SQL_LONGVARCHAR, 1024, 0)).size == 1024);
    CHECK(std::get<NChar>(RequireMakeColumnTypeFromNative(SQL_WCHAR, 16, 0)).size == 16);
    CHECK(std::get<NVarchar>(RequireMakeColumnTypeFromNative(SQL_WVARCHAR, 128, 0)).size == 128);
    CHECK(std::get<NVarchar>(RequireMakeColumnTypeFromNative(SQL_WLONGVARCHAR, 4096, 0)).size == 4096);
}

TEST_CASE("MakeColumnTypeFromNative maps binary types preserving size", "[SqlColumnTypeDefinitions]")
{
    using namespace SqlColumnTypeDefinitions;
    // Only fixed-length SQL_BINARY maps to Binary; both SQL_VARBINARY and SQL_LONGVARBINARY
    // collapse to VarBinary since they share variable-length semantics.
    CHECK(std::get<Binary>(RequireMakeColumnTypeFromNative(SQL_BINARY, 16, 0)).size == 16);
    CHECK(std::get<VarBinary>(RequireMakeColumnTypeFromNative(SQL_VARBINARY, 32, 0)).size == 32);
    CHECK(std::get<VarBinary>(RequireMakeColumnTypeFromNative(SQL_LONGVARBINARY, 1024, 0)).size == 1024);
}

TEST_CASE("MakeColumnTypeFromNative maps GUID and yields nullopt for unsupported", "[SqlColumnTypeDefinitions]")
{
    using namespace SqlColumnTypeDefinitions;
    REQUIRE(HoldsAlternative<Guid>(RequireMakeColumnTypeFromNative(SQL_GUID, 0, 0)));
    CHECK_FALSE(MakeColumnTypeFromNative(SQL_UNKNOWN_TYPE, 0, 0).has_value());
    CHECK_FALSE(MakeColumnTypeFromNative(0xDEAD, 0, 0).has_value());
}

// ================================================================================================
// SqlNullable enum (just verify it has both states usable in code)
// ================================================================================================

TEST_CASE("SqlNullable enumerators", "[SqlColumnTypeDefinitions]")
{
    CHECK(static_cast<int>(SqlNullable::NotNull) != static_cast<int>(SqlNullable::Null));
}
