// SPDX-License-Identifier: Apache-2.0

#include "DbAccessProbe.hpp"

#include <utility>

namespace DbtoolGui::Tests
{

using Lightweight::SqlConnection;
using Lightweight::SqlLogger;

DbAccessProbe::DbAccessProbe():
    SqlLogger(SupportBindLogging::No),
    _forward { &SqlLogger::GetLogger() }
{
}

DbAccessProbe::~DbAccessProbe()
{
    Stop();
}

void DbAccessProbe::Start()
{
    _count = 0;
    if (_installed)
        return;
    _forward.store(&SqlLogger::GetLogger());
    SqlLogger::SetLogger(*this);
    _installed = true;
}

int DbAccessProbe::Count() const noexcept
{
    return _count.load();
}

void DbAccessProbe::Stop()
{
    if (!_installed)
        return;
    // `_forward` stays set: a call another thread began before the switch still forwards through it.
    SqlLogger::SetLogger(*_forward.load());
    _installed = false;
}

void DbAccessProbe::Hit() noexcept
{
    ++_count;
}

void DbAccessProbe::OnWarning(std::string_view const& message)
{
    _forward.load()->OnWarning(message);
}

void DbAccessProbe::OnError(Lightweight::SqlError errorCode, std::source_location sourceLocation)
{
    Hit();
    _forward.load()->OnError(errorCode, sourceLocation);
}

void DbAccessProbe::OnError(Lightweight::SqlErrorInfo const& errorInfo, std::source_location sourceLocation)
{
    Hit();
    _forward.load()->OnError(errorInfo, sourceLocation);
}

void DbAccessProbe::OnScopedTimerStart(std::string const& tag)
{
    _forward.load()->OnScopedTimerStart(tag);
}

void DbAccessProbe::OnScopedTimerStop(std::string const& tag)
{
    _forward.load()->OnScopedTimerStop(tag);
}

void DbAccessProbe::OnConnectionOpened(SqlConnection const& connection)
{
    Hit();
    _forward.load()->OnConnectionOpened(connection);
}

void DbAccessProbe::OnConnectionClosed(SqlConnection const& connection)
{
    _forward.load()->OnConnectionClosed(connection);
}

void DbAccessProbe::OnConnectionIdle(SqlConnection const& connection)
{
    _forward.load()->OnConnectionIdle(connection);
}

void DbAccessProbe::OnConnectionReuse(SqlConnection const& connection)
{
    _forward.load()->OnConnectionReuse(connection);
}

void DbAccessProbe::OnExecuteDirect(std::string_view const& query)
{
    Hit();
    _forward.load()->OnExecuteDirect(query);
}

void DbAccessProbe::OnPrepare(std::string_view const& query)
{
    Hit();
    _forward.load()->OnPrepare(query);
}

void DbAccessProbe::OnBind(std::string_view const& name, std::string value)
{
    _forward.load()->OnBind(name, std::move(value));
}

void DbAccessProbe::OnExecute(std::string_view const& query)
{
    Hit();
    _forward.load()->OnExecute(query);
}

void DbAccessProbe::OnExecuteBatch()
{
    _forward.load()->OnExecuteBatch();
}

void DbAccessProbe::OnFetchRow()
{
    _forward.load()->OnFetchRow();
}

void DbAccessProbe::OnFetchBlock(std::size_t rowsInBlock)
{
    _forward.load()->OnFetchBlock(rowsInBlock);
}

void DbAccessProbe::OnFetchEnd()
{
    _forward.load()->OnFetchEnd();
}

} // namespace DbtoolGui::Tests
