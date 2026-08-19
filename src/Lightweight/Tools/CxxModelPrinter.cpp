// SPDX-License-Identifier: Apache-2.0

#include "CxxModelPrinter.hpp"

#include <Lightweight/DataBinder/SqlNumeric.hpp>

#include <array>
#include <fstream>
#include <limits>
#include <tuple>
#include <unordered_map>
#include <variant>

using namespace std::string_view_literals;

namespace Lightweight::Tools
{

static auto ToLower(std::string value) -> std::string
{
    // Cast to unsigned char before std::tolower — passing a signed `char` with the
    // high bit set is undefined behaviour per the standard.
    std::ranges::transform(
        value, value.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    return value;
}

CxxModelPrinter::CxxModelPrinter(Config config) noexcept:
    _config(std::move(std::move(config)))
{
}

auto CxxModelPrinter::StripSuffix(std::string name) -> std::string
{
    std::string const lowerName = ToLower(name);
    for (auto const& suffix: _config.stripSuffixes)
    {
        if (lowerName.ends_with(suffix))
            return name.substr(0, name.length() - suffix.length());
    }
    return name;
}

auto CxxModelPrinter::SanitizeName(std::string name) -> std::string
{
    static constexpr auto cxxKeywords =
        std::array { "alignas"sv,
                     "alignof"sv,
                     "asm"sv,
                     "auto"sv,
                     "bool"sv,
                     "break"sv,
                     "case"sv,
                     "catch"sv,
                     "char"sv,
                     "char16_t"sv,
                     "char32_t"sv,
                     "char8_t"sv,
                     "class"sv,
                     "co_await"sv,
                     "co_return"sv,
                     "co_yield"sv,
                     "concept"sv,
                     "const"sv,
                     "const_cast"sv,
                     "consteval"sv,
                     "constexpr"sv,
                     "constinit"sv,
                     "continue"sv,
                     "decltype"sv,
                     "default"sv,
                     "delete"sv,
                     "do"sv,
                     "double"sv,
                     "dynamic_cast"sv,
                     "else"sv,
                     "enum"sv,
                     "explicit"sv,
                     "export"sv, // For modules
                     "extern"sv,
                     "false"sv,
                     "float"sv,
                     "for"sv,
                     "friend"sv,
                     "goto"sv,
                     "if"sv,
                     "import"sv, // For modules
                     "inline"sv,
                     "int"sv,
                     "long"sv,
                     "module"sv, // For modules
                     "mutable"sv,
                     "namespace"sv,
                     "new"sv,
                     "noexcept"sv,
                     "nullptr"sv,
                     "operator"sv,
                     "private"sv,
                     "protected"sv,
                     "public"sv,
                     "register"sv, // Deprecated in C++11, reserved in C++17, removed in C++23 but still reserved.
                     "reinterpret_cast"sv,
                     "requires"sv,
                     "return"sv,
                     "short"sv,
                     "signed"sv,
                     "sizeof"sv,
                     "static"sv,
                     "static_assert"sv,
                     "static_cast"sv,
                     "struct"sv,
                     "switch"sv,
                     "template"sv,
                     "this"sv,
                     "thread_local"sv,
                     "throw"sv,
                     "true"sv,
                     "try"sv,
                     "typedef"sv,
                     "typeid"sv,
                     "typename"sv,
                     "union"sv,
                     "unsigned"sv,
                     "using"sv,
                     "virtual"sv,
                     "void"sv,
                     "volatile"sv,
                     "wchar_t"sv,
                     "while"sv };

    if (std::ranges::contains(cxxKeywords, name))
        name += "_";

    return name;
}

// "user_id" into "UserId"
// "task_list_entry" into "TaskListEntry"
// "person" into "Person"
// and so on
std::string CxxModelPrinter::FormatTableName(std::string_view name)
{
    std::string result;
    result.reserve(name.size());

    bool makeUpper = true;

    for (auto const& c: name)
    {
        if (c == '_')
        {
            makeUpper = true;
            continue;
        }
        if (makeUpper)
        {
            result += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            makeUpper = false;
        }
        else
        {
            result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }

    return result;
}

std::string CxxModelPrinter::AliasTableName(std::string_view name) const
{
    if (_config.makeAliases)
        return FormatTableName(name);

    return std::string { name };
}

[[nodiscard]] std::expected<void, std::string> CxxModelPrinter::PrintCumulativeHeaderFile(
    std::filesystem::path const& outputDirectory, std::filesystem::path const& cumulativeHeaderFile)
{
    auto const headerFilePath = outputDirectory / cumulativeHeaderFile;

    auto file = std::ofstream(headerFilePath.string());
    if (!file)
        return std::unexpected(std::format("Failed to create file {}.", headerFilePath.string()));

    file << "// File is automatically generated using ddl2cpp.\n"
         << "#pragma once\n"
         << "\n";

    auto includes = std::vector<std::string> {};
    includes.reserve(_definitions.size());
    for (auto const& [tableName, definition]: _definitions)
        includes.emplace_back(std::format("{}.hpp", AliasTableName(tableName)));

    std::ranges::sort(includes);

    for (auto const& include: includes)
        file << std::format("#include \"{}\"\n", include);

    return {};
}

void CxxModelPrinter::PrintToFiles(std::string_view modelNamespace, std::string_view outputDirectory)
{
    std::vector<std::string> instantiationSources;

    for (auto const& [tableName, definition]: _definitions)
    {
        auto const headerBase = AliasTableName(tableName);
        auto const fileName = std::format("{}/{}.hpp", outputDirectory, headerBase);
        auto file = std::ofstream(fileName);
        if (!file)
        {
            std::println("Failed to create file {}.", fileName);
            continue;
        }
        file << HeaderFileForTheTable(modelNamespace, tableName);

        if (!_config.generateInstantiations)
            continue;

        auto const sourceName = std::format("{}.cpp", headerBase);
        auto sourceFile = std::ofstream(std::format("{}/{}", outputDirectory, sourceName));
        if (!sourceFile)
        {
            std::println("Failed to create file {}/{}.", outputDirectory, sourceName);
            continue;
        }
        sourceFile << InstantiationSourceFor(modelNamespace, std::format("{}.hpp", headerBase), definition);
        instantiationSources.emplace_back(sourceName);
    }

    if (_config.generateInstantiations && !instantiationSources.empty())
        WriteInstantiationCMakeLists(outputDirectory, instantiationSources);
}

void CxxModelPrinter::WriteInstantiationCMakeLists(std::string_view outputDirectory,
                                                   std::vector<std::string> instantiationSources) const
{
    std::ranges::sort(instantiationSources);

    auto const cmakePath = std::format("{}/CMakeLists.txt", outputDirectory);
    auto cmake = std::ofstream(cmakePath);
    if (!cmake)
    {
        std::println("Failed to create file {}.", cmakePath);
        return;
    }

    auto const& target = _config.instantiationTargetName;
    cmake << "# File is automatically generated using ddl2cpp.\n"
             "# Builds the explicit template instantiations for the generated records so that consuming\n"
             "# translation units (which see the matching `extern template`) don't re-instantiate the\n"
             "# DataMapper relation machinery. Link this target and add this directory's parent to your\n"
             "# include path, then include the generated headers as usual.\n\n";
    cmake << std::format("add_library({} STATIC\n", target);
    for (auto const& source: instantiationSources)
        cmake << std::format("    {}\n", source);
    cmake << ")\n";
    cmake << std::format("target_compile_features({} PUBLIC cxx_std_23)\n", target);
    cmake << std::format("target_link_libraries({} PUBLIC Lightweight::Lightweight)\n", target);
    cmake << std::format("target_include_directories({} PUBLIC ${{CMAKE_CURRENT_SOURCE_DIR}})\n", target);
}

std::string CxxModelPrinter::RecordDescriptorFor(std::string_view modelNamespace, TableInfo const& info)
{
    if (info.structName.empty() || info.members.empty())
        return {};

    auto const qualifiedName =
        modelNamespace.empty() ? info.structName : std::format("{}::{}", modelNamespace, info.structName);

    std::string memberPointers;
    std::string fieldNames;
    for (auto const& [memberId, sqlName]: info.members)
    {
        if (!memberPointers.empty())
        {
            memberPointers += ", ";
            fieldNames += ", ";
        }
        memberPointers += std::format("&{}::{}", qualifiedName, memberId);
        fieldNames += std::format("\"{}\"", sqlName);
    }

    return std::format("template <>\n"
                       "struct Lightweight::Description<{0}>\n"
                       "{{\n"
                       "    static constexpr std::size_t FieldCount = {1};\n"
                       "    using Members = Lightweight::RecordMemberList<{2}>;\n"
                       "    static constexpr std::array<std::string_view, {1}> FieldNames = {{ {3} }};\n"
                       "}};\n",
                       qualifiedName,
                       info.members.size(),
                       memberPointers,
                       fieldNames);
}

std::string CxxModelPrinter::ExternTemplateDeclarationFor(std::string_view modelNamespace, TableInfo const& info)
{
    if (info.structName.empty())
        return {};

    auto const qualifiedName =
        modelNamespace.empty() ? info.structName : std::format("{}::{}", modelNamespace, info.structName);

    // ConfigureRelationAutoLoading is the entry point through which the entire (recursive) relation
    // machinery is reached, so declaring it `extern template` keeps that whole closure out of every
    // consuming translation unit; it is instantiated exactly once in the matching .cpp below.
    return std::format("#if !defined(LIGHTWEIGHT_BUILD_MODULES)\n"
                       "extern template void Lightweight::DataMapper::ConfigureRelationAutoLoading<{0}>({0}&);\n"
                       "#endif\n",
                       qualifiedName);
}

std::string CxxModelPrinter::InstantiationSourceFor(std::string_view modelNamespace,
                                                    std::string const& headerFileName,
                                                    TableInfo const& info)
{
    if (info.structName.empty())
        return {};

    auto const qualifiedName =
        modelNamespace.empty() ? info.structName : std::format("{}::{}", modelNamespace, info.structName);

    return std::format("// File is automatically generated using ddl2cpp.\n"
                       "#include \"{1}\"\n"
                       "\n"
                       "#if !defined(LIGHTWEIGHT_BUILD_MODULES)\n"
                       "template void Lightweight::DataMapper::ConfigureRelationAutoLoading<{0}>({0}&);\n"
                       "#endif\n",
                       qualifiedName,
                       headerFileName);
}

std::string CxxModelPrinter::HeaderFileForTheTable(std::string_view modelNamespace,
                                                   std::string const& tableName) // NOLINT(readability-identifier-naming)
{
    std::stringstream output;
    output << "// File is automatically generated using ddl2cpp.\n";
    output << "#pragma once\n";
    output << "\n";

    auto const& requiredTables = _definitions[tableName].requiredTables;
    for (auto const& requiredTable: requiredTables)
        output << std::format("#include \"{}.hpp\"\n", requiredTable);
    if (!std::empty(requiredTables))
        output << '\n';

    output << "#if !defined(LIGHTWEIGHT_BUILD_MODULES)\n";
    output << "#include <Lightweight/DataMapper/DataMapper.hpp>\n";
    output << "#endif\n";
    output << "\n";
    // Needed by the Description specialization emitted below (not exported by the module).
    output << "#include <array>\n";
    output << "#include <string_view>\n";
    output << "\n";

    if (!modelNamespace.empty())
        output << std::format("namespace {}\n{{\n", modelNamespace);

    output << "\n";

    // Records named by an inverse or through relation are forward-declared rather than included: the
    // child's header already includes this one, so including it back would be a cycle. Every relation
    // stores its records indirectly, so a declaration suffices here.
    auto const& forwardDeclared = _definitions[tableName].forwardDeclaredTables;
    if (!forwardDeclared.empty())
    {
        for (auto const& declared: forwardDeclared)
            if (declared != _definitions[tableName].structName)
                output << std::format("struct {};\n", declared);
        output << '\n';
    }

    output << _definitions[tableName].text.str();
    if (!modelNamespace.empty())
        output << std::format("}} // end namespace {}\n", modelNamespace);

    // Emit the Description<> specialization at global scope so the DataMapper reads
    // pre-baked metadata instead of evaluating reflection (dramatically faster to compile).
    if (auto descriptor = RecordDescriptorFor(modelNamespace, _definitions[tableName]); !descriptor.empty())
        output << '\n' << descriptor;

    // Keep the heavy relation machinery out of consuming TUs (defined once in the matching .cpp).
    if (_config.generateInstantiations)
        if (auto externDecl = ExternTemplateDeclarationFor(modelNamespace, _definitions[tableName]); !externDecl.empty())
            output << '\n' << externDecl;

    return output.str();
}

namespace
{

    /// @return `true` if @p constraint links exactly one column to exactly one column.
    bool IsSingleColumn(SqlSchema::ForeignKeyConstraint const& constraint) noexcept
    {
        return constraint.foreignKey.columns.size() == 1 && constraint.primaryKey.columns.size() == 1;
    }

    /// Whether @p columnName is covered by a single-column unique index or UNIQUE constraint on @p table,
    /// which is what makes a foreign key through it one-to-one rather than one-to-many.
    ///
    /// @param table Table owning the column.
    /// @param columnName Column to test.
    /// @return `true` if a value in that column identifies at most one row.
    bool IsUniquelyIndexed(SqlSchema::Table const& table, std::string_view columnName)
    {
        auto const column = std::ranges::find_if(table.columns, [&](auto const& c) { return c.name == columnName; });
        if (column != table.columns.end() && column->isUnique)
            return true;

        // A single-column primary key is unique too, as is any single-column unique index.
        if (table.primaryKeys.size() == 1 && table.primaryKeys.front() == columnName)
            return true;

        return std::ranges::any_of(table.indexes, [&](SqlSchema::IndexDefinition const& index) {
            return index.isUnique && index.columns.size() == 1 && index.columns.front() == columnName;
        });
    }

    /// Whether @p table is a pure join table: exactly two single-column foreign keys pointing at two
    /// distinct tables, and no columns of its own beyond those keys.
    ///
    /// A join table carrying extra payload columns is deliberately *not* treated as one. Such a table is
    /// an entity in its own right - the association-object shape - and collapsing it into a
    /// `HasManyThrough` would hide the payload, so it keeps its plain record plus `BelongsTo` members.
    ///
    /// @param table Candidate join table.
    /// @return The two single-column foreign keys if it qualifies, `std::nullopt` otherwise.
    std::optional<std::pair<SqlSchema::ForeignKeyConstraint, SqlSchema::ForeignKeyConstraint>> AsJoinTable(
        SqlSchema::Table const& table)
    {
        auto singleColumnKeys = std::vector<SqlSchema::ForeignKeyConstraint> {};
        for (auto const& constraint: table.foreignKeys)
            if (IsSingleColumn(constraint))
                singleColumnKeys.emplace_back(constraint);

        if (singleColumnKeys.size() != 2 || table.foreignKeys.size() != 2)
            return std::nullopt;

        if (singleColumnKeys[0].primaryKey.table.table == singleColumnKeys[1].primaryKey.table.table)
            return std::nullopt; // both keys point at the same table: not a two-sided join

        // Every column must participate in one of the two foreign keys, or be part of the table's own
        // key. Anything else is payload, which makes this an association object rather than a join table.
        auto const isKeyColumn = [&](std::string const& name) {
            return std::ranges::contains(singleColumnKeys[0].foreignKey.columns, name)
                   || std::ranges::contains(singleColumnKeys[1].foreignKey.columns, name)
                   || std::ranges::contains(table.primaryKeys, name);
        };
        if (!std::ranges::all_of(table.columns, [&](auto const& c) { return isKeyColumn(c.name); }))
            return std::nullopt;

        return std::pair { singleColumnKeys[0], singleColumnKeys[1] };
    }

    /// Emits the `HasOneThrough`/`HasManyThrough` relation for one side of a resolved join table.
    ///
    /// @param table The join table itself.
    /// @param ownerKey The foreign key naming the owner this relation is planned for.
    /// @param farKey The join table's other foreign key, naming the record @p ownerKey's owner reaches.
    /// @param isAmbiguous Whether more than one foreign key runs between two given tables.
    /// @param plan The plan to add the relation to, keyed by owner table name.
    template <typename IsAmbiguousFn>
    void EmitThroughRelation(SqlSchema::Table const& table,
                             SqlSchema::ForeignKeyConstraint const& ownerKey,
                             SqlSchema::ForeignKeyConstraint const& farKey,
                             IsAmbiguousFn const& isAmbiguous,
                             CxxModelPrinter::RelationPlan& plan)
    {
        auto const& ownerTable = ownerKey.primaryKey.table.table;
        auto const& farTable = farKey.primaryKey.table.table;

        // Scalar when the join record's *owner*-side key is uniquely indexed: at most one join row per
        // owner, so the owner sees a single far record rather than a collection. The far key's own
        // uniqueness governs the *other* direction's cardinality, not this one - a join row always
        // references exactly one far row regardless of uniqueness, so checking farKey here would test
        // something that is trivially always true.
        auto const ownerIsUnique = IsUniquelyIndexed(table, ownerKey.foreignKey.columns.front());

        plan[ownerTable].emplace_back(CxxModelPrinter::PlannedRelation {
            .kind = ownerIsUnique ? CxxModelPrinter::PlannedRelation::Kind::HasOneThrough
                                  : CxxModelPrinter::PlannedRelation::Kind::HasManyThrough,
            .ownerTable = ownerTable,
            .referencedTable = farTable,
            .throughTable = table.name,
            .ownerForeignKeyColumn = ownerKey.foreignKey.columns.front(),
            .referencedForeignKeyColumn = farKey.foreignKey.columns.front(),
            .ownerSelectorRequired = isAmbiguous(table.name, ownerTable),
            .referencedSelectorRequired = isAmbiguous(table.name, farTable),
            // Named after the far table: `project.users` rather than `project.projectUsers`, since the
            // join table is an implementation detail of the relation.
            .memberName = farTable,
        });
    }

    /// Emits the inverse `HasOne`/`HasMany` relation implied by one single-column foreign key, unless it
    /// is composite or points outside the generated set.
    ///
    /// @param table The table declaring @p constraint.
    /// @param constraint The candidate foreign key.
    /// @param byName Resolves a table by name within the generated set.
    /// @param isAmbiguous Whether more than one foreign key runs between two given tables.
    /// @param plan The plan to add the relation to, keyed by owner table name.
    template <typename ByNameFn, typename IsAmbiguousFn>
    void EmitInverseRelation(SqlSchema::Table const& table,
                             SqlSchema::ForeignKeyConstraint const& constraint,
                             ByNameFn const& byName,
                             IsAmbiguousFn const& isAmbiguous,
                             CxxModelPrinter::RelationPlan& plan)
    {
        if (!IsSingleColumn(constraint))
            return; // composite: no BelongsTo either, so no inverse

        auto const& ownerTable = constraint.primaryKey.table.table;
        if (byName(ownerTable) == nullptr)
            return; // references a table outside the generated set

        auto const& childColumn = constraint.foreignKey.columns.front();

        // Scalar when the child's own foreign key is uniquely indexed: one child per owner.
        auto const childIsUnique = IsUniquelyIndexed(table, childColumn);

        plan[ownerTable].emplace_back(CxxModelPrinter::PlannedRelation {
            .kind = childIsUnique ? CxxModelPrinter::PlannedRelation::Kind::HasOne
                                  : CxxModelPrinter::PlannedRelation::Kind::HasMany,
            .ownerTable = ownerTable,
            .referencedTable = table.name,
            .throughTable = {},
            .ownerForeignKeyColumn = childColumn,
            .referencedForeignKeyColumn = {},
            .ownerSelectorRequired = isAmbiguous(table.name, ownerTable),
            .referencedSelectorRequired = false,
            // Named after the child table. Where several foreign keys from the same child table land
            // here the names would collide, so distinguish those by the foreign key column - which is
            // exactly the set that also needs a selector.
            .memberName = isAmbiguous(table.name, ownerTable) ? std::format("{}_{}", table.name, childColumn) : table.name,
        });
    }

} // namespace

CxxModelPrinter::RelationPlan CxxModelPrinter::PlanRelations(std::vector<SqlSchema::Table> const& tables)
{
    auto plan = RelationPlan {};

    // Built once up front rather than scanned per lookup: relation planning resolves a table by name
    // for every foreign key of every table, which made the previous linear scan quadratic in schema
    // size (hundreds of tables on the schemas this generator targets).
    auto tablesByName = std::unordered_map<std::string_view, SqlSchema::Table const*> {};
    tablesByName.reserve(tables.size());
    for (auto const& table: tables)
        tablesByName.emplace(table.name, &table);

    auto const byName = [&](std::string_view name) -> SqlSchema::Table const* {
        auto const it = tablesByName.find(name);
        return it != tablesByName.end() ? it->second : nullptr;
    };

    // How many single-column foreign keys run from one table to another. A count above one makes the
    // inverse ambiguous, so the selector naming the foreign key column becomes mandatory.
    auto foreignKeyMultiplicity = std::map<std::pair<std::string, std::string>, size_t> {};
    for (auto const& table: tables)
        for (auto const& constraint: table.foreignKeys)
            if (IsSingleColumn(constraint))
                ++foreignKeyMultiplicity[{ table.name, constraint.primaryKey.table.table }];

    auto const isAmbiguous = [&](std::string const& child, std::string const& owner) {
        auto const it = foreignKeyMultiplicity.find({ child, owner });
        return it != foreignKeyMultiplicity.end() && it->second > 1;
    };

    // Join tables are consumed as through-relations rather than contributing a HasMany of their own,
    // so resolve them first and remember which tables they were.
    auto joinTables = std::set<std::string> {};

    for (auto const& table: tables)
    {
        auto const joinKeys = AsJoinTable(table);
        if (!joinKeys.has_value())
            continue;

        joinTables.emplace(table.name);

        // One through-relation on each side, each hopping to the other side's target.
        auto const& [leftKey, rightKey] = *joinKeys;
        EmitThroughRelation(table, leftKey, rightKey, isAmbiguous, plan);
        EmitThroughRelation(table, rightKey, leftKey, isAmbiguous, plan);
    }

    // Every remaining single-column foreign key yields an inverse collection on the referenced side.
    for (auto const& table: tables)
    {
        if (joinTables.contains(table.name))
            continue;

        for (auto const& constraint: table.foreignKeys)
            EmitInverseRelation(table, constraint, byName, isAmbiguous, plan);
    }

    return plan;
}

SqlSchema::ForeignKeyConstraint const& CxxModelPrinter::GetForeignKey(
    SqlSchema::Column const& column, std::vector<SqlSchema::ForeignKeyConstraint> const& foreignKeys)
{
    auto it = std::ranges::find_if(foreignKeys, [&](SqlSchema::ForeignKeyConstraint const& foreignKey) {
        return std::ranges::contains(foreignKey.foreignKey.columns, column.name);
    });
    if (it != foreignKeys.end())
        return *it;

    throw std::runtime_error(
        std::format("Foreign key not found for {} in table {}",
                    column.name,
                    column.foreignKeyConstraint->foreignKey.table)); // NOLINT(bugprone-unchecked-optional-access)
}

std::string CxxModelPrinter::MakeDecimalPrecisionNote(SqlSchema::Column const& column)
{
    auto const* const decimal = std::get_if<SqlColumnTypeDefinitions::Decimal>(&column.type);
    if (decimal == nullptr)
        return {};

    // Number of significant decimal digits an IEEE-754 double preserves. `SqlNumeric`'s binder
    // deliberately routes SQLite and MS SQL Server through SQL_C_DOUBLE (see
    // `NativeNumericSupportIsBroken`), so on those backends this is all a value carries — no
    // matter how wide the column was declared.
    constexpr auto fallbackDigits = static_cast<std::size_t>(std::numeric_limits<double>::digits10);

    // Widest precision `SqlNumeric` accepts. This is toolchain-independent — `Int128` supplies a
    // software 128-bit carrier where the compiler has no native one — so it is equally the bound of
    // whichever toolchain built ddl2cpp and of whichever one compiles the generated header.
    constexpr auto maxPrecision = SqlMaxNumericPrecision;

    std::string note;

    if (decimal->precision > fallbackDigits)
        note += std::format("    // NOTE: DECIMAL({}, {}) declares {} digits, but SqlNumeric transfers this column through\n"
                            "    //       SQL_C_DOUBLE on SQLite and MS SQL Server, which carries only {}. The low {}\n"
                            "    //       digit(s) are lost silently there — e.g. MS SQL Server reads its own `money`\n"
                            "    //       maximum 922337203685477.5807 back as 922337203685477.6250. Read the column as\n"
                            "    //       a string if those digits matter. See docs/data-binder.md.\n",
                            decimal->precision,
                            decimal->scale,
                            decimal->precision,
                            fallbackDigits,
                            decimal->precision - fallbackDigits);

    if (decimal->precision > maxPrecision)
        note += std::format("    // NOTE: SqlNumeric<{}, {}> does not compile: {} digits exceed the {} this implementation\n"
                            "    //       can carry. Read this column as a string instead, or narrow it.\n",
                            decimal->precision,
                            decimal->scale,
                            decimal->precision,
                            maxPrecision);

    return note;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
std::string CxxModelPrinter::MakeType(
    SqlSchema::Column const& column,
    std::string const& tableName,
    bool forceUnicodeTextColumn,
    std::unordered_map<std::string /*table*/, std::unordered_set<std::string /*column*/>> const& unicodeTextColumnOverrides,
    size_t sqlFixedStringMaxSize)
{
    auto const optional = [&]<typename T>(T&& type) {
        if (column.isNullable)
            return std::format("std::optional<{}>", type);
        return std::string { std::forward<T>(type) };
    };

    auto const shouldForceUnicodeTextColumn = [=] {
        return forceUnicodeTextColumn
               || (unicodeTextColumnOverrides.contains(tableName)
                   && unicodeTextColumnOverrides.at(tableName).contains(column.name));
    };

    using namespace SqlColumnTypeDefinitions;
    return optional(std::visit(
        detail::overloaded {
            [](Bigint const&) -> std::string { return "int64_t"; },
            [](Binary const& type) -> std::string { return std::format("Light::SqlBinary", type.size); },
            [](Bool const&) -> std::string { return "bool"; },
            [&](Char const& type) -> std::string {
                if (type.size == 1)
                {
                    if (shouldForceUnicodeTextColumn())
                        return "wchar_t";
                    else
                        return "char";
                }
                else if (type.size <= sqlFixedStringMaxSize)
                {
                    // CHAR(n) seems to be always right-side whitespace trimmed,
                    // so we use SqlTrimmedFixedString for it.
                    if (shouldForceUnicodeTextColumn())
                        return std::format("Light::SqlTrimmedFixedString<{}, wchar_t>", type.size);
                    else
                        return std::format("Light::SqlTrimmedFixedString<{}>", type.size);
                }
                else if (type.size == detail::SqlMaxNumberOfChars<char>() && !shouldForceUnicodeTextColumn())
                {
                    return "Light::SqlMaxDynamicAnsiString";
                }
                else if (type.size == detail::SqlMaxNumberOfChars<wchar_t>() && shouldForceUnicodeTextColumn())
                {
                    return "Light::SqlMaxDynamicWideString";
                }
                else
                {
                    if (shouldForceUnicodeTextColumn())
                        return std::format("Light::SqlDynamicWideString<{}>", type.size);
                    else
                        return std::format("Light::SqlDynamicAnsiString<{}>", type.size);
                }
            },
            [](Date const&) -> std::string { return "Light::SqlDate"; },
            [](DateTime const&) -> std::string { return "Light::SqlDateTime"; },
            [](Decimal const& type) -> std::string {
                return std::format("Light::SqlNumeric<{}, {}>", type.precision, type.scale);
            },
            [](Guid const&) -> std::string { return "Light::SqlGuid"; },
            [](Integer const&) -> std::string { return "int32_t"; },
            [=](NChar const& type) -> std::string {
                // NCHAR(n) seems to be always right-side whitespace trimmed,
                // so we use SqlTrimmedFixedString for it.
                if (type.size == 1)
                    return "char16_t";
                else if (type.size <= sqlFixedStringMaxSize)
                    return std::format("Light::SqlTrimmedFixedString<{}, wchar_t>", type.size);
                else
                    return std::format("Light::SqlDynamicWideString<{}>", type.size);
            },
            [](NVarchar const& type) -> std::string { return std::format("Light::SqlDynamicUtf16String<{}>", type.size); },
            [](Real const& v) -> std::string {
                if (v.precision <= 24)
                    return "float";
                else
                    return "double";
            },
            [](Smallint const&) -> std::string { return "int16_t"; },
            [=](Text const& type) -> std::string {
                if (shouldForceUnicodeTextColumn())
                {

                    if (type.size == detail::SqlMaxNumberOfChars<wchar_t>())
                        return "Light::SqlMaxDynamicWideString";
                    return std::format("Light::SqlDynamicWideString<{}>", type.size);
                }
                else
                {
                    if (type.size == detail::SqlMaxNumberOfChars<char>())
                        return "Light::SqlMaxDynamicAnsiString";
                    return std::format("Light::SqlDynamicAnsiString<{}>", type.size);
                }
            },
            [](Time const&) -> std::string { return "Light::SqlTime"; },
            [](Timestamp const&) -> std::string { return "Light::SqlDateTime"; },
            [](Tinyint const&) -> std::string { return "uint8_t"; },
            [](VarBinary const& type) -> std::string { return std::format("Light::SqlDynamicBinary<{}>", type.size); },
            [&](Varchar const& type) -> std::string {
                if (type.size > 0 && type.size <= sqlFixedStringMaxSize)
                {
                    if (shouldForceUnicodeTextColumn())
                        return std::format("Light::SqlWideString<{}>", type.size);
                    else
                        return std::format("Light::SqlAnsiString<{}>", type.size);
                }
                else
                {
                    if (shouldForceUnicodeTextColumn())
                    {

                        if (type.size == detail::SqlMaxNumberOfChars<wchar_t>())
                            return "Light::SqlMaxDynamicWideString";
                        return std::format("Light::SqlDynamicWideString<{}>", type.size);
                    }
                    else
                    {
                        if (type.size == detail::SqlMaxNumberOfChars<char>())
                            return "Light::SqlMaxDynamicAnsiString";
                        return std::format("Light::SqlDynamicAnsiString<{}>", type.size);
                    }
                }
            },
        },
        column.type));
}

std::optional<std::string> CxxModelPrinter::MapColumnNameOverride(SqlSchema::FullyQualifiedTableName const& tableName,
                                                                  std::string const& columnName) const
{
    using namespace SqlSchema;
    auto const it = _config.columnNameOverrides.find(ColumnIdentifier {
        .table = tableName,
        .column = columnName,
    });
    if (it != _config.columnNameOverrides.end())
        return it->second;
    return std::nullopt;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void CxxModelPrinter::ResolveOrderAndPrintTable(std::vector<SqlSchema::Table> const& tables)
{
    // Inverse and through relations are a property of the whole schema, not of one table, so plan
    // them once here and hand each table its own share below.
    auto const relationPlan = PlanRelations(tables);

    std::unordered_map<size_t, std::optional<int>> numberOfForeignKeys;
    for (auto const idx: std::views::iota(static_cast<size_t>(0), tables.size()))
        numberOfForeignKeys[idx] = static_cast<int>(tables[idx].foreignKeys.size());

    auto const updateForeignKeyCountAfterPrinted = [&](auto const& tablePrinted) {
        for (auto const idx: std::views::iota(static_cast<size_t>(0), tables.size()))
        {
            auto const& table = tables[idx];
            if (table.name == tablePrinted.name)
                numberOfForeignKeys[idx] = std::nullopt;

            for (auto const& foreignKey: table.foreignKeys)
            {
                if ((foreignKey.primaryKey.table.table == tablePrinted.name) && numberOfForeignKeys[idx].has_value())
                    numberOfForeignKeys[idx] = numberOfForeignKeys[idx].value() - 1;
            }
        }
    };

    size_t numberOfPrintedTables = 0;

    auto const printTable = [&, this](size_t index, auto const& table) {
        static auto const noRelations = std::vector<PlannedRelation> {};
        auto const planned = relationPlan.find(table.name);
        PrintTable(table, planned != relationPlan.end() ? planned->second : noRelations);
        numberOfPrintedTables++;
        updateForeignKeyCountAfterPrinted(table);
        numberOfForeignKeys[index] = std::nullopt;
    };

    while (numberOfPrintedTables < tables.size())
    {
        for (auto const idx: std::views::iota(static_cast<size_t>(0), tables.size()))
        {
            auto const& table = tables[idx];
            if (!numberOfForeignKeys[idx]) // NOLINT(bugprone-unchecked-optional-access)
                continue;
            if (numberOfForeignKeys[idx].value() == 0) //  NOLINT(bugprone-unchecked-optional-access)
            {
                printTable(idx, table);
            }
            else
            {
                // check all other tables and see if we have some with 0 foreign keys
                // if we do NOT have them, we need to print this table anyway since
                // there is some circular dependency that we cannot resolve
                bool found = false;
                for (auto const otherIdx: std::views::iota(static_cast<size_t>(0), tables.size()))
                {
                    if (numberOfForeignKeys[otherIdx] == 0)
                        found = true;
                }
                // we need to print this table so that we do not print it again
                if (!found)
                    printTable(idx, table);
            }
        }
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void CxxModelPrinter::PrintTable(SqlSchema::Table const& table, std::vector<PlannedRelation> const& relationPlan)
{
    auto& definition = _definitions[table.name];
    std::string cxxPrimaryKeys;
    for (auto const& key: table.primaryKeys)
    {
        if (!cxxPrimaryKeys.empty())
            cxxPrimaryKeys += ", ";
        cxxPrimaryKeys += '"' + key + '"';
    }

    // corresponds to the column name in the sql table
    auto aliasName = [&](std::string_view name) {
        if (_config.makeAliases)
            return std::format(", Light::SqlRealName {{ \"{}\" }}", name);
        return std::string {};
    };

    auto aliasNameOrNullopt = [&](std::string_view name) {
        if (_config.makeAliases)
            return std::format(", Light::SqlRealName {{ \"{}\" }}", name);
        return std::string { ", std::nullopt" };
    };

    auto const primaryKeyPart = [this]() {
        if (_config.primaryKeyAssignment == PrimaryKey::ServerSideAutoIncrement)
            return ", Light::PrimaryKey::ServerSideAutoIncrement"sv;
        else if (_config.primaryKeyAssignment == PrimaryKey::AutoAssign)
            return ", Light::PrimaryKey::AutoAssign"sv;
        else
            return ""sv;
    };

    auto aliasTableName = [&](std::string_view name) {
        if (_config.makeAliases)
        {
            return FormatTableName(name);
        }
        return std::string { name };
    };

    auto aliasRealTableName = [&](std::string_view name) {
        if (_config.makeAliases)
        {
            return std::format("    static constexpr std::string_view TableName = \"{}\";\n\n", name);
        }
        return std::string {};
    };

    auto const selfReferencing = [&](auto const& column) {
        if (column.isForeignKey)
        {
            auto const& foreignKey = GetForeignKey(column, table.foreignKeys);
            return foreignKey.primaryKey.table == foreignKey.foreignKey.table;
        }
        return false;
    };

    // A self-referencing foreign key can be modelled as a `BelongsTo` only when the primary key it
    // points at is declared *before* it: the member pointer names a member of the very struct being
    // defined, which the compiler only accepts for members already seen. When the column order does
    // not allow it, the column falls back to a plain field (see the fallback at the end of the loop).
    auto const columnPosition = [&](std::string_view columnName) {
        return std::ranges::find(table.columns, columnName, &SqlSchema::Column::name) - table.columns.begin();
    };
    auto const selfReferenceIsDeclarable = [&](auto const& column) {
        auto const& foreignKey = GetForeignKey(column, table.foreignKeys);
        if (foreignKey.primaryKey.columns.size() != 1)
            return false;
        // `BelongsTo` static_asserts that the member it points at is a primary key, so a self-reference
        // into a merely UNIQUE column cannot be modelled as one.
        auto const referenced =
            std::ranges::find(table.columns, foreignKey.primaryKey.columns.at(0), &SqlSchema::Column::name);
        if (referenced == table.columns.end() || !referenced->isPrimaryKey)
            return false;
        return referenced - table.columns.begin() < columnPosition(column.name);
    };

    definition.structName = aliasTableName(table.name);
    definition.text << std::format("struct {} final\n", definition.structName);
    definition.text << std::format("{{\n");
    definition.text << aliasRealTableName(table.name);

    UniqueNameBuilder uniqueMemberNameBuilder;

    auto const tableName = SqlSchema::FullyQualifiedTableName { .catalog = {}, .schema = {}, .table = table.name };

    for (auto const& column: table.columns)
    {
        std::string type = MakeType(column,
                                    table.name,
                                    _config.forceUnicodeTextColumns,
                                    _config.unicodeTextColumnOverrides,
                                    _config.sqlFixedStringMaxSize);
        auto const memberName =
            MapColumnNameOverride(tableName, column.name) // NOLINT(bugprone-unchecked-optional-access)
                .or_else([&] { return std::optional { SanitizeName(FormatName(column.name, _config.formatType)) }; })
                .value();

        ++_numberOfColumnsListed;

        if (column.isForeignKey && !column.isPrimaryKey && (!selfReferencing(column) || selfReferenceIsDeclarable(column)))
        {
            auto const& foreignKey = GetForeignKey(column, table.foreignKeys);
            if (foreignKey.primaryKey.columns.size() == 1)
            {
                auto foreignTableName =
                    aliasTableName(foreignKey.primaryKey.table.table); // NOLINT(bugprone-unchecked-optional-access)
                auto const relationName =
                    MapColumnNameOverride(tableName, column.name) // NOLINT(bugprone-unchecked-optional-access)
                        .or_else([&] {
                            return std::optional { SanitizeName(
                                FormatName(StripSuffix(foreignKey.foreignKey.columns.at(0)), _config.formatType)) };
                        })
                        .value();
                auto const emittedName = uniqueMemberNameBuilder.DeclareName(relationName);
                // For a self-reference the referenced primary key member was already emitted into this very
                // struct, and sanitization or de-duplication may have changed its identifier - reuse the name
                // it actually got instead of re-deriving it from the column name.
                auto const referencedMemberName = [&] {
                    auto const& referencedColumn = foreignKey.primaryKey.columns.at(0);
                    if (selfReferencing(column))
                        if (auto const member = std::ranges::find(
                                definition.members, referencedColumn, &std::pair<std::string, std::string>::second);
                            member != definition.members.end())
                            return member->first;
                    return FormatName(referencedColumn, _config.formatType);
                }();
                definition.text << std::format(
                    "    Light::BelongsTo<&{}{}{}> {};\n",
                    std::format("{}::{}", foreignTableName, referencedMemberName),
                    aliasNameOrNullopt(foreignKey.foreignKey.columns.at(0)),
                    [&] {
                        if (column.isNullable)
                            return ", Light::SqlNullable::Null"sv;
                        else
                            return ""sv;
                    }(),
                    emittedName);
                definition.members.emplace_back(emittedName, column.name);
                // A self-reference needs no include: the referenced struct is the one being defined.
                if (!selfReferencing(column))
                    definition.requiredTables.emplace(std::move(foreignTableName));
                ++_numberOfForeignKeysListed;
                continue;
            }
            _warningOnUnsupportedMultiKeyForeignKey[foreignKey.foreignKey.table] = foreignKey;
        }

        if (column.isPrimaryKey)
        {
            auto const emittedName = uniqueMemberNameBuilder.DeclareName(memberName);
            definition.members.emplace_back(emittedName, column.name);
            definition.text << MakeDecimalPrecisionNote(column);
            definition.text << std::format(
                "    Light::Field<{}{}{}> {};", type, primaryKeyPart(), aliasName(column.name), emittedName);
            if (column.isForeignKey)
                definition.text << " // NB: This is also a foreign key";
            definition.text << "\n";
            continue;
        }

        // Fallback: Handle the column as a regular field.
        auto const emittedName = uniqueMemberNameBuilder.DeclareName(memberName);
        definition.members.emplace_back(emittedName, column.name);
        definition.text << MakeDecimalPrecisionNote(column);
        definition.text << std::format("    Light::Field<{}{}> {};", type, aliasName(column.name), emittedName);
        if (column.isForeignKey)
            definition.text << std::format(" // NB: This is also a foreign key");
        definition.text << '\n';
    }

    for (SqlSchema::ForeignKeyConstraint const& foreignKey: table.externalForeignKeys)
    {
        // TODO: How to figure out if this is a HasOne or HasMany relation.
        (void) foreignKey; // TODO
    }

    // Inverse and through relations. These come after the columns because they are not columns: they
    // carry no storage of their own, and RecordColumnMember-based projections skip them.
    if (!relationPlan.empty())
        definition.text << '\n';

    for (auto const& relation: relationPlan)
    {
        auto const referencedStruct = AliasTableName(relation.referencedTable);
        auto const throughStruct = relation.throughTable.empty() ? std::string {} : AliasTableName(relation.throughTable);

        // The relation names the other record but must not include its header: the child already
        // includes this one, so including it back would be a cycle. A forward declaration is enough
        // because every relation stores its records indirectly.
        if (relation.referencedTable != table.name)
            definition.forwardDeclaredTables.emplace(referencedStruct);
        if (!throughStruct.empty() && relation.throughTable != table.name)
            definition.forwardDeclaredTables.emplace(throughStruct);

        // Reserved against member-name collisions before computing this relation's own member name
        // below: a member named identically to a forward-declared type it does not itself recurse into
        // would shadow that type for the rest of the class body - legal C++, but GCC's -Wchanges-meaning
        // (an error under -Werror) rejects it, and it is genuinely confusing besides. Self-references
        // are exempt: a member named after its own enclosing struct is unambiguous (the injected-class-
        // name already means the same thing), and that shape is already relied on elsewhere.
        if (relation.referencedTable != table.name)
            std::ignore = uniqueMemberNameBuilder.TryDeclareName(referencedStruct);
        if (!throughStruct.empty() && relation.throughTable != table.name)
            std::ignore = uniqueMemberNameBuilder.TryDeclareName(throughStruct);

        auto const memberName = uniqueMemberNameBuilder.DeclareName(
            SanitizeName(FormatName(StripSuffix(relation.memberName), _config.formatType)));

        auto const ownerSelector = relation.ownerSelectorRequired
                                       ? std::format(", Light::SqlRealName {{ \"{}\" }}", relation.ownerForeignKeyColumn)
                                       : std::string {};

        switch (relation.kind)
        {
            case PlannedRelation::Kind::HasOne:
                // No HasOne type exists in the library, only HasOneThrough. Emit the collection form
                // and say why, rather than silently pretending the relation is scalar.
                definition.text << std::format("    // NOTE: {}.{} is uniquely indexed, so this relation holds at most\n"
                                               "    //       one record. Lightweight has no HasOne type, so it is "
                                               "emitted as a collection.\n",
                                               relation.referencedTable,
                                               relation.ownerForeignKeyColumn);
                [[fallthrough]];
            case PlannedRelation::Kind::HasMany:
                definition.text << std::format(
                    "    Light::HasMany<{}{}> {};\n", referencedStruct, ownerSelector, memberName);
                break;

            case PlannedRelation::Kind::HasOneThrough:
            case PlannedRelation::Kind::HasManyThrough: {
                // Same selector formatting either way; only the template name differs by cardinality.
                auto const templateName =
                    relation.kind == PlannedRelation::Kind::HasOneThrough ? "HasOneThrough"sv : "HasManyThrough"sv;
                definition.text << std::format(
                    "    Light::{}<{}, Light::Through<{}>{}{}> {};\n",
                    templateName,
                    referencedStruct,
                    throughStruct,
                    ownerSelector,
                    relation.referencedSelectorRequired
                        ? std::format(", Light::SqlRealName {{ \"{}\" }}", relation.referencedForeignKeyColumn)
                        : std::string {},
                    memberName);
                break;
            }
        }

        ++_numberOfRelationsListed;
    }

    definition.text << "};\n\n";
}

void CxxModelPrinter::PrintReport()
{
    std::println();
    std::println("Summary");
    std::println("=======");
    std::println();
    std::println("Tables created          : {}", _definitions.size());
    std::println("Columns listed          : {}", _numberOfColumnsListed);
    std::println("Foreign keys considered : {}", _numberOfForeignKeysListed);
    std::println("Foreign keys ignored    : {}", _warningOnUnsupportedMultiKeyForeignKey.size());
    std::println("Inverse relations       : {}", _numberOfRelationsListed);

    if (!_warningOnUnsupportedMultiKeyForeignKey.empty() && !_config.suppressWarnings)
    {
        std::println();
        std::println("Warning: The database has {} following foreign keys having multiple columns, which is not supported.",
                     _warningOnUnsupportedMultiKeyForeignKey.size());
        for (auto const& fk: _warningOnUnsupportedMultiKeyForeignKey)
            std::println("  {} -> {}", fk.second.foreignKey, fk.second.primaryKey);
    }
}

std::string CxxModelPrinter::ToString(std::string_view modelNamespace)
{
    std::string result;
    for (auto const& [tableName, definition]: _definitions)
    {
        result += std::format("// file: {}.hpp\n", tableName);
        result += HeaderFileForTheTable(modelNamespace, tableName);
    }
    return result;
}

std::string CxxModelPrinter::TableIncludes() const
{

    std::string result;
    for (auto const& [tableName, definition]: _definitions)
    {
        result += std::format("#include \"{}.hpp\"\n", tableName);
    }
    return result;
}

std::string CxxModelPrinter::Example(SqlSchema::Table const& table) const
{
    std::stringstream exampleEntries;

    auto const tableName = AliasTableName(table.name);

    exampleEntries << std::format("auto entries{} = dm.Query<{}>().First(10);\n", tableName, tableName);
    exampleEntries << std::format("for (auto const& entry: entries{})\n", tableName);
    exampleEntries << "{\n";
    exampleEntries << std::format("    std::println(\"{{}}\", Lightweight::DataMapper::Inspect(entry));\n");
    exampleEntries << "}\n";
    exampleEntries << "\n";

    return exampleEntries.str();
}

} // namespace Lightweight::Tools
