// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#if defined(_WIN32) || defined(_WIN64)
    #include <dpapi.h>
    #include <windows.h>
    #pragma comment(lib, "Crypt32.lib")
#endif

#include <optional>
#include <string>

struct Credentials
{
    QString username;
    QString password;
};

// Encrypts using Windows DPAPI (current-user scope — only same user/machine can decrypt).
// Falls back to plain storage on non-Windows.
class CredentialStore
{
  public:
    static QString filePath()
    {
        return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("dbtool-gui.creds"));
    }

    static void save(Credentials const& creds)
    {
        QJsonObject obj;
        obj[QStringLiteral("username")] = creds.username;
        obj[QStringLiteral("password")] = encryptToBase64(creds.password);
        QFile f(filePath());
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            f.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    }

    static std::optional<Credentials> load()
    {
        QFile f(filePath());
        if (!f.open(QIODevice::ReadOnly))
            return std::nullopt;
        auto const doc = QJsonDocument::fromJson(f.readAll());
        if (!doc.isObject())
            return std::nullopt;
        auto const obj = doc.object();
        auto const pwd = decryptFromBase64(obj[QStringLiteral("password")].toString());
        if (!pwd)
            return std::nullopt;
        return Credentials {
            .username = obj[QStringLiteral("username")].toString(),
            .password = *pwd,
        };
    }

  private:
    static QString encryptToBase64(QString const& plaintext)
    {
#if defined(_WIN32) || defined(_WIN64)
        auto const utf8 = plaintext.toUtf8();
        DATA_BLOB input { static_cast<DWORD>(utf8.size()),
                          reinterpret_cast<BYTE*>(const_cast<char*>(utf8.data())) };
        DATA_BLOB output {};
        // 0 flags = current-user scope (default DPAPI behaviour)
        if (!CryptProtectData(&input, nullptr, nullptr, nullptr, nullptr, 0, &output))
            return plaintext; // fallback: store plain if DPAPI fails
        QByteArray blob(reinterpret_cast<char const*>(output.pbData), static_cast<qsizetype>(output.cbData));
        LocalFree(output.pbData);
        return QString::fromLatin1(blob.toBase64());
#else
        return plaintext;
#endif
    }

    static std::optional<QString> decryptFromBase64(QString const& base64)
    {
#if defined(_WIN32) || defined(_WIN64)
        auto const blob = QByteArray::fromBase64(base64.toLatin1());
        DATA_BLOB input { static_cast<DWORD>(blob.size()),
                          reinterpret_cast<BYTE*>(const_cast<char*>(blob.data())) };
        DATA_BLOB output {};
        if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, 0, &output))
        {
            // Maybe stored plain (old file or DPAPI failed on save) — return as-is
            return base64;
        }
        QString result = QString::fromUtf8(reinterpret_cast<char const*>(output.pbData),
                                           static_cast<qsizetype>(output.cbData));
        LocalFree(output.pbData);
        return result;
#else
        return base64;
#endif
    }
};
