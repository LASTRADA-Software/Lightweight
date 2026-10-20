// SPDX-License-Identifier: Apache-2.0

#include "UiWatchdog.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <utility>

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

void UiWatchdog::setStep(QString const& step)
{
    if (step == _step)
        return;
    _step = step;
    Q_EMIT stepChanged();
}

void UiWatchdog::pause()
{
    if (!_heartbeat.isActive())
        return;
    Beat();
    _heartbeat.stop();
    _paused = true;
}

void UiWatchdog::resume()
{
    if (!std::exchange(_paused, false))
        return;
    _stepAtLastBeat = _step;
    _sinceLastBeat.start();
    _heartbeat.start();
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
    _paused = false;
    _stepAtLastBeat = _step;
    if (_maxGapMs != 0)
    {
        _maxGapMs = 0;
        _maxGapStep.clear();
        Q_EMIT maxGapMsChanged();
    }
    _sinceLastBeat.start();
    _heartbeat.start();
}

void UiWatchdog::stop()
{
    disconnect(_frameConnection);
    _paused = false;
    if (!_heartbeat.isActive())
        return;
    Beat();
    _heartbeat.stop();
}

void UiWatchdog::Beat()
{
    auto const gap = static_cast<int>(std::min<qint64>(_sinceLastBeat.restart(), std::numeric_limits<int>::max()));
    auto const begunIn = std::exchange(_stepAtLastBeat, _step);
    if (gap <= _maxGapMs)
        return;
    _maxGapMs = gap;
    _maxGapStep = begunIn == _step ? _step : QStringLiteral("%1 → %2").arg(begunIn, _step);
    Q_EMIT maxGapMsChanged();
}

} // namespace DbtoolGui::Tests
