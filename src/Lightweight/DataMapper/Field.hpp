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
    /// @note If the field is neither auto-incrementable nor a GUID, it must be manually set.
    AutoAssign,

    /// The field is an integer primary key, and it is auto-incremented by the database.
    ServerSideAutoIncrement,
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
    using ValueType = T;

    static constexpr auto IsPrimaryKeyValue = detail::Choose<PrimaryKey>(PrimaryKey::No, P1, P2);
    static constexpr auto ColumnNameOverride = detail::Choose<std::string_view>({}, P1, P2);

    // clang-format off
    constexpr Field() noexcept = default;
    constexpr Field(Field const&) noexcept = default;
    constexpr Field& operator=(Field const&) noexcept = default;
    constexpr Field(Field&&) noexcept = default;
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

    /// Compares two fields for equality.
    constexpr std::weak_ordering operator<=>(Field const& other) const noexcept;

    /// Compares the field value with the given value for equality.
    constexpr bool operator==(Field const& value) const noexcept;

    /// Compares the field value with the given value for inequality.
    constexpr bool operator!=(Field const& value) const noexcept;

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

/// Tests if T is a Field<> that is a primary key.
template <typename T>
constexpr bool IsPrimaryKey =
    detail::IsAutoAssignPrimaryKeyField<T>::value || detail::IsAutoIncrementPrimaryKeyField<T>::value;

/// Requires that T satisfies to be a field with storage and is considered a primary key.
template <typename T>
constexpr bool IsAutoIncrementPrimaryKey = detail::IsAutoIncrementPrimaryKeyField<T>::value;

template <typename T>
constexpr bool IsField = detail::IsFieldType<std::remove_cvref_t<T>>::value;

template <detail::FieldElementType T, auto P1, auto P2>
template <typename... S>
    requires std::constructible_from<T, S...>
constexpr LIGHTWEIGHT_FORCE_INLINE Field<T, P1, P2>::Field(S&&... value) noexcept:
    _value(std::forward<S>(value)...)
{
}

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
constexpr std::weak_ordering LIGHTWEIGHT_FORCE_INLINE Field<T, P1, P2>::operator<=>(Field const& other) const noexcept
{
    return _value <=> other._value;
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

template <detail::FieldElementType T, auto P1, auto P2>
template <typename S>
    requires std::convertible_to<S, T>
constexpr bool LIGHTWEIGHT_FORCE_INLINE Field<T, P1, P2>::operator==(S const& value) const noexcept
{
    return _value == value;
}

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
        return std::format("\'{}\'", _value.value);
    else if constexpr (std::is_same_v<T, SqlTime>)
        return std::format("\'{}\'", _value.value);
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

    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN GetColumn(
        SQLHSTMT stmt, SQLUSMALLINT column, ValueType* result, SQLLEN* indicator, SqlDataBinderCallback const& cb) noexcept
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
    auto format(Lightweight::Field<T, P1, P2> const& field, FormatContext& ctx)
    {
        return formatter<T>::format(field.InspectValue(), ctx);
    }
};
