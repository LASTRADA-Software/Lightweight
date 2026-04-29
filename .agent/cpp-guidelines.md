# C++ guidelines (self-contained)

This file is the canonical, project-internal C++ ruleset. Contributors and agents do **not** need any external file (no `~/.claude/rules/cpp.md`, no global notes) — every rule that applies to Lightweight is here.

## 1. General C++23 baseline

### Design
- **Data-driven design** — avoid hard-coded magic values. Prefer descriptor tables, configuration, or polymorphic dispatch over scattered conditionals.
- **Dependency injection** — pass collaborators through constructors / parameters so tests can substitute fakes. Anything touching I/O, time, randomness, or the database must be injectable.

### Documentation
- **Doxygen** on every new public function, parameter, return, class, struct, and member:
  ```cpp
  /// Short description.
  /// @param name Description.
  /// @return Description.
  ```
- Be factual — no marketing prose.

### Type & const correctness
- **`const`-correctness** throughout: refs, pointers, member functions, parameters that don't mutate.
- **`auto` type deduction** for readability; **structured bindings** for tuple-like returns.
- Mark return values **`[[nodiscard]]`** where ignoring the result would be a bug — this includes builders, query objects, and anything returning `std::expected`.

### Modern C++ surface
- Prefer **C++23**: `constexpr`, `std::ranges`, `std::format`, `std::expected`, `std::span`.
- **`std::expected<T, E>`** with monadic chaining (`and_then`, `or_else`, `transform`, `transform_error`) for fallible operations. Avoid nested `if`s when a chain reads better.
- **`std::span`** for arrays / contiguous sequences in API surfaces.
- **Range views** for generation/transformation: `std::views::iota`, `std::views::filter`, `std::views::transform`, etc.

### Forbidden constructs
- **C-style loops are forbidden.** Use range-based `for` and the views above.
- **No raw owning pointers.** `std::unique_ptr` / `std::shared_ptr` for ownership, RAII for resources.
- **No `NOLINT` suppressions** — fix the underlying issue clang-tidy reports. The `linux-clang-debug` preset enables `clang-tidy` automatically.
- **No new third-party dependencies** without strong justification — vcpkg manifest at `vcpkg.json` lists what we already accept.

### Tooling
- Run **`clang-format`** on every changed file (project `.clang-format` is authoritative).
- Build with the matching preset (`linux-clang-debug` / `windows-clangcl-debug`); resolve every warning — `PEDANTIC_COMPILER_WERROR=ON`.
- All changes must be covered by unit tests; aim to **increase** code coverage with every PR.

## 2. Lightweight-specific patterns

### Namespace & symbol visibility
- All public symbols live in the `Lightweight` namespace; the alias `Light` is provided for brevity in user code.
- Public headers must be **self-contained** — they must compile with no PCH and only their own includes.
- Keep ABI-affecting changes deliberate; the library is consumed as a vcpkg port.

### Error handling on the public API
Prefer `std::expected<T, SqlError>` for fallible public APIs. Reserve exceptions for programmer errors (precondition violation, contract misuse). Internally, ODBC `SQLRETURN` codes are wrapped via the helpers in `src/Lightweight/SqlError.{hpp,cpp}` and `SqlErrorDetection.hpp`.

```cpp
return statement.Prepare(sql)
    .and_then([&] { return statement.Execute(args...); })
    .transform([&](auto&&) { return …; })
    .transform_error([](SqlError const& e) { return wrapWithContext(e); });
```

### ODBC `SQLHANDLE` lifetime
ODBC handles (env, dbc, stmt) are owned by `SqlConnection` / `SqlStatement` (and the small RAII wrappers near them). They are released in the destructor. Never:
- allocate a raw `SQLHANDLE` at call sites,
- store one beyond the lifetime of its wrapper,
- pass `SQLHANDLE` across abstraction boundaries (pass the wrapper instead).

### Per-DBMS dispatch
Per-DBMS branching belongs only inside `SqlQueryFormatter` and its subclasses. Business logic must call a virtual method on the formatter and let the override decide. **Do not** write `if (server == SqlServerType::POSTGRESQL)` outside `QueryFormatter/`.

If a new feature behaves differently on each DBMS:
1. Add a `[[nodiscard]] virtual` method to `SqlQueryFormatter` with a sensible default.
2. Override per DBMS in `PostgreSqlFormatter`, `SQLiteFormatter`, `SqlServerFormatter`.
3. Cover all three in tests.

### `SqlDataBinder<T>` contract for Unicode strings
Unicode-bearing string types must round-trip identically across MSSQL, PostgreSQL, and SQLite. This is non-trivial because each driver has different opinions about UTF-16 vs UTF-8.

The repo's hard-won rules (codified in commits `894c67c7`, `89885982`, `f311cb9f`):

- Bind Unicode payloads via **`SQL_C_WCHAR`** (UTF-16) uniformly across the `BasicStringBinder` surface — `BasicStringBinder.hpp` does this.
- For non-`u16string` Unicode types, transcode through a temporary `std::u16string` using `UnicodeConverter.hpp`, bind that, and copy back in the post-process callback (`SqlDataBinderCallback::PlanPostProcessOutputColumn`).
- Use `SQL_C_WCHAR` even where the driver "would also accept" `SQL_C_CHAR` — the latter is unreliable for non-ASCII on `psqlODBC`.
- For PostgreSQL connection strings, **disable LF↔CRLF translation** (`LFConversion=0`); without this, embedded newlines in character data silently mutate.

### `[[nodiscard]]` policy on builders
Query builders (`SqlQuery::Select`, `Insert`, `Update`, `Delete`, `Migrate`) and `DataMapper::Query<…>` return objects that *must* be terminated (`.All()`, `.First()`, `.Execute()`, etc.). Mark every step `[[nodiscard]]` so accidental discard is a build error.

### Catch2 + DI in tests
Tests must obtain a connection through the test fixture wired to `--test-env=<name>`, never by constructing one with hard-coded strings. New connection strings go in `.test-env.yml`. See `.agent/testing.md`.

### Reflection
The library uses C++20 member pointers by default and supports a C++26 reflection mode (`LIGHTWEIGHT_CXX26_REFLECTION`). New code that enumerates record fields should use the abstraction (e.g., the `Member(x)` macro pattern in `src/tests/Utils.hpp`) rather than committing to one mode.

### Modules
`Lightweight.cppm` is the C++20 module aggregator (`LIGHTWEIGHT_BUILD_MODULES=ON`, requires CMake ≥ 3.28). New public headers must be addable to the module export list without breaking the build.

## 3. Quick checklist before pushing

- `clang-format` clean
- `clang-tidy` clean (no `NOLINT`s added)
- `linux-clang-debug` builds with no warnings
- Tests run green against `sqlite3`, `mssql2022`, `postgres`
- New public APIs have doxygen + `[[nodiscard]]` where relevant
- No new `if (server == …)` outside `QueryFormatter/`
- New SQL primitives go through `SqlQueryFormatter` virtuals
- Unicode-bearing types round-trip via `SQL_C_WCHAR` / `BasicStringBinder.hpp`
- `/simplify` was run; out-of-scope findings were either addressed or surfaced to the user
