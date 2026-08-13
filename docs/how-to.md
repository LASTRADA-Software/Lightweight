# How to

The following is a list of some topics that you might find usefull

## Rename column name

By default DataMapper will use the name of data member as a column name, to change the name that should be used in sql queries 
you have to provide it as a template argument to the `Field` template. For example if you have a data member named `foo`, that corresponds to the column named `bar` you need to declare it as following:
```cpp
struct MyTable
{
    Light::Field<int, SqlRealName { "bar" }> foo {};
};
```

To directly get the name of the datamember that will be used for the sql queries you can use `FieldNameOf`, in the case presented above
```cpp
FieldNameOf<&MyTable::foo>; // is equivalent to std::string_view("bar");
```

## `UPDATE ... RETURNING` / fetching the result of a data-modifying statement

SQLite and PostgreSQL both support appending `RETURNING <columns>` to `INSERT`/`UPDATE`/`DELETE` to
read back affected rows without a second round trip. `SqlStatement` executes such a statement like
any other (`stmt.Prepare("UPDATE t SET n = n + 1 WHERE id = ? RETURNING n")`, then `Execute(...)`),
and `SqlResultCursor::NumRowsAffected()`/`NumColumnsAffected()` reliably report the true row/column
counts on every driver we've tested — but whether the returned row is actually **fetchable** via
`FetchRow()`/`TryFetchRow()` is driver-dependent. Some ODBC driver builds execute a `RETURNING`
statement through a path that never opens a cursor over the result set, so `FetchRow()` throws
`24000 Invalid cursor state` even though the statement executed correctly and the row count is
accurate ([#545](https://github.com/LASTRADA-Software/Lightweight/issues/545); observed with
certain sqliteodbc builds).

If you need this to work portably across driver builds, use the two-statement form instead — a
conditional `UPDATE ... WHERE ...` (no `RETURNING`) dispatched on `NumRowsAffected()`, followed by
an ordinary read-back by primary key:

```cpp
auto cursor = stmt.Execute(/* guard condition params */);
if (cursor.NumRowsAffected() == 0)
{
    // guard condition didn't match — nothing to do
}
else
{
    // read the row back explicitly instead of relying on RETURNING's fetchability
    auto readBack = SqlStatement { stmt.Connection() };
    readBack.Prepare("SELECT n FROM t WHERE id = ?");
    auto readCursor = readBack.Execute(id);
    readCursor.FetchRow();
}
```

This costs one extra round trip but is fully equivalent in atomicity: the guard clause lives inside
the `UPDATE`'s own `WHERE`, evaluated and applied as one indivisible statement under the database's
write lock.
