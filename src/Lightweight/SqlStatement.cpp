// SPDX-License-Identifier: Apache-2.0

#include "SqlQuery.hpp"
#include "SqlStatement.hpp"
#include "Utils.hpp"

struct SqlStatement::Data
{
    std::optional<SqlConnection> ownedConnection; // The connection object (if owned)
    std::vector<SQLLEN> indicators;               // Holds the indicators for the bound output columns
    std::vector<std::function<void()>> postExecuteCallbacks;
    std::vector<std::function<void()>> postProcessOutputColumnCallbacks;

    static Data const NoData;
};

SqlStatement::Data const SqlStatement::Data::NoData {};

static auto MakeUnexpected(SqlErrorInfo info, std::source_location location)
{
    SqlLogger::GetLogger().OnError(info, location);
    return std::unexpected { std::move(info) };
}

void SqlStatement::RequireIndicators()
{
    auto const count = NumColumnsAffected() + 1;
    if (m_data->indicators.size() <= count)
        m_data->indicators.resize(count + 1);
}

SQLLEN* SqlStatement::GetIndicatorForColumn(SQLUSMALLINT column) noexcept
{
    return &m_data->indicators[column];
}

void SqlStatement::PlanPostExecuteCallback(std::function<void()>&& cb)
{
    m_data->postExecuteCallbacks.emplace_back(std::move(cb));
}

void SqlStatement::ProcessPostExecuteCallbacks()
{
    for (auto& cb: m_data->postExecuteCallbacks)
        cb();
    m_data->postExecuteCallbacks.clear();
}

void SqlStatement::PlanPostProcessOutputColumn(std::function<void()>&& cb)
{
    m_data->postProcessOutputColumnCallbacks.emplace_back(std::move(cb));
}

SqlServerType SqlStatement::ServerType() const noexcept
{
    return m_connection->ServerType();
}

SqlStatement::SqlStatement():
    m_data { new Data {
                 .ownedConnection = SqlConnection(),
                 .indicators = {},
                 .postExecuteCallbacks = {},
                 .postProcessOutputColumnCallbacks = {},
             },
             [](Data* data) {
                 // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
                 delete data;
             } },
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    m_connection { &*m_data->ownedConnection }
{
    if (m_connection->NativeHandle())
        RequireSuccess(SQLAllocHandle(SQL_HANDLE_STMT, m_connection->NativeHandle(), &m_hStmt));
}

SqlStatement::SqlStatement(SqlStatement&& other) noexcept:
    m_data { std::move(other.m_data) },
    m_connection { other.m_connection },
    m_hStmt { other.m_hStmt },
    m_preparedQuery { std::move(other.m_preparedQuery) },
    m_expectedParameterCount { other.m_expectedParameterCount }
{
    other.m_data.reset();
    other.m_connection = nullptr;
    other.m_hStmt = SQL_NULL_HSTMT;
}

SqlStatement& SqlStatement::operator=(SqlStatement&& other) noexcept
{
    if (this == &other)
        return *this;

    m_data = std::move(other.m_data);
    m_connection = other.m_connection;
    m_hStmt = other.m_hStmt;
    m_preparedQuery = std::move(other.m_preparedQuery);
    m_expectedParameterCount = other.m_expectedParameterCount;

    other.m_data.reset();
    other.m_connection = nullptr;
    other.m_hStmt = SQL_NULL_HSTMT;

    return *this;
}

// Construct a new SqlStatement object, using the given connection.
SqlStatement::SqlStatement(SqlConnection& relatedConnection):
    m_data { new Data(),
             [](Data* data) {
                 // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
                 delete data;
             } },
    m_connection { &relatedConnection }
{
    RequireSuccess(SQLAllocHandle(SQL_HANDLE_STMT, m_connection->NativeHandle(), &m_hStmt));
}

SqlStatement::SqlStatement(std::nullopt_t /*nullopt*/):
    m_data { const_cast<Data*>(&Data::NoData), [](Data* /*data*/) {
            } }
{
}

SqlStatement::~SqlStatement() noexcept
{
    SqlLogger::GetLogger().OnFetchEnd();
    SQLFreeHandle(SQL_HANDLE_STMT, m_hStmt);
}

SqlStatement SqlStatement::Prepare(std::string_view query) &&
{
    auto resultStatement = SqlStatement { std::move(*this) };
    resultStatement.Prepare(query);
    return resultStatement;
}

void SqlStatement::Prepare(std::string_view query) &
{
    SqlLogger::GetLogger().OnPrepare(query);

    m_preparedQuery = std::string(query);

    m_data->postExecuteCallbacks.clear();
    m_data->postProcessOutputColumnCallbacks.clear();

    // Unbinds the columns, if any
    RequireSuccess(SQLFreeStmt(m_hStmt, SQL_UNBIND));

    // Prepares the statement
    RequireSuccess(SQLPrepareA(m_hStmt, (SQLCHAR*) query.data(), (SQLINTEGER) query.size()));
    RequireSuccess(SQLNumParams(m_hStmt, &m_expectedParameterCount));
    m_data->indicators.resize(m_expectedParameterCount + 1);
}

void SqlStatement::ExecuteDirect(const std::string_view& query, std::source_location location)
{
    if (query.empty())
        return;

    m_preparedQuery.clear();
    SqlLogger::GetLogger().OnExecuteDirect(query);

    RequireSuccess(SQLExecDirectA(m_hStmt, (SQLCHAR*) query.data(), (SQLINTEGER) query.size()), location);
}

void SqlStatement::ExecuteWithVariants(std::vector<SqlVariant> const& args)
{
    SqlLogger::GetLogger().OnExecute(m_preparedQuery);

    if (!(m_expectedParameterCount == (std::numeric_limits<decltype(m_expectedParameterCount)>::max)() && args.empty())
        && !(static_cast<size_t>(m_expectedParameterCount) == args.size()))
        throw std::invalid_argument { "Invalid argument count" };

    for (auto const& [i, arg]: args | std::views::enumerate)
        SqlDataBinder<SqlVariant>::InputParameter(m_hStmt, static_cast<SQLUSMALLINT>(1 + i), arg, *this);

    RequireSuccess(SQLExecute(m_hStmt));
    ProcessPostExecuteCallbacks();
}

// Retrieves the number of rows affected by the last query.
size_t SqlStatement::NumRowsAffected() const
{
    SQLLEN numRowsAffected {};
    RequireSuccess(SQLRowCount(m_hStmt, &numRowsAffected));
    return numRowsAffected;
}

// Retrieves the number of columns affected by the last query.
size_t SqlStatement::NumColumnsAffected() const
{
    SQLSMALLINT numColumns {};
    RequireSuccess(SQLNumResultCols(m_hStmt, &numColumns));
    return numColumns;
}

// Retrieves the last insert ID of the last query's primary key.
size_t SqlStatement::LastInsertId(std::string_view tableName)
{
    return ExecuteDirectScalar<size_t>(Query(tableName).LastInsertId()).value_or(0);
}

// Fetches the next row of the result set.
bool SqlStatement::FetchRow()
{
    auto result = TryFetchRow();
    if (result.has_value())
        return result.value();

    SqlErrorInfo errorInfo = std::move(result.error());
    if (errorInfo.sqlState == "07009")
        throw std::invalid_argument(std::format("SQL error: {}", errorInfo));
    else
        throw SqlException(std::move(errorInfo));
}

std::expected<bool, SqlErrorInfo> SqlStatement::TryFetchRow(std::source_location location) noexcept
{
    auto const sqlResult = SQLFetch(m_hStmt);
    switch (sqlResult)
    {
        case SQL_NO_DATA:
            SQLCloseCursor(m_hStmt);
            m_data->postProcessOutputColumnCallbacks.clear();
            SqlLogger::GetLogger().OnFetchEnd();
            return false;
        default:
            if (!SQL_SUCCEEDED(sqlResult))
                return MakeUnexpected(LastError(), location);

            // post-process the output columns, if needed
            for (auto const& postProcess: m_data->postProcessOutputColumnCallbacks)
                postProcess();
            m_data->postProcessOutputColumnCallbacks.clear();
            SqlLogger::GetLogger().OnFetchRow();
            return true;
    }
}

void SqlStatement::RequireSuccess(SQLRETURN error, std::source_location sourceLocation) const
{
    ::RequireSuccess(m_hStmt, error, sourceLocation);
}

SqlQueryBuilder SqlStatement::Query(std::string_view const& table) const
{
    return Connection().Query(table);
}

SqlQueryBuilder SqlStatement::QueryAs(std::string_view const& table, std::string_view const& tableAlias) const
{
    return Connection().QueryAs(table, tableAlias);
}
