// SPDX-License-Identifier: Apache-2.0
//
// `ProfileListModel` is a thin QAbstractListModel adaptor on top of
// `Lightweight::Config::ProfileStore`. It is backed by a snapshot that the
// controller refreshes explicitly — the underlying store is mutated only from
// the UI thread, so we do not need thread-safe change tracking here.

#pragma once

#include <Lightweight/Config/ProfileStore.hpp>

#include <vector>

#include <QtCore/QAbstractListModel>
#include <QtQmlIntegration/QtQmlIntegration>

namespace DbtoolGui
{

class ProfileListModel: public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
  public:
    enum Roles : int
    {
        NameRole = Qt::UserRole + 1,
        PluginsDirRole,
        SchemaRole,
        ConnectionStringRole,
        DsnRole,
        UidRole,
        SecretRefRole,
        IsDefaultRole,
    };
    Q_ENUM(Roles)

    explicit ProfileListModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(QModelIndex const& parent = {}) const override;
    [[nodiscard]] QVariant data(QModelIndex const& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /// Replaces the backing snapshot and triggers a full model reset. Called by
    /// `AppController` whenever the user opens / saves a profile file.
    void ReplaceFrom(Lightweight::Config::ProfileStore const& store);

    /// Returns a profile by name, or nullptr if none matches. Used by the
    /// controller when the user selects a row from QML.
    [[nodiscard]] Lightweight::Config::Profile const* Find(QString const& name) const;

  private:
    std::vector<Lightweight::Config::Profile> _profiles;
    QString _defaultProfile;
};

} // namespace DbtoolGui
