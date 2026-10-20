// SPDX-License-Identifier: Apache-2.0
//
// The Qt Quick Test setup object of the workflow tests: registers the `DbtoolGuiWorkflow` module.

#pragma once

#include "UiWatchdog.hpp"
#include "WorkflowFixture.hpp"

#include <QtCore/QObject>
#include <QtQml/QQmlEngine>
#include <QtQml/qqml.h>

namespace DbtoolGui::Tests
{

/// Registers `UiWatchdog` and the `WorkflowFixture` singleton as module `DbtoolGuiWorkflow` 1.0 once
/// Qt Quick Test has created its engine.
class WorkflowSetup final: public QObject
{
    Q_OBJECT

  public Q_SLOTS:
    /// Called by Qt Quick Test for each engine it creates.
    void qmlEngineAvailable(QQmlEngine* /*engine*/)
    {
        if (_registered)
            return;
        _registered = true;
        qmlRegisterType<UiWatchdog>("DbtoolGuiWorkflow", 1, 0, "UiWatchdog");
        qmlRegisterSingletonType<WorkflowFixture>(
            "DbtoolGuiWorkflow", 1, 0, "WorkflowFixture", [](QQmlEngine* engine, QJSEngine* /*scriptEngine*/) {
                return new WorkflowFixture(engine);
            });
    }

  private:
    bool _registered = false;
};

} // namespace DbtoolGui::Tests
