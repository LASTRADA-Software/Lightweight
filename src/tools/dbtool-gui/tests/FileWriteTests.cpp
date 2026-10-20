// SPDX-License-Identifier: Apache-2.0
//
// The file helpers behind the failure report: dialog URLs become local paths, and a failed write
// reports its error and leaves an existing file as it was.

#include "../ControllerJobs.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
#include <QtCore/QUrl>

using DbtoolGui::LocalFilePath;
using DbtoolGui::WriteFileAtomically;

namespace
{

QString ReadAll(QString const& path)
{
    auto file = QFile { path };
    REQUIRE(file.open(QIODevice::ReadOnly));
    return QString::fromUtf8(file.readAll());
}

} // namespace

TEST_CASE("a file dialog URL becomes a local path in native separators", "[dbtool-gui][file-write]")
{
    CHECK(LocalFilePath(QUrl::fromLocalFile(QStringLiteral("/tmp/report.txt")))
          == QDir::toNativeSeparators(QStringLiteral("/tmp/report.txt")));
    CHECK(LocalFilePath(QUrl { QStringLiteral("https://example.org/x") }).isEmpty());
}

#if defined(_WIN32)
TEST_CASE("a file URL with a host becomes the UNC path of the share", "[dbtool-gui][file-write]")
{
    CHECK(LocalFilePath(QUrl { QStringLiteral("file://server/share/x.txt") })
          == QDir::toNativeSeparators(QStringLiteral("//server/share/x.txt")));
    CHECK(LocalFilePath(QUrl { QStringLiteral("file:///C:/dir/x.txt") })
          == QDir::toNativeSeparators(QStringLiteral("C:/dir/x.txt")));
}
#endif

TEST_CASE("an atomic write replaces the file and reports a missing folder", "[dbtool-gui][file-write]")
{
    auto const directory = QTemporaryDir {};
    REQUIRE(directory.isValid());
    auto const path = directory.filePath(QStringLiteral("report.txt"));

    CHECK(WriteFileAtomically(path, QByteArrayLiteral("first")).isEmpty());
    CHECK(ReadAll(path) == QStringLiteral("first"));
    CHECK(WriteFileAtomically(path, QByteArrayLiteral("second")).isEmpty());
    CHECK(ReadAll(path) == QStringLiteral("second"));

    CHECK_FALSE(
        WriteFileAtomically(directory.filePath(QStringLiteral("missing/report.txt")), QByteArrayLiteral("x")).isEmpty());
    CHECK(ReadAll(path) == QStringLiteral("second"));
}
