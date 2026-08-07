# Design: a dependency-injection seam at the ODBC error boundary

Status: **proposal**

## Goal

Make error *classification* and error *propagation* unit-testable without a database, without adding
runtime cost to the success path, and without changing the public `SqlDataBinder<T>` contract.

## The problem

Today, everything interesting about error handling is unreachable from a unit test.

`Utils.cpp` holds the single classification chokepoint that ~81 call sites funnel through:

```cpp
void RequireSuccess(SQLHSTMT hStmt, SQLRETURN error, std::source_location sourceLocation)
{
    if (SQL_SUCCEEDED(error))
        return;

    auto const errorInfo = SqlErrorInfo::FromStatementHandle(hStmt);   // <-- impure: reads a driver handle
    if (errorInfo.sqlState == "07009")
        throw std::invalid_argument(...);                              // <-- pure policy, worth testing
    else
        throw SqlException(errorInfo);                                 // <-- pure policy, worth testing
}
```

The *policy* — demote `07009` (Invalid Descriptor Index) to `std::invalid_argument` so callers can
treat a missing optional column as a soft failure, throw `SqlException` for everything else — is
ordinary branching logic. It is also currently untestable, because reaching it requires a real ODBC
driver that returns a specific SQLSTATE on demand.

That is the whole problem in one function: **pure decision logic welded to an impure handle read.**

## Non-goals

This proposal deliberately does *not*:

- Abstract the ODBC API generally (no `IOdbcApi` with a virtual per `SQLBindParameter`). That would
  put indirect calls on per-cell hot paths that are hand-tuned with `LIGHTWEIGHT_FORCE_INLINE`, and
  it would still fail to let a fake produce a coherent result set, because the binders in
  `DataBinder/` call `SQLGetData`/`SQLBindParameter` directly rather than through `SqlStatement`.
- Change `SqlDataBinder<T>`'s signature. `docs/data-binder.md` documents
  `InputParameter(SQLHSTMT, SQLUSMALLINT, ...) -> SQLRETURN` as a public extension point; every
  third-party binder implements it. Changing it is an ABI and source break that belongs in a major
  version, not in a testability change.
- Enable testing of handle *lifecycle* paths — e.g. `SQLFetchScroll` failing mid-block and
  `RowArrayCursor` unwinding correctly. Those stay integration-only. See "Known limits".

## Design

Two independent, additive changes.

### 1. Split the pure policy out of `RequireSuccess`

Introduce a free function that makes the decision without touching a handle:

```cpp
/// What the caller should do about a failed ODBC return code.
enum class SqlFailureAction : uint8_t
{
    /// The call succeeded; nothing to do.
    None,
    /// Throw std::invalid_argument - a soft, caller-recoverable failure.
    ThrowInvalidArgument,
    /// Throw SqlException - a genuine SQL error.
    ThrowSqlException,
};

/// Decides how a failed ODBC return code should surface, given the diagnostics already read
/// from the driver.
///
/// Pure: performs no I/O and touches no handle, so it is directly unit-testable by constructing
/// a SqlErrorInfo - the same approach SqlErrorDetectionTests.cpp already uses.
///
/// @param result    The ODBC return code.
/// @param errorInfo The diagnostics corresponding to @p result.
/// @return The action the caller should take.
///
/// @note Not marked constexpr: SqlErrorInfo holds std::string members, so a call can never be a
/// constant expression. Marking it constexpr would compile but would be misleading.
[[nodiscard]] SqlFailureAction ClassifyOdbcResult(SQLRETURN result, SqlErrorInfo const& errorInfo) noexcept;
```

`RequireSuccess` becomes a thin, still-inlineable wrapper:

```cpp
void RequireSuccess(SQLHSTMT hStmt, SQLRETURN error, std::source_location sourceLocation)
{
    if (SQL_SUCCEEDED(error))
        return;                                    // success path unchanged - no added work

    auto const errorInfo = SqlErrorInfo::FromStatementHandle(hStmt);
    switch (ClassifyOdbcResult(error, errorInfo))
    {
        case SqlFailureAction::ThrowInvalidArgument:
            throw std::invalid_argument(std::format(
                "SQL error: {} in {}:{}", errorInfo, sourceLocation.file_name(), sourceLocation.line()));
        case SqlFailureAction::ThrowSqlException:
            throw SqlException(errorInfo);
        case SqlFailureAction::None:
            break;
    }
}
```

This alone makes the policy testable with zero infrastructure:

```cpp
CHECK(ClassifyOdbcResult(SQL_ERROR, MakeError("07009")) == SqlFailureAction::ThrowInvalidArgument);
CHECK(ClassifyOdbcResult(SQL_ERROR, MakeError("23000")) == SqlFailureAction::ThrowSqlException);
```

### 2. Make the diagnostic source injectable

`SqlErrorInfo::FromHandle(SQLSMALLINT, SQLHANDLE)` is already `private` and is the *only* place the
library reads diagnostics from a handle. Add a test-only override:

```cpp
/// Supplies SqlErrorInfo for a handle. Injected in tests to drive error paths without a driver.
class SqlDiagnosticSource
{
  public:
    virtual ~SqlDiagnosticSource() = default;

    /// @param handleType One of SQL_HANDLE_ENV / SQL_HANDLE_DBC / SQL_HANDLE_STMT.
    /// @param handle     The handle to read diagnostics from.
    /// @return The diagnostics for @p handle.
    [[nodiscard]] virtual SqlErrorInfo Diagnose(SQLSMALLINT handleType, SQLHANDLE handle) = 0;
};

/// Overrides the diagnostic source process-wide. Passing nullptr restores the real ODBC reader.
/// Ownership stays with the caller.
LIGHTWEIGHT_API void SetDiagnosticSource(SqlDiagnosticSource* source);
```

`FromHandle` consults it only when a failure has already occurred:

```cpp
SqlErrorInfo SqlErrorInfo::FromHandle(SQLSMALLINT handleType, SQLHANDLE handle)
{
    if (auto* source = detail::CurrentDiagnosticSource())   // null in production
        return source->Diagnose(handleType, handle);

    // ... existing SQLGetDiagRecW loop, unchanged ...
}
```

This mirrors the mechanism the project already uses and accepts for `SqlLogger`
(`SqlLogger::SetLogger(SqlLogger&)` / `GetLogger()`), so it introduces no new concept — and
`SqlLogger::OnError(SqlErrorInfo const&, ...)` is already virtual, meaning a test can observe the
logging path the same way.

## Why this costs nothing in production

The success path is untouched:

```cpp
if (SQL_SUCCEEDED(error))
    return;
```

No pointer is added to `SqlStatement` or `SqlConnection`, no vtable, no per-object state. The
injection check sits *after* `SQL_SUCCEEDED` returns false — i.e. on a path that is already about to
read diagnostics from the driver and throw. One null-pointer test against the cost of an ODBC
diagnostic round-trip plus an exception is not measurable.

Critically, `SqlDataBinder<T>`'s `SQLHSTMT` signature is unchanged, so the inline
`SQLBindParameter`/`SQLGetData` calls in `DataBinder/BasicStringBinder.hpp` and friends compile
byte-identically.

## What becomes testable

- The `07009` demotion, and that every other SQLSTATE throws `SqlException` — directly, via
  `ClassifyOdbcResult`.
- That callers which are *supposed* to recover from a soft failure actually do (the optional-column
  paths that catch `std::invalid_argument`).
- Error *propagation*: with an injected source returning a scripted SQLSTATE, any call that reaches
  `RequireSuccess` on a failure can be driven to the specific exception, including the
  transient-error retry logic in `SqlBackup/Common.hpp` (`RetryOnTransientError`) against real
  `SqlException`s rather than hand-built lambdas.
- `SqlLogger::OnError` being called with the right `SqlErrorInfo` for a given failure.

## Known limits — stated up front

This seam covers error *classification and propagation*. It does **not** make these testable:

- Handle lifecycle under failure: `SQLFetchScroll` returning `SQL_ERROR` mid-block and
  `RowArrayCursor` unwinding, statement-handle state scrubbing (`ResetParameterArrayBinding`), or
  `SQL_CLOSE`/`SQL_UNBIND` semantics.
- Anything requiring a *coherent fake result set* — the binders read via `SQLGetData` directly, so a
  fake would have to impersonate the ODBC ABI.

Those need a fake at the driver level (a scriptable ODBC driver registered in `odbcinst.ini`), which
is the correct escalation *if* it ever proves necessary — it preserves zero production overhead,
unlike a virtual `IOdbcApi`. This design does not preclude it.

The honest pitch: a large fraction of the *valuable* error-path coverage for a few days of work,
not completeness.

## Blast radius

| File | Change |
|---|---|
| `src/Lightweight/Utils.hpp` / `.cpp` | Add `SqlFailureAction` + `ClassifyOdbcResult`; `RequireSuccess` delegates |
| `src/Lightweight/SqlError.hpp` / `.cpp` | Add `SqlDiagnosticSource` + `SetDiagnosticSource`; `FromHandle` consults it |
| `src/tests/SqlErrorDetectionTests.cpp` | Extend with classification tests (existing DB-free pattern) |
| new `src/tests/SqlErrorPropagationTests.cpp` | Scripted-SQLSTATE propagation tests |

No public signature changes. No ABI break. Four files touched, two of them tests.

## Open question for review

Should `SetDiagnosticSource` be compiled out entirely in release builds (guarded by `BUILD_TESTS`)?

- **For**: guarantees zero production cost and prevents misuse as a production hook.
- **Against**: the tests would then exercise a different code path than ships, which is exactly the
  class of divergence `AGENT.md` warns about in the GCC-vs-Clang section.

Recommendation: **keep it in all builds.** The cost is a null check on an already-failing path, and
keeping test and production code paths identical is worth more than eliminating it.
