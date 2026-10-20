// SPDX-License-Identifier: Apache-2.0
//
// A GUI-thread heartbeat for the dbtool-gui tests: a 10 ms timer whose longest gap between two ticks
// is how long the event loop was stalled.

#pragma once

#include <cstdint>

#include <QtCore/QElapsedTimer>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QTimer>

class QQuickWindow;

namespace DbtoolGui::Tests
{

/// Measures the longest GUI-thread stall while armed, as the longest gap between two ticks of a
/// 10 ms precise timer. Lives on and must be used from the GUI thread.
class UiWatchdog: public QObject
{
    Q_OBJECT
    /// Longest gap between two heartbeats since the watchdog was armed, in milliseconds.
    Q_PROPERTY(int maxGapMs READ maxGapMs NOTIFY maxGapMsChanged)
    /// The longest gap that still counts as responsive, in milliseconds (default 1000).
    Q_PROPERTY(int budgetMs READ budgetMs WRITE setBudgetMs NOTIFY budgetMsChanged)
    /// Whether a gap longer than `budgetMs` was measured.
    Q_PROPERTY(bool exceeded READ exceeded NOTIFY maxGapMsChanged)
    /// Whether a test only reports the measured gap or fails when it exceeds the budget.
    Q_PROPERTY(Mode mode READ mode WRITE setMode NOTIFY modeChanged)
    /// What the test is doing, e.g. "click connectButton"; the report names it with the longest gap.
    Q_PROPERTY(QString step READ step WRITE setStep NOTIFY stepChanged)
    /// The step during which the longest gap happened: "A" when it began and ended in step A, "A → B"
    /// when it began in A and ended in B.
    Q_PROPERTY(QString maxGapStep READ maxGapStep NOTIFY maxGapMsChanged)

  public:
    /// What a test does with a gap over the budget.
    enum class Mode : std::uint8_t
    {
        Record,  ///< Report the gap only.
        Enforce, ///< Fail the test.
    };
    Q_ENUM(Mode)

    /// Default budget: the longest UI-thread stall the GUI may show.
    static constexpr int DefaultBudgetMs = 1000;

    /// Creates a disarmed watchdog.
    explicit UiWatchdog(QObject* parent = nullptr);

    /// @return The longest gap measured since arming, in milliseconds.
    [[nodiscard]] int maxGapMs() const noexcept
    {
        return _maxGapMs;
    }

    /// @return The budget in milliseconds.
    [[nodiscard]] int budgetMs() const noexcept
    {
        return _budgetMs;
    }

    /// Sets the budget in milliseconds.
    void setBudgetMs(int budgetMs);

    /// @return Whether the longest gap exceeds the budget.
    [[nodiscard]] bool exceeded() const noexcept
    {
        return _maxGapMs > _budgetMs;
    }

    /// @return The mode a test applies.
    [[nodiscard]] Mode mode() const noexcept
    {
        return _mode;
    }

    /// Sets the mode a test applies.
    void setMode(Mode mode);

    /// @return What the test is doing.
    [[nodiscard]] QString const& step() const noexcept
    {
        return _step;
    }

    /// Names what the test does from now on.
    void setStep(QString const& step);

    /// @return The step of the longest gap (see the property).
    [[nodiscard]] QString const& maxGapStep() const noexcept
    {
        return _maxGapStep;
    }

    /// Stops counting while the test itself, not the application, holds the GUI thread (a fixture
    /// helper copying files, say). Counts the gap up to now first; nothing while not armed.
    Q_INVOKABLE void pause();

    /// Counts again after `pause`, from now on.
    Q_INVOKABLE void resume();

    /// Arms the watchdog once @p window has swapped its first frame from now on, so start-up work
    /// before that frame (QML compilation, for one) does not count. Emits `firstFrame` then.
    Q_INVOKABLE void armOnFirstFrame(QQuickWindow* window);

    /// Arms the watchdog now, resetting the longest gap.
    Q_INVOKABLE void armNow();

    /// Disarms the watchdog, counting the gap since the last heartbeat.
    Q_INVOKABLE void stop();

  Q_SIGNALS:
    /// The longest gap (and with it `exceeded`) changed.
    void maxGapMsChanged();
    /// The budget changed.
    void budgetMsChanged();
    /// The mode changed.
    void modeChanged();
    /// The step changed.
    void stepChanged();
    /// The window passed to `armOnFirstFrame` swapped its first frame; the watchdog is armed.
    void firstFrame();

  private:
    void Beat();

    QTimer _heartbeat;
    QElapsedTimer _sinceLastBeat;
    QMetaObject::Connection _frameConnection;
    int _maxGapMs = 0;
    QString _step;
    /// The step at the last heartbeat, where the next gap begins.
    QString _stepAtLastBeat;
    QString _maxGapStep;
    /// Whether `pause` stopped an armed heartbeat, which `resume` restarts.
    bool _paused = false;
    int _budgetMs = DefaultBudgetMs;
    Mode _mode = Mode::Record;
};

} // namespace DbtoolGui::Tests
