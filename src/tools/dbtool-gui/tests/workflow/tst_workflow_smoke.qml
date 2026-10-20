// SPDX-License-Identifier: Apache-2.0
//
// The workflow harness itself: the real Main window opens, its controls are found by objectName,
// and the watchdog measures from the first frame on.

import QtQuick
import QtTest
import DbtoolGuiWorkflow

WorkflowTestCase {
    name: "WorkflowSmoke"

    function test_main_window_opens_under_the_watchdog() {
        openMain()
        findRequired("connectButton")
        tryVerify(() => watchdog.maxGapMs > 0, 5000, "the watchdog must measure heartbeats after the first frame")
    }
}
