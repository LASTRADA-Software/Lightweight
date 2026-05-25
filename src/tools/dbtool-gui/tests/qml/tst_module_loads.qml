// SPDX-License-Identifier: Apache-2.0
//
// Smoke test for the `Lightweight.Migrations` QML module. Verifies the
// module imports without error and the `AppController` singleton exposes
// the properties the rest of the UI binds against. A regression here means
// the QML plugin registration broke — which would make the live UI fail to
// load at all.

import QtQuick
import QtTest
import Lightweight.Migrations

TestCase {
    name: "ModuleLoads"

    function test_app_controller_singleton_is_reachable() {
        verify(AppController !== null)
    }

    function test_app_controller_properties_have_expected_types() {
        compare(typeof AppController.connected, "boolean")
        compare(typeof AppController.viewMode, "string")
        compare(typeof AppController.migrationCount, "number")
        compare(typeof AppController.selectionCount, "number")
    }

    function test_disconnected_at_startup_in_test_runner() {
        verify(!AppController.connected)
    }

    function test_models_are_exposed() {
        verify(AppController.profiles !== null)
        verify(AppController.migrations !== null)
        verify(AppController.releases !== null)
        verify(AppController.odbcDataSources !== null)
    }
}
