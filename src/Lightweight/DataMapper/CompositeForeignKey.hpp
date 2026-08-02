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

namespace detail
{
    /// Extracts the member type from a pointer-to-member, without requiring the owning class to be
    /// complete. `decltype(std::declval<Owner const&>().*Ptr)` would require completeness, which a
    /// relation declared *inside* its own record cannot offer.
    template <typename T>
    struct MemberPointeeType;

    template <typename Member, typename Owner>
    struct MemberPointeeType<Member Owner::*>
    {
        using type = Member;
    };
} // namespace detail

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

#if defined(LIGHTWEIGHT_CXX26_REFLECTION)
    /// The field type on this record's side, e.g. `Field<int32_t>`.
    using FromField = std::remove_cvref_t<typename[:std::meta::type_of(FromPtr):]>;

    /// The field type on the referenced record's side.
    using IntoField = std::remove_cvref_t<typename[:std::meta::type_of(IntoPtr):]>;
#else
    /// The field type on this record's side, e.g. `Field<int32_t>`.
    ///
    /// Taken from the pointer-to-member's own type rather than from a `declval` of the owning record:
    /// the declaring record is incomplete while this relation is instantiated as one of its members.
    using FromField = std::remove_cvref_t<typename detail::MemberPointeeType<decltype(FromPtr)>::type>;

    /// The field type on the referenced record's side.
    using IntoField = std::remove_cvref_t<typename detail::MemberPointeeType<decltype(IntoPtr)>::type>;
#endif

    /// Reads this connection's field out of @p record.
    ///
    /// Wraps the member access so the two reflection modes differ in exactly one place: the
    /// non-reflection branch uses a pointer-to-member, while the C++26 branch splices the reflection.
    ///
    /// @param record The record holding the foreign key.
    /// @return A reference to the field.
    template <typename RecordT = FromRecord>
    [[nodiscard]] static auto const& FieldOf(RecordT const& record) noexcept
    {
#if defined(LIGHTWEIGHT_CXX26_REFLECTION)
        return record.[:FromPtr:];
#else
        return record.*FromPtr;
#endif
    }

    /// Member index of this record's foreign key member within its own record.
    ///
    /// A function rather than a variable: the declaring record is still incomplete while this relation
    /// is instantiated as one of its members, so reflecting over it has to wait until first use.
    [[nodiscard]] static consteval std::size_t FromMemberIndex() noexcept
    {
        return MemberIndexOf<FromPtr>;
    }

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
///     // Composite key members must not be PrimaryKey::AutoAssign: auto-assignment yields one value
///     // that would be written into every key member. See GenerateAutoAssignPrimaryKey.
///     Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName { "part_a" }> partA;
///     Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName { "part_b" }> partB;
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

    // A foreign key pointing at its own record through the same member is degenerate: it would read a
    // value out of a record and then look that same record up by it. Each endpoint on its own passes
    // every check above, so the pairing has to be rejected explicitly.

    // The permutation below ranks each connection by how many name an earlier referenced member, which
    // is only a total ordering while those indices are distinct. Two connections naming the same
    // referenced member would share a rank, leaving one key slot unwritten and binding a
    // default-constructed value against a real predicate - a wrong-row lookup with no diagnostic.
    static_assert(
        []() consteval {
            auto const indices = std::array { Connections::IntoMemberIndex... };
            for (auto outer = std::size_t { 0 }; outer != indices.size(); ++outer)
                for (auto inner = outer + 1; inner != indices.size(); ++inner)
                    if (indices[outer] == indices[inner])
                        return false;
            return true;
        }(),
        "Two Connections of a composite foreign key reference the same member of the referenced "
        "record. Each column of the key must be connected exactly once.");

    // NB: the "connections cover the whole referenced key" check cannot live here. This class is
    // instantiated while the *declaring* record is still incomplete - the relation is one of its
    // members - and RecordPrimaryKeyCount reflects over the referenced record, which in a mutually
    // referencing pair is equally incomplete at that point. It is therefore checked in
    // AssertCoversReferencedKey() below, which runs from the value accessors, i.e. at first use, when
    // both records are complete.

    /// The tuple of foreign key values, in the order the connections are declared.
    using ValueType = std::tuple<typename Connections::FromField::ValueType...>;

  private:
    /// Referenced member index of each connection, in declaration order.
    static constexpr auto IntoIndices = std::array { Connections::IntoMemberIndex... };

    /// Index of the connection whose referenced member comes @p Slot-th in the referenced record.
    ///
    /// Computed by counting how many connections name an earlier member, which is a total ranking
    /// because the referenced indices are asserted pairwise distinct above.
    template <std::size_t Slot>
    static constexpr std::size_t ConnectionForSlot = []() consteval {
        for (auto candidate = std::size_t { 0 }; candidate != Count; ++candidate)
        {
            auto rank = std::size_t { 0 };
            for (auto const other: IntoIndices)
                if (other < IntoIndices[candidate])
                    ++rank;
            if (rank == Slot)
                return candidate;
        }
        return Count; // unreachable: the ranking is a bijection onto [0, Count)
    }();

    /// The connection occupying @p Slot of the referenced record's key order.
    template <std::size_t Slot>
    using ConnectionAtSlot = std::tuple_element_t<ConnectionForSlot<Slot>, std::tuple<Connections...>>;

    /// Reads the value belonging in @p Slot straight out of the record's own field.
    template <std::size_t Slot>
    [[nodiscard]] static decltype(auto) ValueAtSlot(Child const& record)
    {
        return ConnectionAtSlot<Slot>::FieldOf(record).Value();
    }

  public:
    /// The tuple of foreign key values, ordered to match the referenced record's key members.
    ///
    /// Differs from @ref ValueType whenever the connections are not written in the referenced record's
    /// member order, and differs in *type* too when the key columns are heterogeneous.
    using OrderedValueType = decltype([]<std::size_t... Slot>(std::index_sequence<Slot...>) {
        return std::tuple<typename ConnectionAtSlot<Slot>::FromField::ValueType...> {};
    }(std::index_sequence_for<Connections...> {}));

    /// Reads this record's foreign key values, in the order the connections are declared.
    ///
    /// The values are not stored on the relation: there is exactly one copy of each, in the `Field`
    /// that owns the column, so nothing can fall out of sync.
    ///
    /// @param record The record holding the foreign key.
    /// @return The values, in declaration order of the connections.
    [[nodiscard]] static ValueType ValuesOf(Child const& record)
    {
        AssertCoversReferencedKey();
        return ValueType { Connections::FieldOf(record).Value()... };
    }

    /// Checks that the connections cover the referenced record's whole primary key.
    ///
    /// Deferred to first use rather than asserted in the class body: at class-instantiation time the
    /// referenced record can still be incomplete, so reflecting over its members is not yet possible.
    /// A partial key would otherwise surface only as an argument-count mismatch thrown from the first
    /// navigation, far from the declaration that caused it.
    static constexpr void AssertCoversReferencedKey() noexcept
    {
        static_assert(Count == RecordPrimaryKeyCount<ReferencedRecord>,
                      "A composite foreign key must connect every primary key column of the referenced "
                      "record. Connecting only some of them cannot identify a row.");

        // Also deferred, and for the same reason: recovering a member index reflects over the owning
        // record. Only the same-record case can be degenerate - across records the two endpoints are
        // different entities by construction.
        static_assert(((!std::same_as<typename Connections::FromRecord, typename Connections::IntoRecord>
                        || Connections::FromMemberIndex() != Connections::IntoMemberIndex)
                       && ...),
                      "A Connection must join two different members. Pairing a member with itself reads "
                      "a value out of a record only to look the same record up by it.");
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
    [[nodiscard]] static OrderedValueType OrderedValuesOf(Child const& record)
    {
        // The permutation is entirely a compile-time property of the connection list, so the ordered
        // tuple is *built* by index rather than default-constructed and then assigned into. That keeps
        // heterogeneous keys working - assigning through a runtime-matched index would require every
        // slot's assignment to be well-formed, which fails as soon as two key columns differ in type -
        // and it needs no default-constructible value type.
        AssertCoversReferencedKey();
        return [&]<std::size_t... Slot>(std::index_sequence<Slot...>) {
            return OrderedValueType { ValueAtSlot<Slot>(record)... };
        }(std::index_sequence_for<Connections...> {});
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
