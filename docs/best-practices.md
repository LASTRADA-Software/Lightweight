# Best practices

## Introduction

This document provides a set of best practices for using the API.
These best practices are based on the experience of the API team and the feedback from the API users
as well as learnings from the underlying technologies.

## Common best practices

### Use the DataMapper API

The `DataMapper` API provides a high-level abstraction for working with database tables.
It simplifies the process of querying, inserting, updating, and deleting data from the database
while retaining performance and flexibility.

### Keep data model and business logic separate

Keep the data model and business logic separate to improve the maintainability and scalability of your application.

Remember to also keep frontend (e.g. GUI) and backend (e.g. API) separate.

<<<<<<< Updated upstream
### Use transactions with care

Use transactions to group multiple database operations into a single unit of work.
This ensures that all operations are either committed or rolled back together.

However, be careful when using transactions, as they can affect the performance serverically if not used properly.

### Binding output parameters

When not using the `DataMapper` API, you need to also retrieve the result manually.
Either via `SqlStatement::BindOutputColumns()` or in post, by fetching the columns individually.
It is always highly recommended to pre-bind in order to avoid unnecessary memory allocations and copying.

With this, it is sufficient to call `SqlStatement::BindOutputColumns()` once one and then you can reuse the result
throughout many `SqlStatement::FetchRow()` calls.

The pitfall here is, that if using `std::optional<T>` column types, you **MUST** rebind the result columns before
each fetch operation whereas if not having any nullable values, you do not have to.

||||||| Stash base
=======
### Use transactions with care

Use transactions to group multiple database operations into a single unit of work.
This ensures that all operations are either committed or rolled back together.

However, be careful when using transactions, as they can affect the performance serverically if not used properly.

### Binding output parameters

First things first, always favor using the `DataMapper` API over manual querying and binding of columns.

However, when not using the `DataMapper` API, you need to also retrieve the result manually.
Either via `SqlStatement::BindOutputColumns()` or in post, by fetching the columns individually.
It is always highly recommended to pre-bind in order to avoid unnecessary memory allocations and copying.

With this, it is sufficient to call `SqlStatement::BindOutputColumns()` once one and then you can reuse the result
throughout many `SqlStatement::FetchRow()` calls.

The pitfall here is, that if using `std::optional<T>` column types, you **MUST** rebind the result columns before
each fetch operation, whereas, if not having any nullable values, you do not have to.

```cpp
struct MixedNullRow
{
    Field<SqlGuid, PrimaryKey::AutoAssign> id {};
    Field<std::optional<SqlAnsiString<30>>> name;
    Field<std::optional<int>> age;
};

void ForEachData(SqlStatement& stmt, auto&& onRow)
{
    stmt.ExecuteDirect(stmt.Query(RecordTableName<MixedNullRow>)
                           .Select()
                           .Fields({ "id"sv, "name"sv, "age"sv })
                           .All());

    auto currentRow = MixedNullRow {};

    // bind output columns for the first time
    stmt.BindOutputColumns(&currentRow.id, &currentRow.name, &currentRow.age);

    while (stmt.FetchRow())
    {
        onRow(currentRow);

        // Bind output columns for the next fetch.
        // (ONLY necessary when using std::optional<T> in the columns)
        stmt.BindOutputColumns(&currentRow.id, &currentRow.name, &currentRow.age);
    }
}
```

>>>>>>> Stashed changes
## SQL driver related best practices

### Query result row columns in order

When querying the result set, always access the columns in the order they are returned by the query.
At least MS SQL server driver has issues when accessing columns out of order.
Carefully check the driver documentation for the specific behavior of the driver you are using.

This can be avoided when using the `DataMapper` API, which maps the result always in order and as efficient as possible.

## Performance is key

### Use native column types

Use the native column types provided by the API for the columns in your tables.
This will help to improve the performance of your application by reducing the overhead of data conversion.

The existence of `SqlVariant` in the API allows you to store any type of data in a single column,
but it is recommended to use the native column types whenever possible.

### Use prepared statements

Prepared statements are precompiled SQL statements that can be executed multiple times with different parameters.
Using prepared statements can improve the performance of your application by reducing the overhead
of parsing, analyzing, and compiling the SQL queries.

### Use pagination

When querying large result sets, use pagination to limit the number of results returned in a single response.
This will help to reduce the response time and the load on the server.

## SQL server variation challenges

### 64-bit integer handling in Oracle database

Oracle database does not support 64-bit integers natively.
When working with 64-bit integers in Oracle database, you need to use the `SqlNumeric` column types.
