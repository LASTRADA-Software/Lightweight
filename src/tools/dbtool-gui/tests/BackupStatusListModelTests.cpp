// SPDX-License-Identifier: Apache-2.0

#include "../Models/BackupStatusListModel.hpp"
#include "../Models/BackupTableListModel.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QEvent>
#include <QtCore/QPointer>

using DbtoolGui::BackupStatusListModel;

TEST_CASE("BackupStatusListModel starts empty and resets to the given profiles", "[dbtool-gui][backup-model]")
{
    BackupStatusListModel model;
    CHECK(model.rowCount() == 0);

    model.resetProfiles({ "prod", "dev" });

    REQUIRE(model.rowCount() == 2);
    auto const idx0 = model.index(0, 0);
    CHECK(model.data(idx0, BackupStatusListModel::NameRole).toString() == "prod");
    CHECK(model.data(idx0, BackupStatusListModel::RunStateRole).toString() == "idle");
    CHECK_FALSE(model.data(idx0, BackupStatusListModel::ArchiveExistsRole).toBool());
}

TEST_CASE("setArchiveStatus updates one row and emits dataChanged", "[dbtool-gui][backup-model]")
{
    BackupStatusListModel model;
    model.resetProfiles({ "prod", "dev" });
    auto const stamp = QDateTime::fromString("2026-07-11T14:02:00", Qt::ISODate);

    model.setArchiveStatus("dev", true, 1234, stamp);

    auto const idx = model.index(1, 0);
    CHECK(model.data(idx, BackupStatusListModel::ArchiveExistsRole).toBool());
    CHECK(model.data(idx, BackupStatusListModel::ArchiveSizeRole).toULongLong() == 1234);
    CHECK(model.data(idx, BackupStatusListModel::ArchiveMtimeRole).toDateTime() == stamp);
    // Unknown profile name: silently ignored, no crash.
    model.setArchiveStatus("ghost", true, 1, stamp);
}

TEST_CASE("setRunState transitions and stores error text", "[dbtool-gui][backup-model]")
{
    BackupStatusListModel model;
    model.resetProfiles({ "prod" });
    auto const idx = model.index(0, 0);

    model.setRunState("prod", "running");
    CHECK(model.data(idx, BackupStatusListModel::RunStateRole).toString() == "running");
    CHECK(model.data(idx, BackupStatusListModel::ErrorTextRole).toString().isEmpty());

    model.setRunState("prod", "failed", "login failed");
    CHECK(model.data(idx, BackupStatusListModel::RunStateRole).toString() == "failed");
    CHECK(model.data(idx, BackupStatusListModel::ErrorTextRole).toString() == "login failed");

    // Unknown profile name: silently ignored, no crash.
    model.setRunState("ghost", "running");
}

TEST_CASE("data() returns an invalid QVariant for an out-of-range index or unknown role", "[dbtool-gui][backup-model]")
{
    BackupStatusListModel model;
    model.resetProfiles({ "prod" });

    CHECK_FALSE(model.data(QModelIndex {}, BackupStatusListModel::NameRole).isValid());
    CHECK_FALSE(model.data(model.index(1, 0), BackupStatusListModel::NameRole).isValid());
    CHECK_FALSE(model.data(model.index(0, 0), Qt::DisplayRole).isValid());
}

TEST_CASE("roleNames exposes the QML-facing names", "[dbtool-gui][backup-model]")
{
    BackupStatusListModel model;
    auto const roles = model.roleNames();
    CHECK(roles.value(BackupStatusListModel::NameRole) == "name");
    CHECK(roles.value(BackupStatusListModel::ArchiveExistsRole) == "archiveExists");
    CHECK(roles.value(BackupStatusListModel::ArchiveSizeRole) == "archiveSize");
    CHECK(roles.value(BackupStatusListModel::ArchiveMtimeRole) == "archiveMtime");
    CHECK(roles.value(BackupStatusListModel::RunStateRole) == "runState");
    CHECK(roles.value(BackupStatusListModel::ErrorTextRole) == "errorText");
}

TEST_CASE("resetProfiles creates a per-profile table model reachable via TablesRole", "[dbtool-gui][backup-model]")
{
    DbtoolGui::BackupStatusListModel model;
    model.resetProfiles({ "prod", "dev" });

    auto* prodTables = model.data(model.index(0, 0), DbtoolGui::BackupStatusListModel::TablesRole)
                           .value<DbtoolGui::BackupTableListModel*>();
    REQUIRE(prodTables != nullptr);
    CHECK(prodTables->rowCount() == 0);

    // tablesFor() returns the same instance; unknown name -> nullptr.
    CHECK(model.tablesFor("prod") == prodTables);
    CHECK(model.tablesFor("ghost") == nullptr);
}

TEST_CASE("tablesFor models are distinct per profile", "[dbtool-gui][backup-model]")
{
    DbtoolGui::BackupStatusListModel model;
    model.resetProfiles({ "prod", "dev" });
    CHECK(model.tablesFor("prod") != model.tablesFor("dev"));
}

TEST_CASE("TablesRole appears in roleNames", "[dbtool-gui][backup-model]")
{
    DbtoolGui::BackupStatusListModel model;
    CHECK(model.roleNames().value(DbtoolGui::BackupStatusListModel::TablesRole) == "tables");
}

TEST_CASE("resetProfiles releases the previous per-profile table models", "[dbtool-gui][backup-model]")
{
    DbtoolGui::BackupStatusListModel model;
    model.resetProfiles({ "prod" });
    QPointer<DbtoolGui::BackupTableListModel> firstProdTables = model.tablesFor("prod");
    REQUIRE(!firstProdTables.isNull());

    // Reload the profile list — the old table model must be scheduled for
    // destruction, not leaked as an orphaned child of the status model.
    model.resetProfiles({ "prod", "dev" });

    // deleteLater() defers destruction to the event loop; pump it so the
    // QPointer observes the release.
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    CHECK(firstProdTables.isNull()); // old model destroyed -> QPointer nulled

    // And the reloaded model still exposes fresh, valid table models.
    CHECK(model.tablesFor("prod") != nullptr);
    CHECK(model.tablesFor("dev") != nullptr);
}
