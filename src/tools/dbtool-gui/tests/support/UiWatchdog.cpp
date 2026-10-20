// SPDX-License-Identifier: Apache-2.0

#include "UiWatchdog.hpp"

#include <algorithm>
#include <chrono>
#include <limits>

#include <QtQuick/QQuickWindow>

namespace DbtoolGui::Tests
{

UiWatchdog::UiWatchdog(QObject* parent):
    QObject(parent)
{
    _heartbeat.setInterval(std::chrono::milliseconds { 10 });
    _heartbeat.setTimerType(Qt::PreciseTimer);
    connect(&_heartbeat, &QTimer::timeout, this, &UiWatchdog::Beat);
}

void UiWatchdog::setBudgetMs(int budgetMs)
{
    if (budgetMs == _budgetMs)
        return;
    _budgetMs = budgetMs;
    Q_EMIT budgetMsChanged();
    Q_EMIT maxGapMsChanged();
}

void UiWatchdog::setMode(Mode mode)
{
    if (mode == _mode)
        return;
    _mode = mode;
    Q_EMIT modeChanged();
}

void UiWatchdog::armOnFirstFrame(QQuickWindow* window)
{
    if (window == nullptr)
        return;
    disconnect(_frameConnection);
    // frameSwapped comes from the render thread under the threaded render loop; queue it to this thread.
    _frameConnection = connect(
        window,
        &QQuickWindow::frameSwapped,
        this,
        [this] {
            armNow();
            Q_EMIT firstFrame();
        },
        static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::SingleShotConnection));
    window->update();
}

void UiWatchdog::armNow()
{
    if (_maxGapMs != 0)
    {
        _maxGapMs = 0;
        Q_EMIT maxGapMsChanged();
    }
    _sinceLastBeat.start();
    _heartbeat.start();
}

void UiWatchdog::stop()
{
    disconnect(_frameConnection);
    if (!_heartbeat.isActive())
        return;
    Beat();
    _heartbeat.stop();
}

void UiWatchdog::Beat()
{
    auto const gap = static_cast<int>(std::min<qint64>(_sinceLastBeat.restart(), std::numeric_limits<int>::max()));
    if (gap <= _maxGapMs)
        return;
    _maxGapMs = gap;
    Q_EMIT maxGapMsChanged();
}

} // namespace DbtoolGui::Tests
