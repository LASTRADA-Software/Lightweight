// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../SqlStatement.hpp"
#include "../Utils.hpp"
#include "Error.hpp"
#include "Record.hpp"

#include <array>
#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

namespace Lightweight
{

/// @brief One column pair of a composite foreign key: "this record's column references that one".
///
/// Both endpoints are pointers-to-member, so the pairing is part of the type. That is the whole point:
/// with two parallel column *lists* the pairing would be implicit in position, and transposing two
/// same-typed columns would be silently wrong. Here a transposition is a different `Connection`, and
/// where the paired columns differ in type it does not even compile.
///
/// @tparam FromPtr Pointer to the member of *this* record holding part of the foreign key.
/// @tparam IntoPtr Pointer to the member of the referenced record it points at, which must be a
///                 primary key there.
///
/// @ingroup DataMapper
///
/// @code
/// CompositeForeignKey<Connection<&Child::refA, &Parent::partA>,
///                     Connection<&Child::refB, &Parent::partB>> parent;
/// @endcode
template <auto FromPtr, auto IntoPtr>
struct Connection
{
    /// Pointer to this record's foreign key member.
    static constexpr auto From = FromPtr;

    /// Pointer to the referenced record's primary key member.
    static constexpr auto Into = IntoPtr;

#if defined(LIGHTWEIGHT_CXX26_REFLECTION)
    /// The record this connection starts from.
    using FromRecord = MemberClassType<FromPtr>;

    /// The record this connection points at.
    using IntoRecord = MemberClassType<IntoPtr>;
#else
    /// The record this connection starts from.
    using FromRecord = MemberClassType<decltype(FromPtr)>;

    /// The record this connection points at.
    using IntoRecord = MemberClassType<decltype(IntoPtr)>;
#endif

    /// The field type on this record's side, e.g. `Field<int32_t>`.
    using FromField = std::remove_cvref_t<decltype(std::declval<FromRecord const&>().*FromPtr)>;

    /// The field type on the referenced record's side.
    using IntoField = std::remove_cvref_t<decltype(std::declval<IntoRecord const&>().*IntoPtr)>;

    /// Member index of the referenced member within the referenced record.
    ///
    /// This is what makes the relation independent of the order its connections are written in: the
    /// `WHERE` clause of a primary key lookup is emitted in the referenced record's *member
    /// declaration* order, so values must be permuted into that order before being bound. See
    /// @ref CompositeForeignKey::OrderedValuesOf.
    static constexpr std::size_t IntoMemberIndex = MemberIndexOf<IntoPtr>;
};

namespace detail
{
    template <typename T>
    struct IsConnectionType: std::false_type
    {
    };

    template <auto FromPtr, auto IntoPtr>
    struct IsConnectionType<Connection<FromPtr, IntoPtr>>: std::true_type
    {
    };
} // namespace detail

/// @brief Satisfied by @ref Connection specializations.
///
/// @ingroup DataMapper
template <typename T>
concept ConnectionType = detail::IsConnectionType<std::remove_cvref_t<T>>::value;

/// @brief Represents a foreign key spanning several columns.
///
/// Declared as a list of @ref Connection, each pairing one of this record's columns with the column it
/// references. The referenced and referencing records are *derived* from those pointers rather than
/// named again, so they cannot disagree with the connections.
///
/// This member holds no column of its own. Every foreign key column is an ordinary `Field` on the
/// record - one data member per database column - and this relation only ties them together and
/// navigates. It therefore contributes nothing to `RecordColumnCount` and is skipped by every
/// projection, exactly as `HasMany` and `HasOneThrough` are.
///
/// Connections may be listed in any order. Values are permuted into the referenced record's member
/// order before binding, because that is the order a primary key lookup emits its predicates in - see
/// @ref OrderedValuesOf and `src/tests/CompositeKeyOrderingTests.cpp`.
///
/// @tparam Connections One @ref Connection per column of the foreign key.
///
/// @ingroup DataMapper
///
/// @code
/// struct Parent
/// {
///     Field<int32_t, PrimaryKey::AutoAssign, SqlRealName { "part_a" }> partA;
///     Field<int32_t, PrimaryKey::AutoAssign, SqlRealName { "part_b" }> partB;
/// };
/// struct Child
/// {
///     Field<int32_t, PrimaryKey::AutoAssign> id;
///     Field<int32_t, SqlRealName { "ref_a" }> refA;
///     Field<int32_t, SqlRealName { "ref_b" }> refB;
///     CompositeForeignKey<Connection<&Child::refA, &Parent::partA>,
///                         Connection<&Child::refB, &Parent::partB>> parent;
/// };
/// @endcode
template <ConnectionType... Connections>
class CompositeForeignKey
{
    static_assert(sizeof...(Connections) > 0, "A composite foreign key must connect at least one column.");

  public:
    /// Number of columns this foreign key spans.
    static constexpr std::size_t Count = sizeof...(Connections);

    /// The record holding the foreign key, i.e. the one declaring this member.
    using Child = std::tuple_element_t<0, std::tuple<typename Connections::FromRecord...>>;

    /// The referenced record.
    using ReferencedRecord = std::tuple_element_t<0, std::tuple<typename Connections::IntoRecord...>>;

    static_assert((std::same_as<typename Connections::FromRecord, Child> && ...),
                  "Every Connection of a composite foreign key must start at the same record. "
                  "Check that each Connection's first pointer-to-member names this record.");

    static_assert((std::same_as<typename Connections::IntoRecord, ReferencedRecord> && ...),
                  "Every Connection of a composite foreign key must point at the same record. "
                  "A foreign key references one table; splitting it across two is not expressible.");

    // Compared by *value* type rather than by field type: `SqlRealName` is part of `Field`'s type, so
    // two columns holding the same kind of value have different field types whenever their column
    // names differ - which is almost always. The value type is what has to line up for the comparison
    // the database will perform.
    static_assert((std::same_as<typename Connections::FromField::ValueType, typename Connections::IntoField::ValueType>
                   && ...),
                  "Each connected column pair must hold the same value type. A mismatch here usually "
                  "means two connections were transposed.");

    static_assert((Connections::IntoField::IsPrimaryKey && ...),
                  "A composite foreign key must reference primary key columns. "
                  "Check the PrimaryKey marker on the referenced record's members.");

    /// The tuple of foreign key values, in the order the connections are declared.
    using ValueType = std::tuple<typename Connections::FromField::ValueType...>;

    /// Reads this record's foreign key values, in the order the connections are declared.
    ///
    /// The values are not stored on the relation: there is exactly one copy of each, in the `Field`
    /// that owns the column, so nothing can fall out of sync.
    ///
    /// @param record The record holding the foreign key.
    /// @return The values, in declaration order of the connections.
    [[nodiscard]] static ValueType ValuesOf(Child const& record)
    {
        return ValueType { (record.*Connections::From).Value()... };
    }

    /// Reads this record's foreign key values, permuted into the referenced record's member order.
    ///
    /// This is the order they must be bound in. A primary key lookup emits one `WHERE` predicate per
    /// primary key member, in that record's *member declaration* order, and binds its arguments
    /// positionally - so passing values in connection order would bind them to the wrong predicates
    /// whenever the two orders differ. With same-typed key columns that yields a wrong row rather than
    /// an error, which is why the permutation is done here rather than left to the caller.
    ///
    /// @param record The record holding the foreign key.
    /// @return The values, ordered to match the referenced record's primary key members.
    [[nodiscard]] static ValueType OrderedValuesOf(Child const& record)
    {
        auto values = ValuesOf(record);
        auto ordered = values;

        // Rank each connection by its referenced member index; the rank is the slot its value belongs
        // in. Done over indices rather than by sorting a runtime container so the whole permutation is
        // fixed at compile time for the common (already-ordered) case.
        constexpr auto intoIndices = std::array { Connections::IntoMemberIndex... };

        [&]<std::size_t... I>(std::index_sequence<I...>) {
            (
                [&] {
                    // How many connections reference an earlier member than this one does.
                    auto rank = std::size_t { 0 };
                    for (auto const other: intoIndices)
                        if (other < intoIndices[I])
                            ++rank;
                    AssignAt(ordered, rank, std::get<I>(values));
                }(),
                ...);
        }(std::index_sequence_for<Connections...> {});

        return ordered;
    }

    /// @return The referenced record, loading it on first access.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE ReferencedRecord const& Record() const
    {
        RequireLoaded();
        return *_record;
    }

    /// @return `true` if the referenced record has been loaded.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr bool IsLoaded() const noexcept
    {
        return _record.get() != nullptr;
    }

    /// Discards the loaded record, so the next access loads it again.
    LIGHTWEIGHT_FORCE_INLINE void Unload() noexcept
    {
        _record.reset();
    }

    /// Adopts an already-fetched referenced record, marking the relation loaded.
    ///
    /// @param record The fetched record.
    LIGHTWEIGHT_FORCE_INLINE constexpr void EmplaceRecord(std::shared_ptr<ReferencedRecord> record) noexcept
    {
        _record = std::move(record);
    }

    /// @return A pointer to the referenced record, loading it on first access.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr ReferencedRecord const* operator->() const
    {
        RequireLoaded();
        return _record.get();
    }

    /// Carries the deferred load, installed by the DataMapper.
    struct Loader
    {
        std::function<std::shared_ptr<ReferencedRecord>()> loadReference {};
    };

    /// Used internally to configure on-demand loading of the referenced record.
    ///
    /// @param loader The loader to install.
    void SetAutoLoader(Loader loader)
    {
        _loader = std::move(loader);
    }

  private:
    /// Assigns @p value into the tuple slot at the runtime index @p slot.
    ///
    /// A tuple cannot be indexed at runtime, so the index is matched against the compile-time pack.
    /// Every element of the permutation has the same type here - the static_asserts above guarantee
    /// pairwise type equality - so only the matching slot is written.
    template <typename Tuple, typename Value>
    static constexpr void AssignAt(Tuple& tuple, std::size_t slot, Value const& value)
    {
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            (((I == slot) ? (void) (std::get<I>(tuple) = value) : (void) 0), ...);
        }(std::make_index_sequence<std::tuple_size_v<Tuple>> {});
    }

    void RequireLoaded() const
    {
        if (_record)
            return;

        if (_loader.loadReference)
            _record = _loader.loadReference();

        if (!_record)
            throw SqlRequireLoadedError(Reflection::TypeNameOf<std::remove_cvref_t<decltype(*this)>>);
    }

    Loader _loader {};
    mutable std::shared_ptr<ReferencedRecord> _record {};
};

namespace detail
{
    template <typename T>
    struct IsCompositeForeignKeyType: std::false_type
    {
    };

    template <ConnectionType... Connections>
    struct IsCompositeForeignKeyType<CompositeForeignKey<Connections...>>: std::true_type
    {
    };
} // namespace detail

/// @brief Whether @p T is a @ref CompositeForeignKey.
///
/// @ingroup DataMapper
template <typename T>
constexpr bool IsCompositeForeignKey = detail::IsCompositeForeignKeyType<std::remove_cvref_t<T>>::value;

} // namespace Lightweight
