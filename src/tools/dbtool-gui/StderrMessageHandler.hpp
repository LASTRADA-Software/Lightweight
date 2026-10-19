// SPDX-License-Identifier: Apache-2.0
//
// Qt message handler shared by dbtool-gui and its test executables: every message goes to
// stderr, never to a dialog or only to the debugger.

#pragma once

#include <cstdio>
#include <cstdlib>
#include <format>

#include <QtCore/QString>
#include <QtCore/qlogging.h>

namespace DbtoolGui
{

/// Writes a Qt log message to stderr as `[LEVEL] message (file:line)`; a fatal message then
/// aborts, which `Lightweight::Tools::DisableErrorDialogs` keeps from opening a dialog.
/// Without it a WIN32-subsystem build drops every message, so a startup problem looks like
/// "the app exited silently". Install with `qInstallMessageHandler`.
inline void WriteQtMessageToStderr(QtMsgType type, QMessageLogContext const& context, QString const& message)
{
    char const* prefix = "UNKNOWN";
    switch (type)
    {
        case QtDebugMsg:
            prefix = "DEBUG";
            break;
        case QtInfoMsg:
            prefix = "INFO";
            break;
        case QtWarningMsg:
            prefix = "WARN";
            break;
        case QtCriticalMsg:
            prefix = "ERROR";
            break;
        case QtFatalMsg:
            prefix = "FATAL";
            break;
    }
    auto const* const file = context.file != nullptr ? context.file : "";
    auto const line = std::format("[{}] {} ({}:{})\n", prefix, message.toLocal8Bit().constData(), file, context.line);
    std::fputs(line.c_str(), stderr);
    std::fflush(stderr);
    if (type == QtFatalMsg)
        std::abort();
}

} // namespace DbtoolGui
