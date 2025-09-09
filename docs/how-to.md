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
