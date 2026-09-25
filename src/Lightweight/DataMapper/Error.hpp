// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <expected>
#include <format>
#include <stdexcept>
#include <string_view>

namespace Lightweight
{

/// @brief Why a relation could not provide its record(s).
///
/// Returned by the relation accessors (`BelongsTo::Record()`, `HasMany::All()`, `HasMany::Count()`, ...)
/// through @ref RelationResult instead of throwing.
///
/// @ingroup DataMapper
enum class RelationError : std::uint8_t
{
    /// No loader is installed and nothing was loaded: the record was built by hand, or read with
    /// `DataMapperOptions { .loadRelations = false }`.
    NotConfigured,

    /// There is nothing to load: the foreign key is NULL, or the referenced row does not exist.
    NotFound,

    /// The default connection string changed since the record was read (see
    /// @c SqlConnection::SetDefaultConnectionString), so the record belongs to a database the
    /// application has switched away from. No query is attempted. Re-read the record, or load its
    /// relations before switching.
    Outdated,

    /// The load query failed (lost connection, missing table, ...). The driver's diagnostic is reported
    /// through @c SqlLogger. Unlike the other errors this one is not remembered: the next access retries.
    QueryFailed,
};

/// @brief The outcome of a relation access: the requested value, or why it is unavailable.
///
/// @ingroup DataMapper
template <typename T>
using RelationResult = std::expected<T, RelationError>;

/// @return The name of @p error, e.g. `"Outdated"`.
constexpr std::string_view to_string(RelationError error) noexcept
{
    switch (error)
    {
        case RelationError::NotConfigured:
            return "NotConfigured";
        case RelationError::NotFound:
            return "NotFound";
        case RelationError::Outdated:
            return "Outdated";
        case RelationError::QueryFailed:
            return "QueryFailed";
    }
    return "Unknown";
}

/// @brief Raised by the relation shortcuts that cannot return a @ref RelationResult - `operator->`,
/// `operator*`, `begin()`/`end()`, `At()` and `operator[]` - when the relation is unavailable.
///
/// Prefer the accessors returning @ref RelationResult (`Record()`, `All()`, `Count()`, ...) to handle
/// the unavailable case without exceptions.
///
/// @ingroup DataMapper
class SqlRequireLoadedError: public std::runtime_error
{
  public:
    /// Constructs the error with the name of the column type that failed to load.
    ///
    /// @param columnType The relation type, for the message.
    /// @param error Why it failed.
    explicit SqlRequireLoadedError(std::string_view columnType, RelationError error = RelationError::NotConfigured):
        std::runtime_error(std::format("Could not load the data record: {} ({})", columnType, to_string(error))),
        _error { error }
    {
    }

    /// @return Why the relation could not be loaded.
    [[nodiscard]] RelationError Error() const noexcept
    {
        return _error;
    }

  private:
    RelationError _error;
};

} // namespace Lightweight

/// Formats a @ref Lightweight::RelationError by name.
template <>
struct std::formatter<Lightweight::RelationError>: std::formatter<std::string_view>
{
    /// Formats @p error as its name.
    auto format(Lightweight::RelationError error, std::format_context& ctx) const
    {
        return std::formatter<std::string_view>::format(Lightweight::to_string(error), ctx);
    }
};
