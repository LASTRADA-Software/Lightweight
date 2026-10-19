// SPDX-License-Identifier: Apache-2.0
//
// One row per task of a run in flight (pre-migrate steps, their units and tables, the
// migrations), fed by `Lightweight::ProgressTask`s that a worker thread drives.

#pragma once

#include <Lightweight/ProgressTask.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

#include <QtCore/QAbstractListModel>
#include <QtCore/QList>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <QtQmlIntegration/QtQmlIntegration>

namespace DbtoolGui
{

/// @brief The task tree of a run as a flat list model, in tree order, with each row's depth.
///
/// `CreateRootTask` hands out a root task that may be driven from any thread. Opening and ending a
/// task posts a queued call to the model, so rows appear and end in order on the GUI thread.
/// `SetTotal`, `Advance` and `Fail` only update a shared snapshot of the task under a mutex; a
/// timer copies the changed snapshots into their rows every `UpdateInterval`, so a task that
/// advances per row changes its row at most once per interval and no update is lost. The root
/// task has no row. A finished sub-task's row disappears, a finished top-level row stays until
/// `clear()`, and a failed row always stays, so the user sees what failed and why. `Fail` also
/// sends `failed <title>: <reason>` to the run's log callback at error level.
class ProgressTaskModel: public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by MigrationRunner")

    /// Rows in the model.
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    /// Whether any row is still running.
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)

  public:
    /// The model's roles.
    enum Role : std::uint16_t
    {
        TitleRole = Qt::UserRole + 1, ///< The task's title.
        DepthRole,                    ///< 0 for a top-level task, its parent's depth + 1 otherwise.
        TotalRole,                    ///< Units of work; 0 = indeterminate.
        DoneRole,                     ///< Units of work done.
        DetailRole,                   ///< What the task does now.
        ElapsedMsRole,                ///< Milliseconds since the task began, frozen once it finished.
        IndeterminateRole,            ///< Whether the total is unknown (0).
        FinishedRole,                 ///< Whether the task ended.
        FailedRole,                   ///< Whether the task failed.
        FailureReasonRole,            ///< Why the task failed; empty otherwise.
    };
    Q_ENUM(Role)

    /// How often changed task snapshots are copied into their rows.
    static constexpr std::chrono::milliseconds UpdateInterval { 50 };
    /// How often the elapsed time of running rows is refreshed.
    static constexpr std::chrono::milliseconds ElapsedInterval { 250 };

    /// The time source; called from the GUI thread and from the threads driving the tasks.
    using Clock = std::function<std::chrono::steady_clock::time_point()>;

    /// @param parent The QObject parent.
    /// @param clock The time source; empty uses `std::chrono::steady_clock::now`.
    explicit ProgressTaskModel(QObject* parent = nullptr, Clock clock = {});

    /// Detaches the tasks still alive from this model; they keep working and report nowhere.
    ~ProgressTaskModel() override;

    ProgressTaskModel(ProgressTaskModel const&) = delete;
    ProgressTaskModel(ProgressTaskModel&&) = delete;
    ProgressTaskModel& operator=(ProgressTaskModel const&) = delete;
    ProgressTaskModel& operator=(ProgressTaskModel&&) = delete;

    /// Opens the root task of a run. Callable from any thread.
    /// @param isCancellationRequested What every task of the run answers to `IsCancellationRequested`; empty = never.
    /// @param log Receives every task's `Log` call, on the calling thread; empty drops them.
    /// @return The root task; it has no row, its sub-tasks do.
    [[nodiscard]] std::unique_ptr<Lightweight::ProgressTask> CreateRootTask(
        std::function<bool()> isCancellationRequested,
        std::function<void(Lightweight::ProgressTask::LogLevel, QString const&)> log);

    /// Removes every row. Tasks still alive keep working without a row.
    Q_INVOKABLE void clear();

    /// Copies every changed task snapshot into its row now; the timer does this every `UpdateInterval`.
    void pollNow();

    /// @return Whether any row is still running.
    [[nodiscard]] bool active() const noexcept
    {
        return _active;
    }

    [[nodiscard]] int rowCount(QModelIndex const& parent = {}) const override;
    [[nodiscard]] QVariant data(QModelIndex const& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

  signals:
    /// Emitted when rows were added or removed.
    void countChanged();
    /// Emitted when `active` flips.
    void activeChanged();

  private:
    struct Registry;
    class QueuedTask;

    /// One task's row.
    struct Row
    {
        std::uint64_t id = 0;
        int depth = 0;
        QString title;
        std::uint64_t total = 0;
        std::uint64_t done = 0;
        QString detail;
        bool failed = false;
        QString failureReason;
        std::chrono::steady_clock::time_point started;
        std::optional<std::chrono::steady_clock::time_point> finished;
    };

    /// Adds the row of a task that began, after its parent's last descendant.
    void ApplyBegin(std::uint64_t id,
                    std::uint64_t parentId,
                    QString const& title,
                    std::uint64_t total,
                    std::chrono::steady_clock::time_point started);

    /// Applies a task's last snapshot, then removes its row or marks it finished.
    void ApplyEnd(std::uint64_t id, std::chrono::steady_clock::time_point finished);

    /// Copies the task's snapshot into its row when it changed, clearing its dirty flag.
    /// @return Whether the row changed.
    bool ApplySnapshotLocked(Row& row);

    /// Polls the snapshots and, every `ElapsedInterval`, refreshes the running rows' elapsed time.
    void OnTimer();

    /// Recomputes `active`, emitting `activeChanged` when it flips, and stops the timer when idle.
    void UpdateActive();

    /// @return The row index of task @p id, or -1.
    [[nodiscard]] int RowOf(std::uint64_t id) const;

    /// @return The index after the last row below row @p rowIndex in the tree.
    [[nodiscard]] int EndOfSubtree(int rowIndex) const;

    Clock _clock;
    std::shared_ptr<Registry> _registry;
    QList<Row> _rows;
    QTimer _timer;
    std::chrono::steady_clock::time_point _lastElapsedRefresh;
    bool _active = false;
};

} // namespace DbtoolGui
