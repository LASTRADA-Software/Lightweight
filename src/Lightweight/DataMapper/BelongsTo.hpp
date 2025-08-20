// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../DataBinder/Core.hpp"
#include "../DataBinder/SqlNullValue.hpp"
#include "../SqlStatement.hpp"
#include "../Utils.hpp"
#include "Error.hpp"
#include "Field.hpp"

#include <compare>
#include <optional>
#include <string_view>
#include <type_traits>

namespace Lightweight
{

/// @brief Represents a one-to-one relationship.
///
/// The `TheReferencedField` parameter is the field in the other record that references the current record,
/// in the form of `&OtherRecord::Field`.
/// Other Field must be a primary key.
///
/// @tparam TheReferencedField The field in the other record that references the current record.
/// @tparam ColumnNameOverrideString If not an empty string, this value will be used as the column name in the database.
///
/// @ingroup DataMapper
///
/// @code
/// struct User {
///     Field<SqlGuid, PrimaryKey::AutoAssign> id;
///     Field<SqlAnsiString<30>> name;
/// };
/// struct Email {
///     Field<SqlGuid, PrimaryKey::AutoAssign> id;
///     Field<SqlAnsiString<40>> address;
///     BelongsTo<&User::id> user;
///     // Also possible to customize the column name
///     BelongsTo<&User::id, SqlRealName<"the_user_id">, SqlNullable::Null> maybe_user;
/// };
/// @endcode
template <auto TheReferencedField, auto ColumnNameOverrideString = std::nullopt, SqlNullable Nullable = SqlNullable::NotNull>
class BelongsTo
{
  public:
    /// The field in the other record that references the current record.
    static constexpr auto ReferencedField = TheReferencedField;

    /// If not an empty string, this value will be used as the column name in the database.
    static constexpr std::string_view ColumnNameOverride = []() consteval {
        if constexpr (!std::same_as<decltype(ColumnNameOverrideString), std::nullopt_t>)
            return std::string_view { ColumnNameOverrideString };
        else
            return std::string_view {};
    }();

#if defined(LIGHTWEIGHT_CXX26_REFLECTION)
    /// Represents the record type of the other field.
    using ReferencedRecord = MemberClassType<TheReferencedField>;

    /// Represents the base column type of the foreign key, matching the primary key of the other record.
    using BaseType = typename[:std::meta::type_of(ReferencedField):] ::ValueType;

    static_assert(std::remove_cvref_t<decltype(std::declval<ReferencedRecord>().[:ReferencedField:])>::IsPrimaryKey,
                  "The referenced field must be a primary key.");
#else
    /// Represents the record type of the other field.
    using ReferencedRecord = MemberClassType<decltype(TheReferencedField)>;

    static_assert(std::remove_cvref_t<decltype(std::declval<ReferencedRecord>().*ReferencedField)>::IsPrimaryKey,
                  "The referenced field must be a primary key.");

    /// Represents the base column type of the foreign key, matching the primary key of the other record.
    using BaseType = typename std::remove_cvref_t<decltype(std::declval<ReferencedRecord>().*ReferencedField)>::ValueType;
#endif

    /// Represents the value type of the foreign key,
    /// which can be either an optional or a non-optional type of the referenced field,
    using ValueType = std::conditional_t<Nullable == SqlNullable::Null, std::optional<BaseType>, BaseType>;

    static constexpr auto IsOptional = Nullable == SqlNullable::Null;
    static constexpr auto IsMandatory = !IsOptional;
    static constexpr auto IsPrimaryKey = false;
    static constexpr auto IsAutoIncrementPrimaryKey = false;

    template <typename... S>
        requires std::constructible_from<ValueType, S...>
    constexpr BelongsTo(S&&... value) noexcept:
        _referencedFieldValue(std::forward<S>(value)...)
    {
    }

    constexpr BelongsTo(ReferencedRecord const& other) noexcept:
#if defined(LIGHTWEIGHT_CXX26_REFLECTION)
        _referencedFieldValue { (other.[:ReferencedField:]).Value() },
#else
        _referencedFieldValue { (other.*ReferencedField).Value() },
#endif
        _loaded { true },
        _record { std::make_unique<ReferencedRecord>(other) }
    {
    }

    constexpr BelongsTo(BelongsTo const& other) noexcept:
        _referencedFieldValue(other._referencedFieldValue),
        _loader(std::move(other._loader)),
        _loaded(other._loaded),
        _modified(other._modified),
        _record(other._record ? std::make_unique<ReferencedRecord>(*other._record) : nullptr)
    {
    }

    constexpr BelongsTo(BelongsTo&& other) noexcept:
        _referencedFieldValue(std::move(other._referencedFieldValue)),
        _loader(std::move(other._loader)),
        _loaded(other._loaded),
        _modified(other._modified),
        _record(std::move(other._record))
    {
    }

    BelongsTo& operator=(SqlNullType /*nullValue*/) noexcept
    {
        if (!_referencedFieldValue)
            return *this;
        _loaded = false;
        _record = std::nullopt;
        _referencedFieldValue = {};
        _modified = true;
        return *this;
    }

    BelongsTo& operator=(ReferencedRecord& other)
    {
#if defined(LIGHTWEIGHT_CXX26_REFLECTION)
        if (_referencedFieldValue == (other.[:ReferencedField:]).Value())
#else
        if (_referencedFieldValue == (other.*ReferencedField).Value())
#endif
            return *this;
        _loaded = true;
        _record = std::make_unique<ReferencedRecord>(other);
#if defined(LIGHTWEIGHT_CXX26_REFLECTION)
        _referencedFieldValue = (other.[:ReferencedField:]).Value();
#else
        _referencedFieldValue = (other.*ReferencedField).Value();
#endif
        _modified = true;
        return *this;
    }

    BelongsTo& operator=(BelongsTo const& other)
    {
        if (this == &other)
            return *this;

        _referencedFieldValue = other._referencedFieldValue;
        _loader = std::move(other._loader);
        _loaded = other._loaded;
        _modified = other._modified;
        _record = other._record ? std::make_unique<ReferencedRecord>(*other._record) : nullptr;

        return *this;
    }

    BelongsTo& operator=(BelongsTo&& other) noexcept
    {
        if (this == &other)
            return *this;
        _referencedFieldValue = std::move(other._referencedFieldValue);
        _loader = std::move(other._loader);
        _loaded = other._loaded;
        _modified = other._modified;
        _record = std::move(other._record);
        other._loaded = false;
        return *this;
    }

    ~BelongsTo() noexcept = default;

    // clang-format off

    /// Marks the field as modified or unmodified.
    LIGHTWEIGHT_FORCE_INLINE constexpr void SetModified(bool value) noexcept { _modified = value; }

    /// Checks if the field is modified.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr bool IsModified() const noexcept { return _modified; }

    /// Retrieves the reference to the value of the field.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr ValueType const& Value() const noexcept { return _referencedFieldValue; }

    /// Retrieves the mutable reference to the value of the field.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr ValueType& MutableValue() noexcept { return _referencedFieldValue; }

    /// Retrieves a record from the relationship.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr ReferencedRecord& Record() { RequireLoaded(); return *_record; }

    /// Retrieves an immutable reference to the record from the relationship.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr ReferencedRecord const& Record() const { RequireLoaded(); return *_record; }

    /// Checks if the record is loaded into memory.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr bool IsLoaded() const noexcept { return _loaded; }

    /// Unloads the record from memory.
    LIGHTWEIGHT_FORCE_INLINE void Unload() noexcept { _record = nullptr; _loaded = false; }

    /// Retrieves the record from the relationship.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr ReferencedRecord& operator*() noexcept { RequireLoaded(); return *_record; }

    /// Retrieves the record from the relationship.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr ReferencedRecord const& operator*() const noexcept { RequireLoaded(); return *_record; }

    /// Retrieves the record from the relationship.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr ReferencedRecord* operator->() { RequireLoaded(); return _record.get(); }

    /// Retrieves the record from the relationship.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr ReferencedRecord const* operator->() const { RequireLoaded(); return _record.get(); }

    /// Checks if the field value is NULL.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr bool operator!() const noexcept { return !_referencedFieldValue; }

    /// Checks if the field value is not NULL.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr explicit operator bool() const noexcept { return static_cast<bool>(_referencedFieldValue); }

    // clang-format on

    /// Emplaces a record into the relationship. This will mark the relationship as loaded.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr ReferencedRecord& EmplaceRecord()
    {
        _loaded = true;
        _record = std::make_unique<ReferencedRecord>();
        return *_record;
    }

    LIGHTWEIGHT_FORCE_INLINE void BindOutputColumn(SQLSMALLINT outputIndex, SqlStatement& stmt)
    {
        stmt.BindOutputColumn(outputIndex, &_referencedFieldValue);
    }

    std::weak_ordering operator<=>(BelongsTo const& other) const noexcept
    {
        return _referencedFieldValue <=> other.Value();
    }

    template <detail::FieldElementType T, PrimaryKey IsPrimaryKeyValue = PrimaryKey::No>
    std::weak_ordering operator<=>(Field<T, IsPrimaryKeyValue> const& other) const noexcept
    {
        return _referencedFieldValue <=> other.Value();
    }

    bool operator==(BelongsTo const& other) const noexcept
    {
        return (_referencedFieldValue <=> other.Value()) == std::weak_ordering::equivalent;
    }

    bool operator!=(BelongsTo const& other) const noexcept
    {
        return (_referencedFieldValue <=> other.Value()) != std::weak_ordering::equivalent;
    }

    template <detail::FieldElementType T, PrimaryKey IsPrimaryKeyValue = PrimaryKey::No>
    bool operator==(Field<T, IsPrimaryKeyValue> const& other) const noexcept
    {
        return (_referencedFieldValue <=> other.Value()) == std::weak_ordering::equivalent;
    }

    template <detail::FieldElementType T, PrimaryKey IsPrimaryKeyValue = PrimaryKey::No>
    bool operator!=(Field<T, IsPrimaryKeyValue> const& other) const noexcept
    {
        return (_referencedFieldValue <=> other.Value()) != std::weak_ordering::equivalent;
    }

    struct Loader
    {
        std::function<std::optional<ReferencedRecord>()> loadReference {};
    };

    /// Used internally to configure on-demand loading of the record.
    void SetAutoLoader(Loader loader) noexcept
    {
        _loader = std::move(loader);
    }

  private:
    void RequireLoaded()
    {
        if (_loaded)
            return;

        if (_loader.loadReference)
        {
            auto value = _loader.loadReference();
            if (value)
            {
                _record = std::make_unique<ReferencedRecord>(std::move(value.value()));
                _loaded = true;
            }
        }

        if (!_loaded)
            throw SqlRequireLoadedError(Reflection::TypeNameOf<std::remove_cvref_t<decltype(*this)>>);
    }

    ValueType _referencedFieldValue {};
    Loader _loader {};
    bool _loaded = false;
    bool _modified = false;
    std::unique_ptr<ReferencedRecord> _record {};
};

template <auto ReferencedField, auto ColumnNameOverrideString, SqlNullable Nullable>
std::ostream& operator<<(std::ostream& os, BelongsTo<ReferencedField, ColumnNameOverrideString, Nullable> const& belongsTo)
{
    return os << belongsTo.Value();
}

namespace detail
{
    template <typename T>
    struct IsBelongsToType: std::false_type
    {
    };

    template <auto ReferencedField, auto ColumnNameOverrideString, SqlNullable Nullable>
    struct IsBelongsToType<BelongsTo<ReferencedField, ColumnNameOverrideString, Nullable>>: std::true_type
    {
    };

} // namespace detail

template <typename T>
constexpr bool IsBelongsTo = detail::IsBelongsToType<std::remove_cvref_t<T>>::value;

template <typename T>
concept is_belongs_to = IsBelongsTo<T>;

template <typename T>
constexpr bool IsOptionalBelongsTo = false;

template <is_belongs_to T>
constexpr bool IsOptionalBelongsTo<T> = T::IsOptional;

template <auto ReferencedField, auto ColumnNameOverrideString, SqlNullable Nullable>
struct SqlDataBinder<BelongsTo<ReferencedField, ColumnNameOverrideString, Nullable>>
{
    using SelfType = BelongsTo<ReferencedField, ColumnNameOverrideString, Nullable>;
    using InnerType = typename SelfType::ValueType;

    static constexpr auto ColumnType = SqlDataBinder<InnerType>::ColumnType;

    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN InputParameter(SQLHSTMT stmt,
                                                             SQLUSMALLINT column,
                                                             SelfType const& value,
                                                             SqlDataBinderCallback& cb)
    {
        return SqlDataBinder<InnerType>::InputParameter(stmt, column, value.Value(), cb);
    }

    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN
    OutputColumn(SQLHSTMT stmt, SQLUSMALLINT column, SelfType* result, SQLLEN* indicator, SqlDataBinderCallback& cb)
    {
        auto const sqlReturn = SqlDataBinder<InnerType>::OutputColumn(stmt, column, &result->MutableValue(), indicator, cb);
        cb.PlanPostProcessOutputColumn([result]() { result->SetModified(true); });
        return sqlReturn;
    }

    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN GetColumn(
        SQLHSTMT stmt, SQLUSMALLINT column, SelfType* result, SQLLEN* indicator, SqlDataBinderCallback const& cb) noexcept
    {
        auto const sqlReturn = SqlDataBinder<InnerType>::GetColumn(stmt, column, &result->MutableValue(), indicator, cb);
        if (SQL_SUCCEEDED(sqlReturn))
            result->SetModified(true);
        return sqlReturn;
    }
};

} // namespace Lightweight
