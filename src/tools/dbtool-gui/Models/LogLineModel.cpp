// SPDX-License-Identifier: Apache-2.0

#include "LogLineModel.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <iterator>
#include <ranges>
#include <utility>

#include <QtCore/QByteArray>
#include <QtCore/QTime>
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>

namespace DbtoolGui
{

namespace
{
    /// How long appended lines wait to be shown together.
    constexpr auto FlushInterval = std::chrono::milliseconds { 50 };

    /// The level tag of a line in `text()`, padded to one width.
    QString LevelTag(LogLevel level)
    {
        static auto const tags = std::array {
            QStringLiteral("INFO "),
            QStringLiteral("WARN "),
            QStringLiteral("ERROR"),
        };
        auto const index = static_cast<std::size_t>(level);
        return index < tags.size() ? tags[index] : tags[0];
    }
} // namespace

LogLineModel::LogLineModel(QObject* parent):
    QAbstractListModel(parent)
{
    _flushTimer.setSingleShot(true);
    _flushTimer.setInterval(FlushInterval);
    QObject::connect(&_flushTimer, &QTimer::timeout, this, &LogLineModel::Flush);
}

int LogLineModel::rowCount(QModelIndex const& parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(_lines.size());
}

QVariant LogLineModel::data(QModelIndex const& index, int role) const
{
    auto const row = index.row();
    if (row < 0 || row >= static_cast<int>(_lines.size()))
        return {};
    auto const& line = _lines[static_cast<std::size_t>(row)];
    switch (role)
    {
        case TimeRole:
            return line.time;
        case LevelRole:
            return static_cast<int>(line.level);
        case Qt::DisplayRole:
        case TextRole:
            return line.text;
        default:
            return {};
    }
}

QHash<int, QByteArray> LogLineModel::roleNames() const
{
    return {
        { TimeRole, "time" },
        { LevelRole, "level" },
        { TextRole, "text" },
    };
}

void LogLineModel::Append(QString line, LogLevel level)
{
    _pending.push_back(Line {
        .time = QTime::currentTime().toString(QStringLiteral("hh:mm:ss")),
        .level = level,
        .text = std::move(line),
    });
    if (_pending.size() > static_cast<std::size_t>(Capacity))
    {
        _pending.pop_front();
        ++_dropped;
    }
    if (!_flushTimer.isActive())
        _flushTimer.start();
}

void LogLineModel::Flush()
{
    if (_pending.empty())
        return;
    auto const incoming = static_cast<int>(_pending.size());
    if (auto const overflow = OverflowOnFlush(); overflow > 0)
    {
        beginRemoveRows({}, 0, overflow - 1);
        _lines.erase(_lines.begin(), _lines.begin() + overflow);
        _dropped += overflow;
        endRemoveRows();
    }
    auto const first = static_cast<int>(_lines.size());
    beginInsertRows({}, first, first + incoming - 1);
    std::ranges::move(_pending, std::back_inserter(_lines));
    _pending.clear();
    endInsertRows();
    emit countChanged();
}

int LogLineModel::count() const noexcept
{
    return static_cast<int>(_lines.size());
}

int LogLineModel::OverflowOnFlush() const noexcept
{
    auto const kept = static_cast<int>(_lines.size());
    return std::clamp(kept + static_cast<int>(_pending.size()) - Capacity, 0, kept);
}

int LogLineModel::droppedCount() const noexcept
{
    return _dropped + OverflowOnFlush();
}

QString LogLineModel::text() const
{
    auto out = QString {};
    auto const appendLine = [&out](QString const& line) {
        if (!out.isEmpty())
            out += QLatin1Char('\n');
        out += line;
    };
    if (auto const dropped = droppedCount(); dropped > 0)
        appendLine(QStringLiteral("… %1 earlier lines dropped").arg(dropped));
    auto const append = [&appendLine](Line const& line) {
        appendLine(line.time + QStringLiteral("  ") + LevelTag(line.level) + QStringLiteral("  ") + line.text);
    };
    std::ranges::for_each(_lines | std::views::drop(OverflowOnFlush()), append);
    std::ranges::for_each(_pending, append);
    return out;
}

void LogLineModel::copyToClipboard() const
{
    if (auto* const clipboard = QGuiApplication::clipboard())
        clipboard->setText(text());
}

void LogLineModel::clear()
{
    _flushTimer.stop();
    beginResetModel();
    _lines.clear();
    _pending.clear();
    _dropped = 0;
    endResetModel();
    emit countChanged();
}

} // namespace DbtoolGui
