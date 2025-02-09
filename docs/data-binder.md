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
