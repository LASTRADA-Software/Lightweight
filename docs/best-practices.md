# Best Practices

## Introduction

This document provides a set of best practices for using the API.  
These best practices are based on the experience of the API team and feedback from API users, as well as learnings from
the underlying technologies.

## Common Best Practices

### Use the DataMapper API

The `DataMapper` API provides a high-level abstraction for working with database tables.  
It simplifies the process of querying, inserting, updating, and deleting data from the database while retaining
performance and flexibility.

### Keep Data Model and Business Logic Separate

Keep the data model and business logic separate to improve the maintainability and scalability of your application.

Remember to also keep frontend (e.g., GUI) and backend (e.g., API) separate.

### Use Transactions with Care

Use transactions to group multiple database operations into a single unit of work.  
This ensures that all operations are either committed or rolled back together.

However, be careful when using transactions, as they can affect performance severely if not used properly.

### Binding Output Parameters

First things first, always favor using the `DataMapper` API over manual querying and binding of columns.

However, when not using the `DataMapper` API, you need to retrieve the result manually, either via
`SqlStatement::BindOutputColumns()` or afterwards by fetching the columns individually.  
It is always highly recommended to pre-bind to avoid unnecessary memory allocations and copying.

With this, it is sufficient to call `SqlStatement::BindOutputColumns()` once, and then you can reuse the result
throughout many `SqlStatement::FetchRow()` calls.

The pitfall here is that if you are using `std::optional<T>` column types, you **MUST** rebind the result columns before
each fetch operation. If there are no nullable values, you do not have to.

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

    // Bind output columns for the first time
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

## SQL Driver-Related Best Practices

### Query Result Row Columns in Order

When querying the result set, always access the columns in the order they are returned by the query.  
At least the MS SQL Server driver has issues when accessing columns out of order.
Carefully check the driver documentation for the specific behavior of the driver you are using.

This can be avoided when using the `DataMapper` API, which always maps the result in order and as efficiently as
possible.

## Performance Is Key

### Use Native Column Types

Use the native column types provided by the API for the columns in your tables.  
This will help to improve the performance of your application by reducing the overhead of data conversion.

The existence of `SqlVariant` in the API allows you to store any type of data in a single column, but it is recommended
to use the native column types whenever possible.

### Use Prepared Statements

Prepared statements are precompiled SQL statements that can be executed multiple times with different parameters.  
Using prepared statements can improve the performance of your application by reducing the overhead of parsing,
analyzing, and compiling SQL queries.

### Use Pagination or Infinite Scrolling

When querying large result sets, use pagination or infinite scrolling to limit the number of results returned in a
single response.  
This will help to reduce the response time and the load on the server, and improve the performance of your
application.

## SQL Server Variation Challenges

### 64-bit Integer Handling in Oracle Database

Oracle database does not support 64-bit integers natively.  
When working with 64-bit integers in Oracle database, you need to use the `SqlNumeric` column types.