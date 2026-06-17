// SPDX-License-Identifier: Apache-2.0
//
// A tiny terminal TODO-list app whose purpose is to demonstrate the Lightweight C++23 coroutine API
// (Async::Task<T>) flowing through NVIDIA stdexec sender/receiver pipelines (std::execution / P2300).
//
// What it shows:
//   * Schema is created and kept current with the *migration* runner (LIGHTWEIGHT_SQL_MIGRATION +
//     MigrationManager), not a one-off CreateTable — re-running the app applies nothing new.
//   * Every database operation is expressed as an stdexec sender by adapting a Lightweight Task with
//     Async::AsSender(...), then composed with stdexec::then / when_all and driven by
//     stdexec::sync_wait. The DB coroutines resume on the worker pool (multi-threaded model), so the
//     blocking sync_wait on main() never has to pump anything.
//   * Adding a todo is idempotent: re-adding an existing title does not duplicate it, it just makes
//     sure that item is unchecked again.
//   * Default backend is a local SQLite3 file (todo.db); override with ODBC_CONNECTION_STRING.

#include <Lightweight/Async/Sender.hpp>
#include <Lightweight/Async/ThreadPoolExecutor.hpp>
#include <Lightweight/Lightweight.hpp>
#include <Lightweight/SqlMigration.hpp>

#include <cstdlib>
#include <format>
#include <optional>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <exec/static_thread_pool.hpp>
#include <stdexec/execution.hpp>

using namespace Lightweight;
using namespace Lightweight::SqlColumnTypeDefinitions;

namespace
{

/// Reads an environment variable, returning an empty string when unset (portable across MSVC/POSIX).
/// @param name The environment variable name.
/// @return The value, or an empty string if unset.
std::string GetEnvironmentVariable(std::string const& name)
{
#if defined(_MSC_VER)
    char* buffer = nullptr;
    size_t length = 0;
    if (_dupenv_s(&buffer, &length, name.c_str()) == 0 && buffer != nullptr)
    {
        std::string result { buffer };
        std::free(buffer);
        return result;
    }
    return {};
#else
    if (auto const* value = std::getenv(name.c_str()); value != nullptr)
        return std::string { value };
    return {};
#endif
}

/// Unwraps an stdexec::sync_wait result tuple. Because Async::AsSender advertises a stopped channel
/// (cancellation), sync_wait yields a nullable optional; this turns a disengaged result (which the
/// pipelines here never produce) into a clear error instead of an unchecked access.
/// @param result The optional tuple returned by stdexec::sync_wait.
/// @return The unwrapped result tuple.
template <typename Tuple>
Tuple UnwrapSyncWait(std::optional<Tuple> result)
{
    if (!result.has_value())
        throw std::runtime_error { "sync_wait completed via set_stopped (unexpected cancellation)" };
    return std::move(result).value();
}

} // namespace

/// A single TODO item. Column names match the migration below; the table is created by the migration
/// runner, while the DataMapper reads/writes rows through this record.
struct Todo final
{
    static constexpr std::string_view TableName = "todos";

    Light::Field<uint64_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<Light::SqlAnsiString<256>, Light::SqlRealName { "title" }> title;
    Light::Field<bool, Light::SqlRealName { "done" }> done { false };
};

// The schema migration. Declaring it auto-registers it with the MigrationManager at static-init time;
// CreateTableIfNotExists keeps re-runs a no-op once the table exists and the migration is recorded.
LIGHTWEIGHT_SQL_MIGRATION(20260616120000, "Create todos table")
{
    plan.CreateTableIfNotExists("todos")
        .PrimaryKeyWithAutoIncrement("id", Bigint())
        .RequiredColumn("title", Varchar(256))
        .RequiredColumn("done", Bool());
}

int main()
{
    // 1) Pick the backend: a local SQLite3 file by default, overridable via ODBC_CONNECTION_STRING.
    if (auto const connectionString = GetEnvironmentVariable("ODBC_CONNECTION_STRING"); !connectionString.empty())
        SqlConnection::SetDefaultConnectionString(SqlConnectionString { connectionString });
    else
        SqlConnection::SetDefaultConnectionString(SqlConnectionString { "DRIVER=SQLite3;Database=todo.db" });

    // 2) Ensure the schema is present and current via the migration runner. The MigrationManager opens
    //    its own DataMapper from the default connection string set above.
    auto& migrations = SqlMigration::MigrationManager::GetInstance();
    migrations.CreateMigrationHistory();
    auto const applied = migrations.ApplyPendingMigrations([](SqlMigration::MigrationBase const& migration,
                                                              size_t current,
                                                              size_t total) {
        std::println(
            "[{}/{}] applying migration {} — {}", current + 1, total, migration.GetTimestamp().value, migration.GetTitle());
    });
    std::println("Applied {} migration(s).", applied);

    // 3) Enable async on a DataMapper, resuming coroutines on the worker pool itself so that a blocking
    //    stdexec::sync_wait on this thread simply waits while the work runs off-thread.
    Async::ThreadPoolExecutor dbWorkers { 2 };
    DataMapper dm;
    dm.Connection().EnableAsync(dbWorkers, dbWorkers);

    auto const addTodo = [&](std::string_view title) {
        // Idempotent add: look the title up first (as a sender pipeline). If it already exists, just
        // make sure it is unchecked — re-adding an item "un-does" it rather than creating a duplicate;
        // otherwise insert a fresh row. The two outcomes use different async methods (UpdateAsync vs
        // CreateAsync, whose senders have different types), so we branch in host code after the lookup
        // rather than inside a single let_value.
        auto existing =
            std::get<0>(UnwrapSyncWait(stdexec::sync_wait(Async::AsSender(dm.QueryAsync<Todo>() // by-title lookup
                                                                              .Where(FieldNameOf<&Todo::title>, "=", title)
                                                                              .First()))));
        if (existing.has_value())
        {
            existing->done = false; // already present — just ensure it is unchecked
            UnwrapSyncWait(stdexec::sync_wait(Async::AsSender(dm.UpdateAsync(*existing))));
            std::println("kept #{} (unchecked): {}", existing->id.Value(), title);
            return;
        }
        auto todo = Todo {};
        todo.title = Light::SqlAnsiString<256> { title };
        // AsSender's value channel carries the new primary key, returned as a single-element tuple.
        auto const [newId] = UnwrapSyncWait(stdexec::sync_wait(Async::AsSender(dm.CreateAsync(todo))));
        std::println("added #{}: {}", newId, title);
    };

    auto const listTodos = [&] {
        // A read expressed as a sender pipeline: fetch all rows, then render them inside `then`.
        UnwrapSyncWait(stdexec::sync_wait(
            Async::AsSender(dm.QueryAsync<Todo>().All()) | stdexec::then([](std::vector<Todo> const& todos) {
                std::println("--- {} todo(s) ---", todos.size());
                for (auto const& todo: todos)
                    std::println("  [{}] #{} {}", todo.done.Value() ? 'x' : ' ', todo.id.Value(), todo.title.Value().str());
            })));
    };

    auto const markDone = [&](uint64_t todoId) {
        // Load -> mutate -> update, chaining the two DB steps with let_value so the second sender starts
        // only after the first completes, all on the worker pool. let_value keeps its argument (the
        // loaded optional) alive for the whole duration of the returned child sender, so we mutate that
        // argument *in place* and hand a reference to it to UpdateAsync — record-level async methods
        // capture the record by reference and it must outlive the co_await (see docs/async.md).
        UnwrapSyncWait(stdexec::sync_wait(Async::AsSender(dm.QuerySingleAsync<Todo>(todoId))
                                          | stdexec::let_value([&](std::optional<Todo>& loaded) {
                                                if (!loaded.has_value())
                                                    throw std::runtime_error { std::format("todo #{} not found", todoId) };
                                                loaded->done = true;
                                                return Async::AsSender(dm.UpdateAsync(*loaded));
                                            })));
        std::println("marked #{} done", todoId);
    };

    // 4) A small scripted demo (keeps the example headless so it runs in CI like test_chinook).
    addTodo("buy milk");
    addTodo("write docs");
    addTodo("ship release");

    // Fan-out two independent reads concurrently on a separate stdexec pool to show senders composing.
    auto pool = exec::static_thread_pool(2);
    auto sched = pool.get_scheduler();
    auto const [a, b] = UnwrapSyncWait(
        stdexec::sync_wait(stdexec::when_all(stdexec::starts_on(sched, Async::AsSender(dm.QueryAsync<Todo>().Count())),
                                             stdexec::starts_on(sched, Async::AsSender(dm.QueryAsync<Todo>().Count())))));
    std::println("concurrent counts agree: {} == {}", a, b);

    markDone(2);
    listTodos();

    // Re-adding an existing title does not duplicate it — it just makes sure the item is unchecked
    // again. "write docs" was checked above, so this un-checks it; the list afterwards shows no
    // duplicate row and an empty checkbox.
    addTodo("write docs");
    listTodos();

    return 0;
}
