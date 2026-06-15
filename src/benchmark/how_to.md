# Compilation-time benchmark

This benchmark measures **compile time** (not runtime) for the pathological case
that matters in practice: an entity (`Table99`) with a deep graph of
foreign-key relationships, which forces the `DataMapper` to recursively
instantiate templates (`ConfigureRelationAutoLoading`, `LoadBelongsTo`,
`QuerySingle`, `CallOnPrimaryKey`, …) for every reachable table.

The whole point is **reproducibility**: to know whether a change made
instantiation cheaper, you must compile the *same* entity graph before and
after and compare. The schema generator is therefore stdlib-only and seeded.

## 1. Generate the schema (deterministic, no SQLAlchemy)

```sh
python ./src/benchmark/CreateDatabase.py            # 100 tables, seed 42
# scale it: python ./src/benchmark/CreateDatabase.py --num-tables 50 --seed 7
```

## 2. Build a SQLite database from the schema

```sh
sqlite3 test.sqlite < schema.sql
```

## 3. Generate the C++ entity headers

```sh
ddl2cpp --connection-string 'DRIVER=SQLite3;Database=test.sqlite' \
        --output src/benchmark/entities --make-aliases --naming-convention camelCase \
        --generate-instantiations
```

(`ddl2cpp` is built by any normal preset, e.g. `out/build/clang-debug/src/tools/ddl2cpp`.)

`ddl2cpp` always emits a `Lightweight::Description<>` specialization per record so the
DataMapper reads pre-baked metadata instead of evaluating reflection. With
`--generate-instantiations` it additionally emits, per record, an `extern template`
declaration in the header plus a `.cpp` that instantiates the heavy relation machinery once,
and a `CMakeLists.txt` defining a `LightweightEntities` library target. Consuming translation
units then link that library instead of re-instantiating the machinery — so the closure is
compiled once (in the library) rather than in every translation unit that queries the records.
The benchmark's `CMakeLists.txt` picks this up automatically (`add_subdirectory(entities)`).

## 4. Configure with the benchmark enabled

```sh
cmake --preset clang-debug -DLIGHTWEIGHT_BUILD_BENCHMARK=ON
```

You can now build `LightweightBenchmark` normally, but for **precise numbers**
use the measurement harness instead of a plain build.

## 5. Measure compile time precisely

```sh
python src/benchmark/measure_compile_time.py --runs 3
```

This recompiles `benchmark.cpp` in isolation (using the exact flags from
`compile_commands.json`), reports the median wall-clock time, and — via Clang's
`-ftime-trace` — breaks the time down into frontend/backend and the hottest
individual template instantiations. Example output:

```
== Wall-clock compile time ==
  median :   61909.4 ms
== Compiler activity (from -ftime-trace) ==
  Frontend                         61273.8 ms
  Backend                              7.8 ms
== Top template instantiations (by total time) ==
  56263.3 ms       1   ...  InstantiateFunction: DataMapper::ConfigureRelationAutoLoading<Table99>
  12740.2 ms       1   ...  InstantiateFunction: DataMapper::LoadBelongsTo<BelongsTo<&Table52::id, ...>>
  ...
```

Re-run it after a change and compare the median and the per-template lines to
see exactly which recursive instantiation got cheaper.

Useful flags:
- `--build-dir DIR`   point at a different (Clang) build dir.
- `--runs N`          number of timed runs; the median is reported.
- `--top N`           how many hottest template instantiations to list.
- `--json-out FILE`   dump the aggregated result as JSON for diffing.

> `-ftime-trace` is Clang-specific. The harness requires a Clang build dir; GCC
> builds are rejected with a clear message.

To enable the trace inside a normal CMake build instead, configure with
`-DLIGHTWEIGHT_BENCHMARK_TIME_TRACE=ON`; each build then writes
`benchmark.cpp.json` next to the object file.
