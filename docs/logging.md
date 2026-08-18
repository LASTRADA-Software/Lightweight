# Logging and tracing

Lightweight routes every interesting database event — connections opening and closing, statements
being prepared, parameters being bound, rows being fetched, warnings and errors — through a single
`SqlLogger` interface. Installing a logger is how you see what the library is actually sending to
the database, and it is the first tool to reach for when a query misbehaves on one DBMS but not
another.

There is exactly one active logger process-wide. It is retrieved with `SqlLogger::GetLogger()` and
replaced with `SqlLogger::SetLogger()`.

## The built-in loggers

| Logger | Use it for |
|--------|-----------|
| `SqlLogger::NullLogger()` | Discards everything. The default — logging costs nothing unless you opt in. |
| `SqlLogger::StandardLogger()` | Warnings and errors only. A sensible production choice. |
| `SqlLogger::TraceLogger()` | Everything: prepare, bind, execute, fetch, connection lifecycle, scoped timers. Use while diagnosing. |

Switching to full tracing is a single call:

```cpp
#include <Lightweight/Lightweight.hpp>

Light::SqlLogger::SetLogger(Light::SqlLogger::TraceLogger());
```

The test binary exposes this as `--trace-sql` (see `src/tests/Utils.hpp`), which is the same switch
behind a command-line flag.

## Redirecting the output

Both built-in loggers write through a `MessageWriter` sink, which you can replace to route messages
into your own logging framework instead of the default output:

```cpp
auto& logger = Light::SqlLogger::TraceLogger();
logger.SetLoggingSink([](std::string message) {
    MyApp::Log::Debug("{}", message);
});
```

Call `SetLoggingSink({})` to restore the default sink.

## Writing a custom logger

For anything beyond redirecting text — counting queries, feeding metrics, failing a test when an
unexpected statement runs — derive from `SqlLogger::Null` and override just the hooks you care
about. `Null` supplies empty implementations for all of them, so you only write what you need:

```cpp
class QueryCounter final: public Light::SqlLogger::Null
{
  public:
    size_t executed = 0;
    size_t rowsFetched = 0;

    void OnExecute(std::string_view const& /*query*/) override { ++executed; }
    void OnFetchRow() override { ++rowsFetched; }
};

QueryCounter counter;
Light::SqlLogger::SetLogger(counter);
```

Deriving from `SqlLogger` directly is also possible, but almost every hook is pure virtual — you
would have to implement all of them.

The hooks available are:

| Hook | Fires when |
|------|-----------|
| `OnWarning` / `OnError` | A warning or an ODBC error is raised. Two `OnError` overloads: one for a bare `SqlError`, one for a full `SqlErrorInfo` with SQL state and native code. |
| `OnConnectionOpened` / `OnConnectionClosed` | A connection is established or torn down. |
| `OnConnectionIdle` / `OnConnectionReuse` | A pooled connection goes idle or is handed out again. |
| `OnExecuteDirect` | A statement is executed without preparation. |
| `OnPrepare` / `OnExecute` / `OnExecuteBatch` | A statement is prepared, executed, or executed as a batch. |
| `OnBind` | A parameter is bound. Only called when the logger was constructed with `SupportBindLogging::Yes`. |
| `OnFetchRow` / `OnFetchBlock` / `OnFetchEnd` | Rows are fetched one at a time, in a prefetch block, or the result set is exhausted. |
| `OnScopedTimerStart` / `OnScopedTimerStop` | A scoped timing region opens or closes. |

`OnFetchBlock` has a default no-op implementation, so an existing logger keeps compiling when
block-prefetch reporting is added.

## Restoring the previous logger

`SetLogger` does not take ownership, so the logger must outlive its installation. In tests, where
loggers are swapped repeatedly, an RAII guard keeps this honest:

```cpp
struct LoggerSwap
{
    Light::SqlLogger* previous;

    explicit LoggerSwap(Light::SqlLogger& replacement):
        previous { &Light::SqlLogger::GetLogger() }
    {
        Light::SqlLogger::SetLogger(replacement);
    }

    ~LoggerSwap() { Light::SqlLogger::SetLogger(*previous); }

    LoggerSwap(LoggerSwap const&) = delete;
    LoggerSwap& operator=(LoggerSwap const&) = delete;
};
```

## See also

- [best-practices.md](best-practices.md) — when tracing points at a real performance problem.
- [dbtool.md](dbtool.md) — the CLI, which reports migration and backup progress directly.
