// SPDX-License-Identifier: Apache-2.0
//
// `SqlResultModel` is a QAbstractTableModel that backs the SQL Query panel's
// result grid. The runner ships a snapshot of (header, rows) over from a
// worker thread; this model renders it as a tabular view in QML.
//
// Rows are held by value (no streaming) — the runner caps the result-set
// size, so the model never has to deal with unbounded inputs.

#pragma once

#include <QtCore/QAbstractTableModel>
#include <QtCore/QStringList>
#include <QtCore/QVariantList>
#include <QtQmlIntegration/QtQmlIntegration>

#include <vector>

namespace DbtoolGui
{

class SqlResultModel: public QAbstractTableModel
{
    Q_OBJECT
    QML_ELEMENT
  public:
    enum Roles : int
    {
        DisplayRole = Qt::DisplayRole,
        IsNullRole = Qt::UserRole + 1,
    };

    explicit SqlResultModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(QModelIndex const& parent = {}) const override;
    [[nodiscard]] int columnCount(QModelIndex const& parent = {}) const override;
    [[nodiscard]] QVariant data(QModelIndex const& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /// Replaces the model contents in one shot. Header count must match the
    /// width of every row. Triggers a full reset so QML rebinds the table.
    void resetRows(QStringList headers, std::vector<QVariantList> rows);

    /// Drops all rows and headers. Used when the user clears the editor or
    /// connects to a different database.
    void clearRows();

    /// Plain-text contents of one cell. NULL becomes the empty string so
    /// pasting into a spreadsheet does not get the literal `(null)` token
    /// (QML can render `(null)` for display, but the clipboard payload
    /// should be unambiguous data).
    [[nodiscard]] Q_INVOKABLE QString cellText(int row, int column) const;

    /// One row joined by tab. Suitable for pasting into Excel / Calc.
    [[nodiscard]] Q_INVOKABLE QString rowAsTsv(int row) const;

    /// One row encoded as a JSON object (header → value). Useful for
    /// pasting into bug reports or REST request bodies.
    [[nodiscard]] Q_INVOKABLE QString rowAsJson(int row) const;

    /// Header label for the given column (empty string when out of range).
    [[nodiscard]] Q_INVOKABLE QString columnHeader(int column) const;

    /// Whole result set as TSV with a header row. Convenient for piping the
    /// output into a downstream tool.
    [[nodiscard]] Q_INVOKABLE QString allAsTsv() const;

  private:
    QStringList _headers;
    std::vector<QVariantList> _rows;
};

} // namespace DbtoolGui
