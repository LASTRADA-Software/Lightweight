// SPDX-License-Identifier: Apache-2.0

#include "QmlProgressManager.hpp"

namespace DbtoolGui
{

QmlProgressManager::QmlProgressManager(QObject* parent):
    QObject(parent)
{
}

void QmlProgressManager::Update(Lightweight::SqlBackup::Progress const& progress)
{
    auto const totalRows = progress.totalRows.value_or(0);
    emit progressUpdated(QString::fromStdString(std::string { progress.tableName }),
                         static_cast<int>(progress.state),
                         static_cast<qulonglong>(progress.currentRows),
                         static_cast<qulonglong>(totalRows),
                         QString::fromStdString(std::string { progress.message }));
}

void QmlProgressManager::AllDone()
{
    emit allDone();
}

} // namespace DbtoolGui
