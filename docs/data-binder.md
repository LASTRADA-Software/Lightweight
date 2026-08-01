# Data Binder API

Lightweight SQL client is non-intrusively extensible with respect to custom column data types.

A lot of standard types are already supported, but if you need to add a custom type, you can do it by implementing a simple interface.

## Custom Column Data Type Binder Example

Suppose you have a custom type `CustomType` that you want to bind to the SQL statement.

```cpp
struct CustomType
{
    int value;

    constexpr auto operator<=>(CustomType const&) const noexcept = default;
};
```

To bind the custom type to the SQL statement, you need to implement the `SqlDataBinder<>` specialization
for the custom type.

```cpp
template <>
struct SqlDataBinder<CustomType>
{
    /// Define the column type for the custom type, e.g. SqlColumnTypeDefinitions::Guid {}.
    static constexpr auto ColumnType = SqlDataBinder<decltype(CustomType::value)>::ColumnType;

    /// Binds the custom type to the SQL statement as an input parameter.
    static SQLRETURN InputParameter(SQLHSTMT hStmt,
                                    SQLUSMALLINT column,
                                    CustomType const& value,
                                    SqlDataBinderCallback& cb) noexcept
    {
        return SqlDataBinder<int>::InputParameter(hStmt, column, value.value, cb);
    }

    /// Binds the custom type to the SQL statement as an output parameter.
    ///
    /// This function is used to bind the custom type to the SQL statement as an output parameter,
    /// which means that the SQL statement will return the custom type's value into the `result` parameter.
    ///
    /// @param hStmt        The SQL statement handle.
    /// @param column       The column number.
    /// @param result       The custom type to bind to the SQL statement.
    /// @param indicator    The indicator, which is used to determine whether the value is NULL.
    /// @param callback     The callback that is called after the binding process is completed.
    ///
    /// @see SqlDataBinderCallback
    static SQLRETURN OutputColumn(SQLHSTMT hStmt,
                                  SQLUSMALLINT column,
                                  CustomType* result,
                                  SQLLEN* indicator,
                                  SqlDataBinderCallback& callback) noexcept
    {
        return SqlDataBinder<int>::OutputColumn(hStmt, column, &result->value, indicator, callback);
    }

    /// Retrieves the custom type's value from the SQL statement's currnt result row at the given column.
    ///
    /// This function is used to retrieve the custom type's value from the SQL statement's current result row at the given column.
    ///
    /// @param hStmt        The SQL statement handle.
    /// @param column       The column number.
    /// @param result       The custom type to bind to the SQL statement.
    /// @param indicator    The indicator, which is used to determine whether the value is NULL.
    /// @param callback     The callback that can be used to access some runtime information.
    ///
    /// @see SqlDataBinderCallback
    static SQLRETURN GetColumn(SQLHSTMT hStmt,
                               SQLUSMALLINT column,
                               CustomType* result,
                               SQLLEN* indicator,
                               SqlDataBinderCallback const& cb) noexcept
    {
        return SqlDataBinder<int>::GetColumn(hStmt, column, &result->value, indicator, cb);
    }

    /// Provides a human-readable representation of the custom type.
    ///
    /// This is used purely for debugging purposes.
    static std::string Inspect(CustomType const& value) noexcept
    {
        return std::format("CustomType({})", value.value);
    }
};
```

## InputParameter()

The `InputParameter()` function binds the custom type to the SQL statement as an input parameter.

It is usually sufficient to call one of the already existing `SqlDataBinder<T>::InputParameter(...)` 
functions for the underlying type.

Sometimes you may need to perform additional operations, such as converting the custom type to the
underlying type, before calling the `SqlDataBinder<T>::InputParameter(...)` function. Then you can
make use of the passed `callback` to also make sure any additional operations are performed
at the end of the binding process.

## OutputColumn()

The `OutputColumn()` function binds the custom type to the SQL statement as an output parameter.

It is usually sufficient to call one of the already existing `SqlDataBinder<T>::OutputColumn(...)`

## GetColumn()

The `GetColumn()` function retrieves the custom type from the SQL statement.

Calling `GetColumn()` is usually less efficient than calling `OutputColumn()` because it requires
an additional copy operation.

## Inspect()

The `Inspect()` function is used to provide a human-readable representation of the custom type.

This function should be used purely for debugging purposes.

## `SqlNumeric<Precision, Scale>` precision limits

`Precision` is a count of **decimal digits**. It may be at most
`Lightweight::SqlMaxNumericPrecision`, which is **19 on every supported toolchain**:

| Toolchain | `SqlMaxNumericPrecision` | Widest column |
|-----------|--------------------------|---------------|
| GCC / Clang (native `__int128`)         | 19 | `DECIMAL(19, s)` — including MS SQL Server's `money` |
| MSVC, clang-cl (software `Int128Soft`)  | 19 | `DECIMAL(19, s)` — including MS SQL Server's `money` |

The bound is deliberately toolchain-independent. A column's precision comes from the
database schema, so a `ddl2cpp`-generated record has to compile wherever it is consumed —
a header that builds under GCC and fails under MSVC would make the generator useless.
`Lightweight::Int128`, the type that carries the unscaled value, is `__int128_t` where the
compiler has one and a software stand-in (`detail::Int128Soft`) where it does not.

`Scale` counts the digits after the decimal point and may be anywhere in
`[0, Precision]`. `Scale == Precision` is the purely fractional case: `SqlNumeric<4, 4>`
is SQL's `DECIMAL(4, 4)` and covers `[0.0000, 0.9999]`.

Note that `SQL_MAX_NUMERIC_LEN` (16) is a *byte* count, not a digit count — do not use
it as a precision bound. It is the size of `SQL_NUMERIC_STRUCT::val`, a 128-bit
mantissa, which is 38 decimal digits. **38 is not the bound either**: it is what the
ODBC struct could hold, not what this implementation can read back. What narrows it is
the **readable width** — every accessor except `ToUnscaledValue()` and `ToString()`
divides through `long double`, so no more digits can be read back through those than that
type's significand holds. The widest `long double` in use is the 80-bit x87 one, whose
64-bit significand gives 19 digits; past that nothing is readable on any platform — a
fetched `SqlNumeric<20, 0>` holding `99999999999999999999` would come back as
`100000000000000000000`.

A column wider than the bound must be read as a string.

### What each accessor delivers

Even inside the bound the accessors do not all carry the same number of digits. For a
value obtained by fetching (i.e. one whose mantissa arrived intact):

| Accessor | Significant digits | Notes |
|----------|--------------------|-------|
| `ToUnscaledValue()` | up to `Precision` | Returns `Lightweight::Int128`. The only accessor that yields the raw integer. |
| `ToString()` | up to `Precision` | Rendered from the unscaled integer, not by formatting a `long double`, so every digit survives on every toolchain. |
| `ToLongDouble()` | 15 guaranteed; up to 19 in practice | Bounded by `long double`: at most 19 (x87 80-bit); **15 where `long double` is a `double`** (MSVC, Clang on Apple Silicon). |
| `ToDouble()`, `operator<=>` | 15 | `std::numeric_limits<double>::digits10`. |
| `ToFloat()`, `operator==` | **7** | `std::numeric_limits<float>::digits10`. `operator==` compares via `ToFloat()`, so `SqlNumeric<19, 4>` values `1234567890.1234` and `1234567890.9999` compare **equal**. Compare `ToUnscaledValue()` if you need exactness. |

So `ToUnscaledValue()` and `ToString()` are exact to the declared precision everywhere,
including MSVC; the floating-point accessors are not. If you need more than 15 digits out
of a `SqlNumeric`, use one of those two rather than `ToDouble()`.

Neither `Int128` implementation is formattable by `std::format` (the native `__int128_t`
is not either), so render an unscaled value with
`Lightweight::detail::Int128ToString(value.ToUnscaledValue())`, or just use `ToString()`.

### How many of those digits survive a round-trip

Declaring a wide `Precision` does not by itself guarantee that every digit is
*transferred*. What you can rely on:

- **Up to `std::numeric_limits<double>::digits10` (15) significant decimal digits:
  exact everywhere.** Every backend and driver combination round-trips such a value
  unchanged. Beyond that, note that narrowing to 15 significant digits can round *into*
  the integral part — `123456789012345.6789` becomes `123456789012346` — so a wide
  value is not merely "right up to the decimal point".
- **Beyond 15 significant digits: only on the native `SQL_C_NUMERIC` path.** Two
  independent things can narrow the value to a `double` (≈15 significant digits):
  - The binder deliberately falls back to `SQL_C_DOUBLE` for SQLite and MS SQL Server,
    whose drivers do not handle `SQL_NUMERIC_STRUCT` usably (see
    `NativeNumericSupportIsBroken`). This is why MS SQL Server's own `money` loses
    precision on MS SQL Server: its maximum `922337203685477.5807` reads back as
    `922337203685477.6250`.
  - Some driver *builds* narrow internally even on the native path. The psqlODBC that
    ships for Windows converts through a `double` before filling `SQL_NUMERIC_STRUCT`,
    so a 19-digit value comes back as the mantissa of its nearest `double`; the Linux
    build of the same driver transfers the mantissa verbatim.

  PostgreSQL on Linux is therefore the only combination that carries a full 19-digit
  value end to end today.

One further limit is independent of the driver: assigning from a `float`/`double` — the
only value-setting API besides fetching — is bounded by that floating-point type, so
`SqlNumeric<19, 4> { 922337203685477.5807 }` already holds `922337203685477.6250`
before any database is involved.

If you need exactness above 15 digits on an arbitrary backend, read the column as a
string instead.

## How `SqlVariant` decides which alternative to fill

`SqlDataBinder<SqlVariant>::GetColumn` queries the driver for
**`SQL_DESC_CONCISE_TYPE`** and dispatches into the matching variant alternative
based on the concise ODBC SQL type code.

The verbose `SQL_DESC_TYPE` collapses every datetime subtype into the single
value `SQL_DATETIME` (9) and every interval subtype into `SQL_INTERVAL` (10), so
it cannot distinguish DATE from TIME from TIMESTAMP. The concise type does:

| Column kind  | `SQL_DESC_CONCISE_TYPE` | Variant alternative |
|--------------|--------------------------|---------------------|
| DATE         | `SQL_TYPE_DATE` (91)     | `SqlDate`           |
| TIME         | `SQL_TYPE_TIME` (92)     | `SqlTime`           |
| TIMESTAMP    | `SQL_TYPE_TIMESTAMP` (93)| `SqlDateTime`       |

## Driver-specific connection-string requirements

When using `SqlVariant` (or any binder that dispatches on the driver-reported
column type) against PostgreSQL via psqlODBC, the following options must appear
in the connection string:

| Option         | Required value | What it controls                                                                                                              |
|----------------|----------------|-------------------------------------------------------------------------------------------------------------------------------|
| `BoolsAsChar`  | `0`            | Reports `BOOLEAN` columns as `SQL_BIT` so they dispatch into the `bool` alternative. The driver default reports them as `SQL_CHAR`. |
| `LFConversion` | `0`            | Disables LF↔CRLF translation. Required for byte-exact round-trips of `TEXT`/`VARCHAR` values containing `\n`.                  |

The canonical postgres entry with these options is in `scripts/tests/.test-env.yml`.
