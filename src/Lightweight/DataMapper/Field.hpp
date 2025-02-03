// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../DataBinder/Core.hpp"
#include "../SqlStatement.hpp"
#include "SqlRealName.hpp"

#include <reflection-cpp/reflection.hpp>

enum class FieldValueRequirement : uint8_t
{
    NULLABLE,
    NOT_NULL,
};

constexpr inline FieldValueRequirement SqlNullable = FieldValueRequirement::NULLABLE;
constexpr inline FieldValueRequirement SqlNotNullable = FieldValueRequirement::NULLABLE;

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
/// @ingroup DataMapper
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
        requires std::constructible_from<T, S>
    constexpr Field& operator=(S&& value) noexcept;

    static constexpr auto IsOptional = detail::IsStdOptional<T>;
    static constexpr auto IsMandatory = !IsOptional;
    static constexpr auto IsPrimaryKey = IsPrimaryKeyValue != PrimaryKey::No;
    static constexpr auto IsAutoAssignPrimaryKey = IsPrimaryKeyValue == PrimaryKey::AutoAssign;
    static constexpr auto IsAutoIncrementPrimaryKey = IsPrimaryKeyValue == PrimaryKey::ServerSideAutoIncrement;

    constexpr std::weak_ordering operator<=>(Field const& other) const noexcept;

    bool operator==(Field const& value) const noexcept = default;
    bool operator!=(Field const& value) const noexcept = default;

    template <typename S>
        requires std::convertible_to<S, T>
    bool operator==(S const& value) const noexcept;

    template <typename S>
        requires std::convertible_to<S, T>
    bool operator!=(S const& value) const noexcept;

    /// Returns a string representation of the value, suitable for use in debugging and logging.
    [[nodiscard]] std::string InspectValue() const;

    /// Sets the modified state of the field.
    constexpr void SetModified(bool value) noexcept;

    /// Checks if the field has been modified.
    [[nodiscard]] constexpr bool IsModified() const noexcept;

    /// Returns the value of the field.
    [[nodiscard]] constexpr T const& Value() const noexcept;

    /// Returns a mutable reference to the value of the field.
    ///
    /// @note If the field value is changed through this method, it will not be automatically marked as modified.
    [[nodiscard]] constexpr T& MutableValue() noexcept;

  private:
    ValueType _value {};
    bool _modified { false };
};

// clang-format off
namespace detail
{

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

template <typename ReferencedFieldType, auto F>
struct FieldNameOfImpl;

template <typename T, auto F, typename R>
struct FieldNameOfImpl<R T::*, F>
{
    static constexpr std::string_view value = []() constexpr -> std::string_view {
        if constexpr (requires { R::ColumnNameOverride; })
        {
            if constexpr (!R::ColumnNameOverride.empty())
                return R::ColumnNameOverride;
        }
        return Reflection::GetName<F>();
    }();
};

} // namespace detail
// clang-format on


/// @brief Returns the name of the field referenced by the given pointer-to-member.
///
/// This also supports custom column name overrides.
template <auto ReferencedField>
constexpr inline std::string_view FieldNameOf =
    detail::FieldNameOfImpl<decltype(ReferencedField), ReferencedField>::value;

// Requires that T satisfies to be a field with storage and is considered a primary key.
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
    requires std::constructible_from<T, S>
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
template <typename S>
    requires std::convertible_to<S, T>
inline LIGHTWEIGHT_FORCE_INLINE bool Field<T, P1, P2>::operator==(S const& value) const noexcept
{
    return _value == value;
}

template <detail::FieldElementType T, auto P1, auto P2>
template <typename S>
    requires std::convertible_to<S, T>
inline LIGHTWEIGHT_FORCE_INLINE bool Field<T, P1, P2>::operator!=(S const& value) const noexcept
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

    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN GetColumn(SQLHSTMT stmt,
                                                        SQLUSMALLINT column,
                                                        ValueType* result,
                                                        SQLLEN* indicator,
                                                        SqlDataBinderCallback const& cb) noexcept
    {
        auto const sqlReturn = SqlDataBinder<T>::GetColumn(stmt, column, &result->emplace(), indicator, cb);
        if (SQL_SUCCEEDED(sqlReturn))
            result->SetModified(true);
        return sqlReturn;
    }

    static LIGHTWEIGHT_FORCE_INLINE std::string Inspect(ValueType const& value) noexcept
    {
        return value.InspectValue();
    }
};

template <detail::FieldElementType T, auto P1, auto P2>
struct std::formatter<Field<T, P1, P2>>: std::formatter<T>
{
    template <typename FormatContext>
    auto format(Field<T, P1, P2> const& field, FormatContext& ctx)
    {
        return formatter<T>::format(field.InspectValue(), ctx);
    }
};
