// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../DataBinder/Core.hpp"
#include "../DataBinder/SqlDate.hpp"
#include "../DataBinder/SqlDateTime.hpp"
#include "../DataBinder/SqlNumeric.hpp"
#include "../DataBinder/SqlText.hpp"
#include "../DataBinder/SqlTime.hpp"
#include "../Utils.hpp"

#include <reflection-cpp/reflection.hpp>

#include <iomanip>
#include <optional>
#include <sstream>

namespace Lightweight
{

/// @brief Tells the data mapper that this field is a primary key with given semantics, or not a primary key.
enum class PrimaryKey : uint8_t
{
    /// The field is not a primary key.
    No,

    /// @brief The field is a primary key.
    ///
    /// If the field is an auto-incrementable key and not manually set, it is automatically set to the
    /// next available value on the client side, using a SELECT MAX() query.
    /// This is happening transparently to the user.
    ///
    /// If the field is a GUID, it is automatically set to a new GUID value, if not manually set.
    ///
    /// @note If the field is neither auto-incrementable nor a GUID, it must be manually set. For a key
    ///       the mapper must never generate even when it could, use @c Manual.
    AutoAssign,

    /// The field is an integer primary key, and it is auto-incremented by the database.
    ServerSideAutoIncrement,

    /// @brief The field is a primary key whose value the caller always supplies.
    ///
    /// The data mapper neither generates it nor writes a generated value into it, so this is the
    /// assignment for every member of a composite primary key: auto-assignment produces one value,
    /// which a composite key cannot be built from.
    Manual,
};

/// @brief Tells the data mapper that this field is a timestamp it maintains automatically, or not.
///
/// Pass it in one of a Field's two option slots, alone or together with a column-name override:
///
/// @code
/// struct Article
/// {
///     Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id;
///     Field<SqlDateTime, FieldTimestamp::CreatedAt> createdAt;
///     Field<SqlDateTime, FieldTimestamp::UpdatedAt, SqlRealName { "updated_at" }> updatedAt;
/// };
/// @endcode
///
/// The value is read from the DataMapper's timestamp clock (see DataMapper::SetTimestampClock()), once
/// per operation, so every timestamp written by one operation carries the same value. A value the
/// caller set explicitly is never overwritten, which allows importing historical rows.
///
/// Only `SqlDateTime` and `std::optional<SqlDateTime>` fields may carry a timestamp marker.
enum class FieldTimestamp : uint8_t
{
    /// The field is not maintained by the data mapper.
    None,

    /// @brief The field holds the time the record was created.
    ///
    /// Set when the record is inserted, unless the caller gave it a value. Never changed by an update.
    CreatedAt,

    /// @brief The field holds the time the record was last written.
    ///
    /// Set when the record is inserted, unless the caller gave it a value, and again whenever an update
    /// writes any other column of the record, unless the caller modified this field in that change.
    UpdatedAt,
};

namespace detail
{

    // clang-format off

template <typename T>
struct IsStdOptionalType: std::false_type {};

template <typename T>
struct IsStdOptionalType<std::optional<T>>: std::true_type {};

template <typename T>
constexpr bool IsStdOptional = IsStdOptionalType<T>::value;

template <typename T>
concept FieldElementType = SqlInputParameterBinder<T> && SqlOutputColumnBinder<T>;

    // clang-format on

    template <typename TargetType, typename P1, typename P2>
    consteval auto Choose(TargetType defaultValue, P1 p1, P2 p2) noexcept
    {
        if constexpr (!std::same_as<P1, std::nullopt_t> && requires { TargetType { p1 }; })
            return p1;
        else if constexpr (!std::same_as<P2, std::nullopt_t> && requires { TargetType { p2 }; })
            return p2;
        else
            return defaultValue;
    }

    /// Whether @p T is a value type a @ref FieldTimestamp marker may be attached to.
    template <typename T>
    inline constexpr bool IsAutoTimestampValueType = OneOf<T, SqlDateTime, std::optional<SqlDateTime>>;

    /// @brief Whether an auto-maintained timestamp value is unset, i.e. was never given a value.
    ///
    /// An optional timestamp is unset when it holds no value; a plain `SqlDateTime` is unset when it
    /// has no date, as a default-constructed one has (no valid date has a zero month).
    ///
    /// @param value The timestamp value to test.
    /// @return True if the data mapper may fill in the value.
    template <typename T>
        requires IsAutoTimestampValueType<T>
    [[nodiscard]] constexpr bool IsUnsetTimestamp(T const& value) noexcept
    {
        if constexpr (IsStdOptional<T>)
            return !value.has_value();
        else
            return value.sqlValue.month == 0;
    }
} // namespace detail

/// @brief Represents a single column in a table.
///
/// This class is used to represent a single column in a table.
/// It also keeps track of modified-state of the field.
///
/// The column name, index, nullability, and type are known at compile time.
///
/// @see DataMapper
/// @ingroup DataMapper, DataTypes
template <detail::FieldElementType T, auto P1 = std::nullopt, auto P2 = std::nullopt>
struct Field
{
    /// The underlying value type of this field.
    using ValueType = T;

    /// The primary key mode for this field.
    static constexpr auto IsPrimaryKeyValue = detail::Choose<PrimaryKey>(PrimaryKey::No, P1, P2);
    /// If not empty, overrides the default column name in the database.
    static constexpr auto ColumnNameOverride = detail::Choose<std::string_view>({}, P1, P2);
    /// Which timestamp, if any, the data mapper maintains in this field.
    static constexpr auto TimestampKind = detail::Choose<FieldTimestamp>(FieldTimestamp::None, P1, P2);

    /// Indicates if the data mapper sets this field to the creation time of the record.
    static constexpr auto IsCreatedAtTimestamp = TimestampKind == FieldTimestamp::CreatedAt;

    /// Indicates if the data mapper sets this field to the time the record was last written.
    static constexpr auto IsUpdatedAtTimestamp = TimestampKind == FieldTimestamp::UpdatedAt;

    /// Indicates if the data mapper maintains this field as a timestamp of either kind.
    static constexpr auto IsAutoTimestamp = TimestampKind != FieldTimestamp::None;

    static_assert(!IsAutoTimestamp || detail::IsAutoTimestampValueType<T>,
                  "A FieldTimestamp marker (CreatedAt / UpdatedAt) requires the field's value type to be SqlDateTime "
                  "or std::optional<SqlDateTime>.");

    // clang-format off
    constexpr Field() noexcept = default;
    /// Default copy constructor.
    constexpr Field(Field const&) noexcept = default;
    /// Default copy assignment operator.
    constexpr Field& operator=(Field const&) noexcept = default;
    /// Default move constructor.
    constexpr Field(Field&&) noexcept = default;
    /// Default move assignment operator.
    constexpr Field& operator=(Field&&) noexcept = default;
    constexpr ~Field() noexcept = default;
    // clang-format on

    /// Constructs a new field with the given value.
    template <typename... S>
        requires std::constructible_from<T, S...>
    constexpr Field(S&&... value) noexcept;

    /// Assigns a new value to the field.
    template <typename S>
        requires std::constructible_from<T, S> && (!std::same_as<std::remove_cvref_t<S>, Field<T, P1, P2>>)
    // NOLINTNEXTLINE(cppcoreguidelines-c-copy-assignment-signature)
    constexpr Field& operator=(S&& value) noexcept;

    /// Indicates if the field is optional, i.e., it can be NULL.
    static constexpr auto IsOptional = detail::IsStdOptional<T>;

    /// Indicates if the field is mandatory, i.e., it cannot be NULL.
    static constexpr auto IsMandatory = !IsOptional;

    /// Indicates if the field is a primary key.
    static constexpr auto IsPrimaryKey = IsPrimaryKeyValue != PrimaryKey::No;

    /// Indicates if this is a primary key, it also is auto-assigned by the client.
    static constexpr auto IsAutoAssignPrimaryKey = IsPrimaryKeyValue == PrimaryKey::AutoAssign;

    /// Indicates if this is a primary key, it also is auto-incremented by the database.
    static constexpr auto IsAutoIncrementPrimaryKey = IsPrimaryKeyValue == PrimaryKey::ServerSideAutoIncrement;

    /// Three-way comparison operator. The strength of the ordering comes from the underlying
    /// type `T` — e.g. `Field<int>` is `std::strong_ordering`, `Field<double>` is
    /// `std::partial_ordering` (because of NaN), `Field<std::string>` is `std::strong_ordering`.
    /// Defined inline so the deduced return type is available at every call site, and so
    /// `Field<T>` instantiation does not require `T` to have `operator<=>`.
    constexpr auto operator<=>(Field const& other) const noexcept
        requires requires(T const& a, T const& b) { a <=> b; }
    {
        return _value <=> other._value;
    }

    /// Compares the field value with the given value for equality.
    constexpr bool operator==(Field const& other) const noexcept;

    /// Compares the field value with the given value for inequality.
    constexpr bool operator!=(Field const& other) const noexcept;

    /// Compares the field value with the given value for equality.
    template <typename S>
        requires std::convertible_to<S, T>
    constexpr bool operator==(S const& value) const noexcept;

    /// Compares the field value with the given value for inequality.
    template <typename S>
        requires std::convertible_to<S, T>
    constexpr bool operator!=(S const& value) const noexcept;

    /// Returns a string representation of the value, suitable for use in debugging and logging.
    [[nodiscard]] std::string InspectValue() const;

    /// Sets the modified state of the field.
    constexpr void SetModified(bool value) noexcept;

    /// Checks if the field has been modified.
    [[nodiscard]] constexpr bool IsModified() const noexcept;

    /// Returns the value of the field.
    [[nodiscard]] constexpr T const& Value() const noexcept;

    /// When the field type is optional, returns the value or the given default value.
    [[nodiscard]] constexpr auto ValueOr(auto&& defaultValue) const noexcept
        requires IsOptional
    {
        return _value.value_or(std::forward<typename ValueType::value_type>(defaultValue));
    }

    /// Returns a mutable reference to the value of the field.
    ///
    /// @note If the field value is changed through this method, it will not be automatically marked as modified.
    [[nodiscard]] constexpr T& MutableValue() noexcept;

  private:
    ValueType _value {};
    bool _modified { true };
};

// clang-format off
namespace detail
{

template <typename T>
struct IsAutoAssignPrimaryKeyField: std::false_type {};

template <typename T, auto P>
struct IsAutoAssignPrimaryKeyField<Field<T, PrimaryKey::AutoAssign, P>>: std::true_type {};

template <typename T, auto P>
struct IsAutoAssignPrimaryKeyField<Field<T, P, PrimaryKey::AutoAssign>>: std::true_type {};

template <typename T>
struct IsAutoIncrementPrimaryKeyField: std::false_type {};

template <typename T, auto P>
struct IsAutoIncrementPrimaryKeyField<Field<T, PrimaryKey::ServerSideAutoIncrement, P>>: std::true_type {};

template <typename T, auto P>
struct IsAutoIncrementPrimaryKeyField<Field<T, P, PrimaryKey::ServerSideAutoIncrement>>: std::true_type {};

template <typename T>
struct IsFieldType: std::false_type {};

template <typename T, auto P1, auto P2>
struct IsFieldType<Field<T, P1, P2>>: std::true_type {};

} // namespace detail
// clang-format on

/// Tests if T is a Field<> that is a primary key, of whichever @ref PrimaryKey kind.
template <typename T>
constexpr bool IsPrimaryKey = false;

template <typename T, auto P1, auto P2>
constexpr bool IsPrimaryKey<Field<T, P1, P2>> = Field<T, P1, P2>::IsPrimaryKey;

/// Requires that T satisfies to be a field with storage and is considered a primary key.
template <typename T>
constexpr bool IsAutoIncrementPrimaryKey = detail::IsAutoIncrementPrimaryKeyField<T>::value;

template <typename T>
constexpr bool IsField = detail::IsFieldType<std::remove_cvref_t<T>>::value;

/// The @ref FieldTimestamp kind of @p T: the field's marker if T is a Field<>, otherwise None.
template <typename T>
inline constexpr FieldTimestamp FieldTimestampOf = FieldTimestamp::None;

/// The @ref FieldTimestamp kind of a Field<>.
template <typename T, auto P1, auto P2>
inline constexpr FieldTimestamp FieldTimestampOf<Field<T, P1, P2>> = Field<T, P1, P2>::TimestampKind;

/// Tests if T is a Field<> whose value the data mapper maintains as a timestamp (CreatedAt or UpdatedAt).
template <typename T>
inline constexpr bool IsAutoTimestampField = FieldTimestampOf<std::remove_cvref_t<T>> != FieldTimestamp::None;

/// Constructs a new field with the given value.
template <detail::FieldElementType T, auto P1, auto P2>
template <typename... S>
    requires std::constructible_from<T, S...>
constexpr LIGHTWEIGHT_FORCE_INLINE Field<T, P1, P2>::Field(S&&... value) noexcept:
    _value(std::forward<S>(value)...)
{
}

/// @copydoc Field::operator=(S&&)
template <detail::FieldElementType T, auto P1, auto P2>
template <typename S>
    requires std::constructible_from<T, S> && (!std::same_as<std::remove_cvref_t<S>, Field<T, P1, P2>>)
constexpr LIGHTWEIGHT_FORCE_INLINE Field<T, P1, P2>& Field<T, P1, P2>::operator=(S&& value) noexcept
{
    _value = std::forward<S>(value);
    SetModified(true);
    return *this;
}

template <detail::FieldElementType T, auto P1, auto P2>
constexpr bool LIGHTWEIGHT_FORCE_INLINE Field<T, P1, P2>::operator==(Field const& other) const noexcept
{
    return _value == other._value;
}

template <detail::FieldElementType T, auto P1, auto P2>
constexpr bool LIGHTWEIGHT_FORCE_INLINE Field<T, P1, P2>::operator!=(Field const& other) const noexcept
{
    return _value != other._value;
}

/// Equality comparison operator with a convertible value.
template <detail::FieldElementType T, auto P1, auto P2>
template <typename S>
    requires std::convertible_to<S, T>
constexpr bool LIGHTWEIGHT_FORCE_INLINE Field<T, P1, P2>::operator==(S const& value) const noexcept
{
    return _value == value;
}

/// Inequality comparison operator with a convertible value.
template <detail::FieldElementType T, auto P1, auto P2>
template <typename S>
    requires std::convertible_to<S, T>
constexpr bool LIGHTWEIGHT_FORCE_INLINE Field<T, P1, P2>::operator!=(S const& value) const noexcept
{
    return _value != value;
}

template <detail::FieldElementType T, auto P1, auto P2>
inline LIGHTWEIGHT_FORCE_INLINE std::string Field<T, P1, P2>::InspectValue() const
{
    if constexpr (std::is_same_v<T, std::string>)
    {
        std::stringstream result;
        result << std::quoted(_value, '\'');
        return result.str();
    }
    else if constexpr (std::is_same_v<T, SqlText>)
    {
        std::stringstream result;
        result << std::quoted(_value.value, '\'');
        return result.str();
    }
    else if constexpr (std::is_same_v<T, SqlDate>)
        return std::format("\'{}\'", _value.value());
    else if constexpr (std::is_same_v<T, SqlTime>)
        return std::format("\'{}\'", _value.value());
    else if constexpr (std::is_same_v<T, SqlDateTime>)
        return std::format("\'{}\'", _value.value());
    else if constexpr (SqlNumericType<T>)
        return std::format("{}", _value.ToString());
    else if constexpr (requires { _value.has_value(); })
    {
        if (_value.has_value())
            return std::format("{}", _value.value());
        else
            return "NULL";
    }
    else
        return std::format("{}", _value);
}

// ------------------------------------------------------------------------------------------------

template <detail::FieldElementType T, auto P1, auto P2>
constexpr LIGHTWEIGHT_FORCE_INLINE void Field<T, P1, P2>::SetModified(bool value) noexcept
{
    _modified = value;
}

template <detail::FieldElementType T, auto P1, auto P2>
constexpr LIGHTWEIGHT_FORCE_INLINE bool Field<T, P1, P2>::IsModified() const noexcept
{
    return _modified;
}

template <detail::FieldElementType T, auto P1, auto P2>
constexpr LIGHTWEIGHT_FORCE_INLINE T const& Field<T, P1, P2>::Value() const noexcept
{
    return _value;
}

template <detail::FieldElementType T, auto P1, auto P2>
constexpr LIGHTWEIGHT_FORCE_INLINE T& Field<T, P1, P2>::MutableValue() noexcept
{
    return _value;
}

template <detail::FieldElementType T, auto P1, auto P2>
struct SqlDataBinder<Field<T, P1, P2>>
{
    using ValueType = Field<T, P1, P2>;

    static constexpr auto ColumnType = SqlDataBinder<T>::ColumnType;

    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN InputParameter(SQLHSTMT stmt,
                                                             SQLUSMALLINT column,
                                                             ValueType const& value,
                                                             SqlDataBinderCallback& cb)
    {
        return SqlDataBinder<T>::InputParameter(stmt, column, value.Value(), cb);
    }

    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN
    OutputColumn(SQLHSTMT stmt, SQLUSMALLINT column, ValueType* result, SQLLEN* indicator, SqlDataBinderCallback& cb)
    {
        return SqlDataBinder<T>::OutputColumn(stmt, column, &result->MutableValue(), indicator, cb);
    }

    /// @throws Whatever `SqlDataBinder<T>::GetColumn` throws — this forwards to an arbitrary binder
    ///         and cannot promise more than the one it wraps.
    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN
    GetColumn(SQLHSTMT stmt, SQLUSMALLINT column, ValueType* result, SQLLEN* indicator, SqlDataBinderCallback const& cb)
    {
        return SqlDataBinder<T>::GetColumn(stmt, column, &result->emplace(), indicator, cb);
    }

    static LIGHTWEIGHT_FORCE_INLINE std::string Inspect(ValueType const& value) noexcept
    {
        return value.InspectValue();
    }
};

/// @brief Retrieves the type of a member field in a record.
///
/// Field must be a member of the record type, and it must be a field type, e.g. `Field<int>` or `BelongsTo<OtherRecord>`.
///
/// @code
/// using MyRecord = Record {
///    Field<int> value;
///    Field<std::optional<char>> optionalValue;
/// };
///
/// using MyFieldType = ReferencedFieldTypeOf<&MyRecord::value>; // Retrieves `int`
/// using MyOptionalFieldType = ReferencedFieldTypeOf<&MyRecord::optionalValue>; // Retrieves `std::optional<char>`
/// @endcode
///
/// @ingroup DataMapper
template <auto Field>
using ReferencedFieldTypeOf =
#if defined(LIGHTWEIGHT_CXX26_REFLECTION)
    typename std::remove_cvref_t<typename[:type_of(Field):]>::ValueType;
#else
    std::remove_cvref_t<decltype(std::declval<MemberClassType<decltype(Field)>>().*Field)>::ValueType;
#endif

} // namespace Lightweight

template <Lightweight::detail::FieldElementType T, auto P1, auto P2>
struct std::formatter<Lightweight::Field<T, P1, P2>>: std::formatter<T>
{
    template <typename FormatContext>
    // NOLINTNEXTLINE(readability-identifier-naming)
    auto format(Lightweight::Field<T, P1, P2> const& field, FormatContext& ctx) const
    {
        return formatter<T>::format(field.Value(), ctx);
    }
};

/// Specialization for `Field<std::optional<T>, ...>`: `std::optional` has no
/// `std::formatter` specialization in the standard library, so we inherit from
/// the inner `std::formatter<T>` and render `"NULL"` for the empty case.
template <Lightweight::detail::FieldElementType T, auto P1, auto P2>
struct std::formatter<Lightweight::Field<std::optional<T>, P1, P2>>: std::formatter<T>
{
    template <typename FormatContext>
    // NOLINTNEXTLINE(readability-identifier-naming)
    auto format(Lightweight::Field<std::optional<T>, P1, P2> const& field, FormatContext& ctx) const
    {
        if (field.Value().has_value())
            return formatter<T>::format(field.Value().value(), ctx);

        // Fallback for the NULL case — write "NULL" verbatim through the format context.
        constexpr std::string_view nullText { "NULL" };
        return std::ranges::copy(nullText, ctx.out()).out;
    }
};
