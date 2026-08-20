// SPDX-License-Identifier: Apache-2.0
//
// Coverage instrumentation aid — this translation unit intentionally contains no test cases.
//
// DataMapper is a non-template class whose API is almost entirely *member function templates*
// defined in headers. gcov/lcov emit coverage records per *instantiation*, not per template
// definition: a member template that no test ever instantiates produces no .gcno entry at all, so
// its lines are absent from the tracefile rather than being reported as zero-hit. lcov computes
// hit/found over the lines it knows about, which means uninstantiated code silently inflates the
// reported percentage instead of lowering it.
//
// Forcing instantiation here puts those lines into the .gcno notes, so the baseline capture
// (lcov --capture --initial, see cmake/Coverage.cmake) records them with a zero hit count. The
// headline number then reflects "of the DataMapper API, how much do the tests execute" rather than
// "of the DataMapper API the tests happen to instantiate, how much do they execute".
//
// Adding a member template to DataMapper without listing it here leaves it invisible to coverage
// again — extend the lists below when the API grows.

// Utils.hpp must precede Entities.hpp: it defines the `Member(...)` macro that the entity
// declarations use to name primary keys, and its spelling differs between the reflection and
// non-reflection builds.
#include "../Utils.hpp"
#include "Entities.hpp"

#include <Lightweight/Lightweight.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

/// Declared but never defined: yields a builder reference without constructing one (a real builder
/// needs a live DataMapper and a live database connection). Only ever named inside functions that
/// are themselves never called, so the missing definition can never be linked against.
///
/// Deliberately at external linkage — inside the anonymous namespace below, an undefined internal
/// function trips -Wundefined-internal, which is fatal under PEDANTIC_COMPILER_WERROR.
template <typename Record>
Lightweight::SqlAllFieldsQueryBuilder<Record, Lightweight::DataMapperOptions {}>& UnreachableBuilder();

namespace
{

/// Forces code generation for a member function template without calling it.
///
/// Taking the address of a specialization ODR-uses it, which makes the compiler emit the function
/// body (and therefore its gcov arc records). A plain explicit-instantiation declaration is harder
/// to spell for these members: several carry a defaulted non-type template parameter
/// (`DataMapperOptions QueryOptions = {}`), variadic parameter packs, or participate in constrained
/// overload sets, all of which make the explicit-instantiation syntax brittle. Storing the pointer
/// in a volatile sink also stops the optimizer from discarding it.
template <typename Signature>
void ForceInstantiation(Signature function)
{
    static Signature volatile sink;
    sink = function;
}

/// Instantiates the record-centric DataMapper API for a single record type.
///
/// Covers the CRUD, table-management, state-tracking and relation-loading members that a user of
/// `Record` would reach for. Async counterparts are instantiated alongside their sync versions
/// because the coroutine bodies are separate specializations with their own coverage records.
template <typename Record>
void InstantiateRecordApi()
{
    using Light::DataMapper;

    // Table management.
    ForceInstantiation(&DataMapper::CreateTableString<Record>);
    ForceInstantiation(&DataMapper::CreateTablesString<Record>);
    ForceInstantiation(&DataMapper::CreateTable<Record>);
    ForceInstantiation(&DataMapper::CreateTables<Record>);

    // Create.
    ForceInstantiation(&DataMapper::Create<{}, Record>);
    ForceInstantiation(&DataMapper::CreateExplicit<Record>);
    ForceInstantiation(&DataMapper::CreateCopyOf<{}, Record>);
    ForceInstantiation(&DataMapper::CreateAll<std::vector<Record>>);

    // Read.
    ForceInstantiation(&DataMapper::QuerySingle<Record, {}, Light::RecordPrimaryKeyType<Record>>);
    ForceInstantiation(static_cast<std::vector<Record> (DataMapper::*)(Light::SqlSelectQueryBuilder::ComposedQuery const&)>(
        &DataMapper::Query<Record, {}>));
    ForceInstantiation(static_cast<std::vector<Record> (DataMapper::*)(std::string_view)>(&DataMapper::Query<Record, {}>));
    ForceInstantiation(
        static_cast<Light::SqlAllFieldsQueryBuilder<Record, {}> (DataMapper::*)()>(&DataMapper::Query<Record, {}>));
    ForceInstantiation(&DataMapper::QueryAsync<Record, {}>);

    // Update / delete.
    ForceInstantiation(&DataMapper::Update<Record>);
    ForceInstantiation(&DataMapper::UpdateAll<std::vector<Record>>);
    ForceInstantiation(&DataMapper::Delete<Record>);

    // State tracking.
    ForceInstantiation(&DataMapper::IsModified<Record>);
    ForceInstantiation(&DataMapper::SetModifiedState<DataMapper::ModifiedState::Modified, Record>);
    ForceInstantiation(&DataMapper::SetModifiedState<DataMapper::ModifiedState::NotModified, Record>);

    // Relations.
    ForceInstantiation(&DataMapper::LoadRelations<Record>);
    ForceInstantiation(&DataMapper::ConfigureRelationAutoLoading<Record>);

    // Introspection. Only the public surface — private helpers such as
    // BuildFullyQualifiedFieldList() are instantiated transitively by the members above.
    ForceInstantiation(&DataMapper::Inspect<Record>);

    // Asynchronous surface.
    ForceInstantiation(&DataMapper::CreateAsync<{}, Record>);
    ForceInstantiation(&DataMapper::QuerySingleAsync<Record, {}, Light::RecordPrimaryKeyType<Record>>);
    ForceInstantiation(&DataMapper::UpdateAsync<Record>);
    ForceInstantiation(&DataMapper::DeleteAsync<Record>);
    ForceInstantiation(&DataMapper::LoadRelationsAsync<Record>);
}

/// Instantiates the multi-record query overload that returns tuples of joined records.
template <typename First, typename Second>
void InstantiateTupleQueryApi()
{
    // Spelled out rather than deduced: this overload is variadic in its record pack, so the
    // address-of expression is ambiguous against the single-record Query() overloads.
    ForceInstantiation(static_cast<std::vector<std::tuple<First, Second>> (Light::DataMapper::*)(
                           Light::SqlSelectQueryBuilder::ComposedQuery const&)>(&Light::DataMapper::Query<First, Second>));
}

/// Instantiates the scalar Execute() helper for the value types it is used with.
void InstantiateExecuteApi()
{
    ForceInstantiation(&Light::DataMapper::Execute<int>);
    ForceInstantiation(&Light::DataMapper::Execute<std::size_t>);
    ForceInstantiation(&Light::DataMapper::Execute<std::string>);
}

/// Instantiates the query-builder members that are reached only indirectly through
/// DataMapper::Query(), and whose sparse-field / paging overloads are otherwise dark.
///
/// SqlCoreDataMapperQueryBuilder has no direct mention anywhere in the test suite — every use goes
/// through DataMapper::Query(), which only instantiates the handful of members that particular call
/// chain touches. The rest (First(n), Range(offset, limit), the single-field All<Field>() and the
/// sparse-field All<Fields...>() overloads) never reach the tracefile at all.
/// @tparam Record  The record type the builder materializes.
/// @tparam FieldA  A member pointer into @p Record, for the single-field projection overloads.
/// @tparam FieldB  A second member pointer, for the sparse-field (>= 2) projection overloads.
template <typename Record, auto FieldA, auto FieldB>
void InstantiateQueryBuilderApi()
{
    auto& builder = UnreachableBuilder<Record>();

    // Called rather than addressed. These members form constrained overload sets — All() has a
    // whole-record form, a single-field form requiring a member-object pointer, and a sparse-field
    // form requiring `sizeof...(ReferencedFields) >= 2` — so an address-of expression is ambiguous
    // and a disambiguating cast has to restate a return type that is `auto`-deduced from a lambda.
    // A call expression picks the right overload by ordinary lookup and instantiates exactly it.
    //
    // This function is never invoked (see InstantiateDataMapperApi below), so the calls never run;
    // they exist only to force the bodies to be emitted.
    (void) builder.Exist();
    (void) builder.Count();
    (void) builder.Delete();

    (void) builder.All();
    (void) builder.First();
    (void) builder.First(std::size_t { 1 });
    (void) builder.Range(std::size_t { 0 }, std::size_t { 1 });

    // Single-field projection: its own result type (std::vector<FieldType>, not std::vector<Record>).
    (void) builder.template All<FieldA>();
    (void) builder.template First<FieldA>();

    // Sparse-field projection: the `>= 2` constrained overload, a third distinct specialization.
    (void) builder.template All<FieldA, FieldB>();
    (void) builder.template First<FieldA, FieldB>();
}

/// Entry point that pulls every instantiation above into this translation unit.
///
/// Never called — it only has to be compiled. The record types span the interesting shapes:
/// a plain record, a partial-column record, records owning HasMany/BelongsTo relations, a record
/// with HasManyThrough, and a record keyed by a non-GUID primary key.
[[maybe_unused]] void InstantiateDataMapperApi()
{
    InstantiateRecordApi<Person>();
    InstantiateRecordApi<PersonName>();
    InstantiateRecordApi<User>();
    InstantiateRecordApi<Email>();
    InstantiateRecordApi<NullableForeignKeyUser>();
    InstantiateRecordApi<Physician>();
    InstantiateRecordApi<Patient>();
    InstantiateRecordApi<Appointment>();
    InstantiateRecordApi<EntryWithIntPrimaryKey>();

    InstantiateTupleQueryApi<Physician, Appointment>();
    InstantiateTupleQueryApi<Patient, Appointment>();

    InstantiateExecuteApi();

    InstantiateQueryBuilderApi<Person, Member(Person::name), Member(Person::age)>();
    InstantiateQueryBuilderApi<Physician, Member(Physician::id), Member(Physician::name)>();
}

} // namespace

// Relation templates, via explicit class-template instantiation.
//
// These are class templates whose members are mostly *non-template* member functions. Such members
// instantiate lazily — only the ones a test actually calls are emitted — so a per-member address-of
// list would be long and would rot silently. A single explicit instantiation of the class emits
// every member at once, which is both shorter and self-maintaining as the classes grow.
//
// The survey behind this file found these three the thinnest-covered relation types in the library:
// HasManyThrough had 3 mentions across the whole suite against ~18 member functions, HasOneThrough
// 7 against ~10, and HasMany 16 against ~19 — leaving Each(), Unload(), the const begin()/end()
// pair and the const At()/operator[] overloads dark.
template class Lightweight::HasMany<Email>;
template class Lightweight::HasMany<Appointment>;
template class Lightweight::HasManyThrough<Patient, Lightweight::Through<Appointment>>;
template class Lightweight::HasManyThrough<Physician, Lightweight::Through<Appointment>>;

// std::formatter specializations only exist once something formats the type. Nothing in the suite
// std::format()s a Field, so these format() bodies were never instrumented.
template struct std::formatter<Light::Field<Light::SqlAnsiString<25>>>;
template struct std::formatter<Light::Field<int, Light::PrimaryKey::AutoAssign>>;
