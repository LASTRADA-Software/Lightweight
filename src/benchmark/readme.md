
This is a small benchmark of compilation times when we have a huge chain of tables that are connected via foreign keys 

Use the following procedure to create an example schema and create entity files for it

```
python ./src/benchmark/CreateDatabase.py
```

Then generate a database from the schema 

```
sqlite3 test.sqlite < schema.sql
```

```
ddl2cpp --connection-string 'DRIVER=SQLite3;Database=test.sqlite' --output src/benchmark/entities --make-aliases --naming-convention camelCase
```


Then configure cmake with `LIGHWEIGHT_BENCHMARK=ON` and compile target `LightweightBenchmark` 
