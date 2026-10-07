// SPDX-License-Identifier: Apache-2.0
//
// Kit text field (`k-ctl`): 32 px, hairline field border, brand border and
// tint on focus, the read-only palette when `readOnly`, error border when
// `invalid`. An optional leading glyph sits inside the field.
//
// Extracted from BackupsPage.qml so every form (the restore confirmation, the profile
// dialog) uses the same field.

import QtQuick
import QtQuick.Controls
import Lightweight.Migrations

TextField {
    id: field
    property bool mono: false
    property bool invalid: false
    property string leadingGlyph: ""

    implicitHeight: Theme.ctlMd
    font: mono ? Theme.monoFont(12) : Qt.font({ family: Theme.fontFamily, pixelSize: Theme.sizeBody })
    leftPadding: leadingGlyph !== "" ? 30 : 10
    rightPadding: 10
    topPadding: 0
    bottomPadding: 0
    verticalAlignment: TextInput.AlignVCenter
    color: readOnly ? Theme.clrFieldRoText : Theme.clrOnSurface
    placeholderTextColor: Theme.clrOnSurfaceFaint
    selectionColor: Theme.clrPrimarySoftBorder
    selectedTextColor: Theme.clrOnSurface
    selectByMouse: true

    background: Rectangle {
        radius: Theme.r1
        color: field.readOnly ? Theme.clrFieldRoBg
             : (field.activeFocus ? Theme.clrFieldFocusBg : Theme.clrCard)
        border.color: field.invalid ? Theme.clrError
                    : field.activeFocus ? Theme.clrPrimary
                    : (field.readOnly ? Theme.clrFieldRoBorder : Theme.clrFieldBorder)

        Glyph {
            visible: field.leadingGlyph !== ""
            anchors.left: parent.left
            anchors.leftMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            name: field.leadingGlyph
            size: 14
            color: Theme.clrOnSurfaceSubtle
            knockout: parent.color
        }
    }
}
