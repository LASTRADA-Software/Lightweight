// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <exception>

#include <QtCore/QString>

namespace DbtoolGui
{

/// Extracts the friendliest message from @p exception: the driver's message and SQLSTATE of a
/// `SqlException`, else `what()`.
/// @return The message; "(no details reported)" when the exception carries none.
[[nodiscard]] QString ExceptionMessage(std::exception const& exception);

} // namespace DbtoolGui
