// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Api.hpp"
#include "SqlError.hpp"
#include "SqlRetryClassifier.hpp"
#include "SqlServerType.hpp"

#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <type_traits>
#include <utility>

namespace Lightweight
{

class SqlConnection;

/// @ingroup Retry
/// Backoff configuration of a @ref SqlRetryPolicy.
///
/// This is the descriptor half of the policy: pure data, no behaviour, so it can be read from a
/// config file, held in a settings struct, or written inline at a call site.
struct SqlRetrySettings
{
    /// Maximum number of *retries* — an operation is attempted at most @c maxRetries+1 times.
    /// Zero disables retrying without disabling the classification machinery.
    unsigned maxRetries = 3;

    /// Delay before the first retry.
    std::chrono::milliseconds initialDelay { 500 };

    /// Multiplier applied to the delay after each failed attempt (exponential backoff).
    double backoffMultiplier = 2.0;

    /// Upper bound on any single delay, so a long budget cannot produce an unbounded wait.
    std::chrono::milliseconds maxDelay { 30'000 };

    /// Deadline expressed as a budget: once the delays already spent plus the next planned delay
    /// would exceed this, the policy gives up with @ref SqlRetryGiveUpReason::DelayBudgetExhausted
    /// instead of waiting. Zero — the default — means "no deadline, bounded only by
    /// @ref maxRetries".
    ///
    /// A budget is used rather than a wall-clock deadline so that the decision stays pure: it
    /// depends only on the caller-supplied @ref SqlRetryState, never on the current time.
    std::chrono::milliseconds totalDelayBudget { 0 };
};

/// @ingroup Retry
/// How far a retry loop has already got. Threaded through @ref SqlRetryPolicy::Decide so the
/// decision itself stays a pure function of its inputs.
struct SqlRetryState
{
    /// Number of retries already consumed. Zero on the first failure.
    unsigned retriesSoFar = 0;

    /// Sum of the delays already waited out, checked against
    /// @ref SqlRetrySettings::totalDelayBudget.
    std::chrono::milliseconds delaySoFar { 0 };
};

/// @ingroup Retry
/// What a retry loop should do after an attempt failed.
enum class SqlRetryAction : std::uint8_t
{
    /// Wait for @ref SqlRetryDecision::delay and try again.
    Retry,

    /// Surface the failure to the caller.
    GiveUp,
};

/// @ingroup Retry
/// Why @ref SqlRetryPolicy::Decide declined to retry. Reported so callers and logs can tell an
/// exhausted budget apart from an error that was never retryable to begin with.
enum class SqlRetryGiveUpReason : std::uint8_t
{
    /// Not giving up — set when the action is @ref SqlRetryAction::Retry.
    None,

    /// The error is permanent; another attempt would fail identically.
    NotTransient,

    /// The error was transient, but @ref SqlRetrySettings::maxRetries is spent.
    RetriesExhausted,

    /// The error was transient and retries remained, but the next delay would overrun
    /// @ref SqlRetrySettings::totalDelayBudget.
    DelayBudgetExhausted,
};

/// @ingroup Retry
/// The outcome of @ref SqlRetryPolicy::Decide.
struct SqlRetryDecision
{
    /// Whether to retry or to surface the failure.
    SqlRetryAction action {};

    /// How long to wait before the next attempt. Zero unless @ref action is
    /// @ref SqlRetryAction::Retry.
    std::chrono::milliseconds delay {};

    /// Why the policy gave up. @ref SqlRetryGiveUpReason::None when it did not.
    SqlRetryGiveUpReason reason { SqlRetryGiveUpReason::None };

    /// @return @c true when the caller should retry.
    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return action == SqlRetryAction::Retry;
    }
};

/// @ingroup Retry
/// Description of one retry about to happen, handed to a @ref SqlRetryPolicy::RetryObserver.
struct SqlRetryAttempt
{
    /// Which retry this is, counting from one.
    unsigned retryNumber {};

    /// The configured budget, so an observer can render "retry 2/3" without holding the settings.
    unsigned maxRetries {};

    /// How long the driver is about to sleep before re-running the operation.
    std::chrono::milliseconds delay {};

    /// The error that triggered the retry.
    SqlErrorInfo error;
};

/// @ingroup Retry
/// @brief Supplies the wait between retries.
///
/// Injected rather than called directly so a test can drive a full retry loop in microseconds and
/// assert on the delays that *would* have been waited out, instead of actually sleeping through an
/// exponential backoff.
class LIGHTWEIGHT_API SqlRetrySleeper
{
  public:
    SqlRetrySleeper() = default;
    /// Polymorphic destructor.
    virtual ~SqlRetrySleeper() = default;

    SqlRetrySleeper(SqlRetrySleeper const&) = delete;
    SqlRetrySleeper& operator=(SqlRetrySleeper const&) = delete;
    SqlRetrySleeper(SqlRetrySleeper&&) = delete;
    SqlRetrySleeper& operator=(SqlRetrySleeper&&) = delete;

    /// Blocks the calling thread for the given duration.
    ///
    /// @param duration How long to wait.
    virtual void Sleep(std::chrono::milliseconds duration) = 0;
};

/// @ingroup Retry
/// @brief Returns the production sleeper, which forwards to @c std::this_thread::sleep_for.
[[nodiscard]] LIGHTWEIGHT_API SqlRetrySleeper& ThreadSleeper() noexcept;

/// @ingroup Retry
/// @brief A reusable retry/backoff policy for transient database failures.
///
/// Combines a @ref SqlRetryClassifier (which errors are worth another attempt — a per-DBMS
/// question) with @ref SqlRetrySettings (how many attempts, how long to wait between them). Every
/// collaborator is injectable, and none of them is constructed internally: the classifier and the
/// sleeper are borrowed references with sensible process-wide defaults.
///
/// The type separates the *decision* from the *driving*:
///
/// - @ref Decide is pure. Given an error and how far the loop has already got, it says retry or
///   give up, and why. No clock, no sleep, no I/O — so every branch is reachable from a unit test.
/// - @ref Execute / @ref TryExecute are the thin drivers that call @ref Decide in a loop, wait via
///   the injected @ref SqlRetrySleeper, and re-run the callable.
///
/// @code
/// auto const policy = SqlRetryPolicy::For(connection);
/// auto const orderCount = policy.Execute([&] {
///     return SqlStatement { connection }
///         .ExecuteDirectScalar<int>("SELECT COUNT(*) FROM orders")
///         .value_or(0);
/// });
/// @endcode
///
/// @note The callable must be safe to run more than once. Retrying an operation that already had a
///       visible side effect is the caller's responsibility; wrap it in a transaction that the
///       callable itself begins and commits, so a retry starts from a clean slate.
class [[nodiscard]] SqlRetryPolicy
{
  public:
    /// Notified just before each retry. Used, for instance, to route retry notices into a
    /// progress reporter or a log.
    using RetryObserver = std::function<void(SqlRetryAttempt const&)>;

    /// Constructs a policy with the default settings, the dialect-agnostic classifier and the
    /// real sleeper.
    SqlRetryPolicy() = default;

    /// Constructs a policy.
    ///
    /// @param settings The backoff configuration.
    /// @param classifier Which errors are retryable; @c nullptr selects @ref GenericRetryOps().
    ///                   The referenced classifier must outlive the policy — the dialect
    ///                   singletons always do.
    /// @param sleeper How to wait between attempts; @c nullptr selects @ref ThreadSleeper().
    ///                The referenced sleeper must outlive the policy.
    /// @param observer Called before each retry; may be empty.
    LIGHTWEIGHT_API explicit SqlRetryPolicy(SqlRetrySettings settings,
                                            SqlRetryClassifier const* classifier = nullptr,
                                            SqlRetrySleeper* sleeper = nullptr,
                                            RetryObserver observer = {});

    /// Builds a policy whose classifier matches the given server type.
    ///
    /// The mapping runs through @c SqlQueryFormatter::Get(), so the per-DBMS knowledge stays at
    /// the formatter dispatch point. A server type without a formatter of its own falls back to
    /// @ref GenericRetryOps().
    ///
    /// @param serverType The DBMS whose error dialect should be used.
    /// @param settings The backoff configuration.
    /// @return The configured policy.
    [[nodiscard]] LIGHTWEIGHT_API static SqlRetryPolicy For(SqlServerType serverType, SqlRetrySettings settings = {});

    /// Builds a policy whose classifier matches the connection's DBMS.
    ///
    /// @param connection The connection whose server type selects the classifier.
    /// @param settings The backoff configuration.
    /// @return The configured policy.
    [[nodiscard]] LIGHTWEIGHT_API static SqlRetryPolicy For(SqlConnection const& connection, SqlRetrySettings settings = {});

    /// @return The backoff configuration in effect.
    [[nodiscard]] SqlRetrySettings const& Settings() const noexcept
    {
        return _settings;
    }

    /// @return The classifier in effect.
    [[nodiscard]] SqlRetryClassifier const& Classifier() const noexcept
    {
        return *_classifier;
    }

    /// Installs an observer notified before each retry.
    ///
    /// @param observer The observer; pass an empty function to remove a previously set one.
    void SetRetryObserver(RetryObserver observer)
    {
        _observer = std::move(observer);
    }

    /// Computes the backoff delay preceding a given retry.
    ///
    /// @param retryIndex Zero-based retry number: @c 0 is the delay before the first retry.
    /// @return @c initialDelay multiplied by @c backoffMultiplier @p retryIndex times, clamped to
    ///         @c maxDelay.
    [[nodiscard]] LIGHTWEIGHT_API std::chrono::milliseconds DelayFor(unsigned retryIndex) const noexcept;

    /// Decides what to do after a failed attempt.
    ///
    /// Pure: no I/O, no clock, no hidden state.
    ///
    /// @param error The error reported by the failed attempt.
    /// @param state How far the retry loop has already got.
    /// @return Whether to retry, how long to wait first, and — if not — why not.
    [[nodiscard]] LIGHTWEIGHT_API SqlRetryDecision Decide(SqlErrorInfo const& error,
                                                          SqlRetryState const& state) const noexcept;

    /// Runs @p callable, retrying while the policy says the failure is worth another attempt.
    ///
    /// Only @c SqlException is treated as a retry candidate; any other exception propagates
    /// immediately. When the policy gives up, the last @c SqlException is rethrown unchanged, so
    /// the caller sees the original diagnostics rather than a wrapper.
    ///
    /// @tparam Callable A nullary callable, taken by value because it is invoked repeatedly.
    /// @param callable The operation to run.
    /// @return Whatever @p callable returns.
    template <typename Callable>
    auto Execute(Callable callable) const -> std::invoke_result_t<Callable&>;

    /// Like @ref Execute, but reports a final failure as @c std::unexpected rather than throwing.
    ///
    /// @tparam Callable A nullary callable, taken by value because it is invoked repeatedly.
    /// @param callable The operation to run.
    /// @return The callable's result, or the @ref SqlErrorInfo of the attempt the policy gave up on.
    template <typename Callable>
    [[nodiscard]] auto TryExecute(Callable callable) const -> std::expected<std::invoke_result_t<Callable&>, SqlErrorInfo>;

  private:
    LIGHTWEIGHT_API void NotifyRetry(SqlRetryAttempt const& attempt) const;

    SqlRetrySettings _settings {};
    SqlRetryClassifier const* _classifier = &GenericRetryOps();
    SqlRetrySleeper* _sleeper = &ThreadSleeper();
    RetryObserver _observer {};
};

template <typename Callable>
auto SqlRetryPolicy::Execute(Callable callable) const -> std::invoke_result_t<Callable&>
{
    auto state = SqlRetryState {};

    while (true)
    {
        try
        {
            return callable();
        }
        catch (SqlException const& e)
        {
            auto const decision = Decide(e.info(), state);
            if (decision.action == SqlRetryAction::GiveUp)
                throw;

            state.retriesSoFar += 1;
            state.delaySoFar += decision.delay;

            NotifyRetry(SqlRetryAttempt { .retryNumber = state.retriesSoFar,
                                          .maxRetries = _settings.maxRetries,
                                          .delay = decision.delay,
                                          .error = e.info() });

            _sleeper->Sleep(decision.delay);
        }
    }
}

template <typename Callable>
auto SqlRetryPolicy::TryExecute(Callable callable) const -> std::expected<std::invoke_result_t<Callable&>, SqlErrorInfo>
{
    using Result = std::invoke_result_t<Callable&>;

    try
    {
        if constexpr (std::is_void_v<Result>)
        {
            Execute(std::move(callable));
            return {};
        }
        else
            return Execute(std::move(callable));
    }
    catch (SqlException const& e)
    {
        return std::unexpected { e.info() };
    }
}

} // namespace Lightweight
