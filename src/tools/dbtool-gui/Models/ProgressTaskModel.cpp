// SPDX-License-Identifier: Apache-2.0

#include "ProgressTaskModel.hpp"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <ranges>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <QtCore/QMetaObject>

namespace DbtoolGui
{

namespace
{
    /// The callbacks every task of one run shares.
    struct RunCallbacks
    {
        std::function<bool()> isCancellationRequested;
        std::function<void(Lightweight::ProgressTask::LogLevel, QString const&)> log;
    };

    [[nodiscard]] QString FromUtf8(std::string_view text)
    {
        return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
    }
} // namespace

/// The state shared by the model and every task it handed out; it outlives whichever goes first.
struct ProgressTaskModel::Registry
{
    /// The latest state of a task, written by the task's thread and read by the GUI thread.
    struct Snapshot
    {
        std::uint64_t total = 0;
        std::uint64_t done = 0;
        QString detail;
        bool failed = false;
        QString failureReason;
        bool dirty = false;
    };

    explicit Registry(Clock clockSource, ProgressTaskModel* owner):
        clock { std::move(clockSource) },
        model { owner }
    {
    }

    /// Calls @p apply on the model's thread, unless the model is gone by then.
    template <typename Apply>
    void Post(Apply apply)
    {
        // Under the mutex, so the model cannot be destroyed while posting; Qt drops the call once it is.
        auto const lock = std::scoped_lock { mutex };
        if (model)
            QMetaObject::invokeMethod(
                model, [target = model, apply = std::move(apply)] { apply(*target); }, Qt::QueuedConnection);
    }

    Clock const clock;
    std::mutex mutex;
    std::atomic<std::uint64_t> nextId { 1 };
    std::unordered_map<std::uint64_t, Snapshot> snapshots; ///< Guarded by `mutex`.
    ProgressTaskModel* model;                              ///< Guarded by `mutex`; null once the model is gone.
};

/// A task whose sub-tasks become rows of the model. The root task (id 0) has no row.
class ProgressTaskModel::QueuedTask final: public Lightweight::ProgressTask
{
  public:
    QueuedTask(std::shared_ptr<Registry> registry,
               std::shared_ptr<RunCallbacks const> callbacks,
               std::uint64_t id,
               QString title):
        _registry { std::move(registry) },
        _callbacks { std::move(callbacks) },
        _id { id },
        _title { std::move(title) }
    {
    }

    QueuedTask(QueuedTask const&) = delete;
    QueuedTask(QueuedTask&&) = delete;
    QueuedTask& operator=(QueuedTask const&) = delete;
    QueuedTask& operator=(QueuedTask&&) = delete;

    ~QueuedTask() override
    {
        if (_id == 0)
            return;
        auto const cancellation = IsCancellationRequested() ? CancellationAtEnd::Requested : CancellationAtEnd::NotRequested;
        _registry->Post([id = _id, finished = _registry->clock(), cancellation](ProgressTaskModel& model) {
            model.ApplyEnd(id, finished, cancellation);
        });
    }

    [[nodiscard]] std::unique_ptr<ProgressTask> BeginSubTask(std::string_view title, std::uint64_t total) override
    {
        auto const id = _registry->nextId.fetch_add(1, std::memory_order_relaxed);
        {
            auto const lock = std::scoped_lock { _registry->mutex };
            _registry->snapshots[id].total = total;
        }
        auto rowTitle = FromUtf8(title);
        _registry->Post([id, parentId = _id, title = rowTitle, total, started = _registry->clock()](
                            ProgressTaskModel& model) { model.ApplyBegin(id, parentId, title, total, started); });
        return std::make_unique<QueuedTask>(_registry, _callbacks, id, std::move(rowTitle));
    }

    void SetTotal(std::uint64_t total) override
    {
        Update([total](Registry::Snapshot& snapshot) {
            snapshot.total = total;
            if (total != 0)
                snapshot.done = std::min(snapshot.done, total);
        });
    }

    void Advance(std::uint64_t count, std::string_view detail) override
    {
        // Converted before taking the mutex, which the GUI thread's poll also takes.
        auto converted = detail.empty() ? QString {} : FromUtf8(detail);
        Update([count, &converted](Registry::Snapshot& snapshot) {
            snapshot.done += count;
            if (snapshot.total != 0)
                snapshot.done = std::min(snapshot.done, snapshot.total);
            if (!converted.isEmpty())
                snapshot.detail = std::move(converted);
        });
    }

    void Log(LogLevel level, std::string_view message) override
    {
        if (_callbacks->log)
            _callbacks->log(level, FromUtf8(message));
    }

    [[nodiscard]] bool IsCancellationRequested() const override
    {
        return _callbacks->isCancellationRequested && _callbacks->isCancellationRequested();
    }

    void Fail(std::string_view reason) override
    {
        auto const converted = FromUtf8(reason);
        Update([&converted](Registry::Snapshot& snapshot) {
            snapshot.failed = true;
            snapshot.failureReason = converted;
        });
        // Also in the log, so the log panel and the failure report name what failed.
        if (_callbacks->log)
            _callbacks->log(LogLevel::Error,
                            _title.isEmpty() ? QStringLiteral("failed: %1").arg(converted)
                                             : QStringLiteral("failed %1: %2").arg(_title, converted));
    }

  private:
    /// Changes this task's snapshot and marks it dirty; a task without a snapshot (the root, a cleared row) ignores it.
    template <typename Change>
    void Update(Change const& change)
    {
        auto const lock = std::scoped_lock { _registry->mutex };
        auto const found = _registry->snapshots.find(_id);
        if (found == _registry->snapshots.end())
            return;
        change(found->second);
        found->second.dirty = true;
    }

    std::shared_ptr<Registry> _registry;
    std::shared_ptr<RunCallbacks const> _callbacks;
    std::uint64_t _id;
    QString _title; ///< Empty for the root task.
};

ProgressTaskModel::ProgressTaskModel(QObject* parent, Clock clock):
    QAbstractListModel(parent),
    _clock { clock ? std::move(clock) : Clock { [] { return std::chrono::steady_clock::now(); } } },
    _registry { std::make_shared<Registry>(_clock, this) }
{
    _timer.setInterval(UpdateInterval);
    connect(&_timer, &QTimer::timeout, this, &ProgressTaskModel::OnTimer);
}

ProgressTaskModel::~ProgressTaskModel()
{
    auto const lock = std::scoped_lock { _registry->mutex };
    _registry->model = nullptr;
}

std::unique_ptr<Lightweight::ProgressTask> ProgressTaskModel::CreateRootTask(
    std::function<bool()> isCancellationRequested,
    std::function<void(Lightweight::ProgressTask::LogLevel, QString const&)> log)
{
    auto callbacks = std::make_shared<RunCallbacks const>(
        RunCallbacks { .isCancellationRequested = std::move(isCancellationRequested), .log = std::move(log) });
    return std::make_unique<QueuedTask>(_registry, std::move(callbacks), 0, QString {});
}

void ProgressTaskModel::clear()
{
    beginResetModel();
    {
        auto const lock = std::scoped_lock { _registry->mutex };
        for (auto const& row: _rows)
            _registry->snapshots.erase(row.id);
    }
    _rows.clear();
    endResetModel();
    emit countChanged();
    UpdateActive();
}

void ProgressTaskModel::pollNow()
{
    auto changedRows = QList<int> {};
    {
        auto const lock = std::scoped_lock { _registry->mutex };
        // Index loops: libc++ lacks views::enumerate.
        for (auto const rowIndex: std::views::iota(qsizetype { 0 }, _rows.size()))
            if (ApplySnapshotLocked(_rows[rowIndex]))
                changedRows.append(static_cast<int>(rowIndex));
    }
    for (auto const rowIndex: changedRows)
        emit dataChanged(index(rowIndex, 0),
                         index(rowIndex, 0),
                         { TotalRole, DoneRole, DetailRole, IndeterminateRole, FailedRole, FailureReasonRole });
}

bool ProgressTaskModel::ApplySnapshotLocked(Row& row)
{
    auto const found = _registry->snapshots.find(row.id);
    if (found == _registry->snapshots.end() || !found->second.dirty)
        return false;
    auto& snapshot = found->second;
    row.total = snapshot.total;
    row.done = snapshot.done;
    row.detail = snapshot.detail;
    row.failed = snapshot.failed;
    row.failureReason = snapshot.failureReason;
    snapshot.dirty = false;
    return true;
}

void ProgressTaskModel::OnTimer()
{
    pollNow();
    auto const now = _clock();
    if (now - _lastElapsedRefresh < ElapsedInterval)
        return;
    _lastElapsedRefresh = now;
    for (auto const rowIndex: std::views::iota(qsizetype { 0 }, _rows.size()))
        if (!std::as_const(_rows)[rowIndex].finished)
            emit dataChanged(index(static_cast<int>(rowIndex), 0), index(static_cast<int>(rowIndex), 0), { ElapsedMsRole });
}

void ProgressTaskModel::ApplyBegin(std::uint64_t id,
                                   std::uint64_t parentId,
                                   QString const& title,
                                   std::uint64_t total,
                                   std::chrono::steady_clock::time_point started)
{
    auto const parentRow = parentId == 0 ? -1 : RowOf(parentId);
    auto const depth = parentRow < 0 ? 0 : _rows[parentRow].depth + 1;
    auto const position = parentRow < 0 ? static_cast<int>(_rows.size()) : EndOfSubtree(parentRow);

    beginInsertRows({}, position, position);
    auto row = Row {};
    row.id = id;
    row.depth = depth;
    row.title = title;
    row.total = total;
    row.started = started;
    _rows.insert(position, std::move(row));
    endInsertRows();
    emit countChanged();
    UpdateActive();
    if (!_timer.isActive())
        _timer.start();
}

void ProgressTaskModel::ApplyEnd(std::uint64_t id,
                                 std::chrono::steady_clock::time_point finished,
                                 CancellationAtEnd cancellation)
{
    auto const rowIndex = RowOf(id);
    {
        auto const lock = std::scoped_lock { _registry->mutex };
        if (rowIndex >= 0)
            ApplySnapshotLocked(_rows[rowIndex]);
        _registry->snapshots.erase(id);
    }
    if (rowIndex < 0)
        return;

    auto& row = _rows[rowIndex];
    // A row that failed, or still has failed rows below it, stays so the failure keeps its context.
    auto const keep = row.depth == 0 || row.failed || EndOfSubtree(rowIndex) > rowIndex + 1;
    if (keep)
    {
        row.finished = finished;
        row.cancelled =
            cancellation == CancellationAtEnd::Requested && !row.failed && row.total != 0 && row.done < row.total;
        emit dataChanged(index(rowIndex, 0),
                         index(rowIndex, 0),
                         { TotalRole,
                           DoneRole,
                           DetailRole,
                           IndeterminateRole,
                           FailedRole,
                           FailureReasonRole,
                           CancelledRole,
                           FinishedRole,
                           ElapsedMsRole });
    }
    else
    {
        beginRemoveRows({}, rowIndex, rowIndex);
        _rows.removeAt(rowIndex);
        endRemoveRows();
        emit countChanged();
    }
    UpdateActive();
}

void ProgressTaskModel::UpdateActive()
{
    auto const active = std::ranges::any_of(_rows, [](Row const& row) { return !row.finished.has_value(); });
    if (!active)
        _timer.stop();
    if (active == _active)
        return;
    _active = active;
    emit activeChanged();
}

int ProgressTaskModel::RowOf(std::uint64_t id) const
{
    auto const found = std::ranges::find(_rows, id, &Row::id);
    return found == _rows.end() ? -1 : static_cast<int>(found - _rows.begin());
}

int ProgressTaskModel::EndOfSubtree(int rowIndex) const
{
    auto const depth = _rows[rowIndex].depth;
    auto const below = std::ranges::find_if(
        _rows.begin() + rowIndex + 1, _rows.end(), [depth](Row const& row) { return row.depth <= depth; });
    return static_cast<int>(below - _rows.begin());
}

int ProgressTaskModel::rowCount(QModelIndex const& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(_rows.size());
}

QVariant ProgressTaskModel::data(QModelIndex const& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= _rows.size())
        return {};
    auto const& row = _rows[index.row()];
    switch (role)
    {
        case TitleRole:
            return row.title;
        case DepthRole:
            return row.depth;
        case TotalRole:
            return static_cast<qulonglong>(row.total);
        case DoneRole:
            return static_cast<qulonglong>(row.done);
        case DetailRole:
            return row.detail;
        case ElapsedMsRole:
            return static_cast<qlonglong>(
                std::chrono::duration_cast<std::chrono::milliseconds>(row.finished.value_or(_clock()) - row.started)
                    .count());
        case IndeterminateRole:
            return row.total == 0;
        case FinishedRole:
            return row.finished.has_value();
        case FailedRole:
            return row.failed;
        case FailureReasonRole:
            return row.failureReason;
        case CancelledRole:
            return row.cancelled;
        default:
            return {};
    }
}

QHash<int, QByteArray> ProgressTaskModel::roleNames() const
{
    return {
        { TitleRole, "title" },
        { DepthRole, "depth" },
        { TotalRole, "total" },
        { DoneRole, "done" },
        { DetailRole, "detail" },
        { ElapsedMsRole, "elapsedMs" },
        { IndeterminateRole, "indeterminate" },
        { FinishedRole, "finished" },
        { FailedRole, "failed" },
        { FailureReasonRole, "failureReason" },
        { CancelledRole, "cancelled" },
    };
}

} // namespace DbtoolGui
