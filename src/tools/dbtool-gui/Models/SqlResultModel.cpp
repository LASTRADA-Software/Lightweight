// SPDX-License-Identifier: Apache-2.0

#include "SqlResultModel.hpp"

#include <utility>

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace DbtoolGui
{

SqlResultModel::SqlResultModel(QObject* parent):
    QAbstractTableModel(parent)
{
}

int SqlResultModel::rowCount(QModelIndex const& parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(_rows.size());
}

int SqlResultModel::columnCount(QModelIndex const& parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(_headers.size());
}

QVariant SqlResultModel::data(QModelIndex const& index, int role) const
{
    if (!index.isValid())
        return {};
    auto const row = index.row();
    auto const col = index.column();
    if (row < 0 || static_cast<size_t>(row) >= _rows.size())
        return {};
    if (col < 0 || col >= _rows[static_cast<size_t>(row)].size())
        return {};

    auto const& cell = _rows[static_cast<size_t>(row)].at(col);
    switch (role)
    {
        case Qt::DisplayRole:
            return cell.isNull() ? QVariant(QStringLiteral("(null)")) : cell;
        case IsNullRole:
            return cell.isNull();
        default:
            return {};
    }
}

QVariant SqlResultModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole)
        return {};
    if (orientation == Qt::Horizontal)
    {
        if (section < 0 || section >= _headers.size())
            return {};
        return _headers.at(section);
    }
    // Vertical headers: 1-based row numbers, matching most SQL clients.
    return section + 1;
}

QHash<int, QByteArray> SqlResultModel::roleNames() const
{
    return {
        { Qt::DisplayRole, "display" },
        { IsNullRole, "isNull" },
    };
}

void SqlResultModel::resetRows(QStringList headers, std::vector<QVariantList> rows)
{
    beginResetModel();
    _headers = std::move(headers);
    _rows = std::move(rows);
    endResetModel();
}

void SqlResultModel::clearRows()
{
    beginResetModel();
    _headers.clear();
    _rows.clear();
    endResetModel();
}

QString SqlResultModel::cellText(int row, int column) const
{
    if (row < 0 || static_cast<size_t>(row) >= _rows.size())
        return {};
    auto const& cells = _rows[static_cast<size_t>(row)];
    if (column < 0 || column >= cells.size())
        return {};
    auto const& cell = cells.at(column);
    if (cell.isNull())
        return {};
    return cell.toString();
}

QString SqlResultModel::rowAsTsv(int row) const
{
    if (row < 0 || static_cast<size_t>(row) >= _rows.size())
        return {};
    auto const& cells = _rows[static_cast<size_t>(row)];
    QStringList parts;
    parts.reserve(cells.size());
    for (auto const& cell: cells)
        parts << (cell.isNull() ? QString {} : cell.toString());
    return parts.join(QLatin1Char('\t'));
}

QString SqlResultModel::rowAsJson(int row) const
{
    if (row < 0 || static_cast<size_t>(row) >= _rows.size())
        return {};
    auto const& cells = _rows[static_cast<size_t>(row)];
    QJsonObject obj;
    auto const cols = std::min<qsizetype>(cells.size(), _headers.size());
    for (qsizetype i = 0; i < cols; ++i)
    {
        auto const& cell = cells.at(i);
        if (cell.isNull())
            obj.insert(_headers.at(i), QJsonValue::Null);
        else
            obj.insert(_headers.at(i), cell.toString());
    }
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QString SqlResultModel::columnHeader(int column) const
{
    if (column < 0 || column >= _headers.size())
        return {};
    return _headers.at(column);
}

QString SqlResultModel::allAsTsv() const
{
    QStringList lines;
    lines.reserve(static_cast<int>(_rows.size()) + 1);
    lines << _headers.join(QLatin1Char('\t'));
    for (auto const& cells: _rows)
    {
        QStringList parts;
        parts.reserve(cells.size());
        for (auto const& cell: cells)
            parts << (cell.isNull() ? QString {} : cell.toString());
        lines << parts.join(QLatin1Char('\t'));
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace DbtoolGui
