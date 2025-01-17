# Best practices

## Introduction

This document provides a set of best practices for using the API.
These best practices are based on the experience of the API team and the feedback from the API users
as well as learnings from the underlying technologies.

## Common best practices

### Use the `DataMapper` API

The `DataMapper` API provides a high-level abstraction for working with database tables.
It simplifies the process of querying, inserting, updating, and deleting data from the database
while retaining performance and flexibility.

### Keep data model and business logic separate

Keep the data model and business logic separate to improve the maintainability and scalability of your application.

Remember to also keep frontend (e.g. GUI) and backend (e.g. API) separate.

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
