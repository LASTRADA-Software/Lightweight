// SPDX-License-Identifier: Apache-2.0

#include "ExceptionMessage.hpp"

#include <Lightweight/SqlError.hpp>

namespace DbtoolGui
{

QString ExceptionMessage(std::exception const& exception)
{
    // The driver-level explanation ("no such table", …) beats `what()`, which often decays to the bare SQLSTATE.
    if (auto const* sqlException = dynamic_cast<Lightweight::SqlException const*>(&exception))
    {
        auto const& info = sqlException->info();
        auto const message = QString::fromStdString(info.message).trimmed();
        auto const state = QString::fromStdString(info.sqlState).trimmed();
        if (!message.isEmpty())
        {
            if (!state.isEmpty() && state != QStringLiteral("00000"))
                return QStringLiteral("%1 [SQLSTATE %2]").arg(message, state);
            return message;
        }
    }
    auto const fallback = QString::fromUtf8(exception.what()).trimmed();
    return fallback.isEmpty() ? QStringLiteral("(no details reported)") : fallback;
}

} // namespace DbtoolGui
