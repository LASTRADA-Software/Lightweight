// SPDX-License-Identifier: Apache-2.0
//
// `LogLineModel` holds the log pane's lines: the newest `Capacity` of them, each with its receive time
// and level. Lines are added in batches, so a burst of log output costs one insert (and at most one
// removal of the oldest lines) instead of one per line.

#pragma once

#include "../LogLevel.hpp"

#include <deque>

#include <QtCore/QAbstractListModel>
#include <QtCore/QTimer>
#include <QtQmlIntegration/QtQmlIntegration>

namespace DbtoolGui
{

/// The bounded list of log lines behind the log pane.
class LogLineModel: public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by AppController")

    /// The number of lines the view shows.
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    /// The number of lines dropped to stay within `Capacity` since the last `clear()`.
    Q_PROPERTY(int droppedCount READ droppedCount NOTIFY countChanged)

  public:
    /// The most lines kept; older ones are dropped.
    static constexpr int Capacity = 10000;

    /// The roles a delegate reads.
    enum Roles : int
    {
        /// The receive time, `hh:mm:ss`.
        TimeRole = Qt::UserRole + 1,
        /// The `LogLevel`, as an int.
        LevelRole,
        /// The message.
        TextRole,
    };
    Q_ENUM(Roles)

    /// @param parent The QObject parent.
    explicit LogLineModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(QModelIndex const& parent = {}) const override;
    [[nodiscard]] QVariant data(QModelIndex const& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /// Adds @p line at @p level, stamped with the current time. The view receives it with the next
    /// batch, shortly after; the oldest lines beyond `Capacity` are dropped then.
    void Append(QString line, LogLevel level);

    /// @return The number of lines the view shows.
    [[nodiscard]] int count() const noexcept;

    /// @return The number of lines dropped since the last `clear()`, the ones the next batch drops included.
    [[nodiscard]] int droppedCount() const noexcept;

    /// @return The newest `Capacity` lines, the ones not yet shown included, as `hh:mm:ss  LEVEL  message`,
    ///         one per line; after a first line `… N earlier lines dropped` when lines were dropped.
    Q_INVOKABLE QString text() const;

    /// Puts `text()` on the clipboard.
    Q_INVOKABLE void copyToClipboard() const;

    /// Removes every line.
    Q_INVOKABLE void clear();

  signals:
    /// `count` or `droppedCount` changed.
    void countChanged();

  private:
    struct Line
    {
        QString time;
        LogLevel level = LogLevel::Info;
        QString text;
    };

    /// Moves the lines appended since the last batch into the view.
    void Flush();

    /// @return How many of the shown lines the next batch drops.
    [[nodiscard]] int OverflowOnFlush() const noexcept;

    std::deque<Line> _lines;
    std::deque<Line> _pending;
    int _dropped = 0;
    QTimer _flushTimer;
};

} // namespace DbtoolGui
