/****************************************************************************
 *
 * (c) 2024 QGroundControl Development Team. All rights reserved.
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

// Payload Drop widget.
//
// Thin presentation layer over the C++ PayloadDropController backend. The controller owns the
// authoritative workflow state on a dedicated worker thread (RC9 monitoring, pin/drop sequence,
// AUX OUT 10 feedback), so this widget only *renders* that state and forwards button presses.
//
// Visibility is driven solely by RC Channel 9: high (2000us) shows the widget, low (1000us) hides
// it. Hiding is non-destructive — the controller keeps the full workflow state while hidden, so
// when RC9 returns to 2000us the widget reappears exactly where it left off (pin/drop progress,
// button colours, RC readout all preserved). There is no completion dialog; a successful DROP
// completes silently.
Rectangle {
    id:         root
    visible:    controller.widgetVisible

    // Trace visibility transitions (widget shown/hidden) on the UI thread.
    onVisibleChanged: console.log("[PayloadDrop] widget visibility ->", visible,
                                  "(RC9 =", controller.rc9Pwm, "us )")

    Component.onCompleted: console.log("[PayloadDrop] widget created; visibility driven by RC Channel",
                                       root.rcTriggerChannel, "(2000us shows / 1000us hides)")
    implicitWidth:  mainColumn.implicitWidth  + (_margin * 2)
    implicitHeight: mainColumn.implicitHeight + (_margin * 2)
    radius:     ScreenTools.defaultFontPixelHeight / 2
    color:      Qt.rgba(qgcPal.window.r, qgcPal.window.g, qgcPal.window.b, 0.85)
    border.width: 1
    border.color: qgcPal.text

    // ---- Configuration (display only; channel mapping lives in the controller) ----
    property int    rcTriggerChannel:       9       // RC Channel that reveals the widget (display label)
    property int    rcTriggerThresholdUs:   1500    // PWM above which Ch9 reads "high" (display label)

    readonly property real _margin: ScreenTools.defaultFontPixelWidth

    QGCPalette { id: qgcPal; colorGroupEnabled: enabled }

    // ---- Backend: owns workflow state + RC9/servo monitoring on a worker thread ----
    // The controller persists for the lifetime of this widget. Toggling root.visible (above) does
    // NOT touch the controller, so all workflow state survives every hide/show cycle.
    PayloadDropController {
        id:      controller
        vehicle: QGroundControl.multiVehicleManager.activeVehicle
    }

    // Consume clicks so they don't fall through to the map/video underneath.
    DeadMouseArea { anchors.fill: parent }

    // ---- Modal DROP confirmation dialog ----
    // Parented to the application overlay so it is centred over the whole window and is genuinely
    // modal (blocks all interaction behind it) while remaining non-blocking for the UI thread.
    // A single instance is reused; the DROP button's `visible` guard prevents stacking dialogs.
    Dialog {
        id:                 confirmDropDialog
        parent:             Overlay.overlay
        anchors.centerIn:   parent
        modal:              true
        closePolicy:        Popup.NoAutoClose   // Must pick YES or NO — no click-outside / Esc dismissal.
        title:              qsTr("Payload Dropping")
        padding:            ScreenTools.defaultFontPixelHeight

        contentItem: QGCLabel {
            text:               qsTr("Are you sure?")
            wrapMode:           Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
        }

        footer: DialogButtonBox {
            QGCButton {
                text:                       qsTr("YES")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            QGCButton {
                text:                       qsTr("NO")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }

        // YES: close and continue the EXISTING drop execution sequence exactly as before.
        onAccepted: {
            console.log("[PayloadDrop] DROP confirmed -> controller.requestDrop() (AUX OUT 11)")
            controller.requestDrop()
        }
        // NO: close immediately, command nothing, return to the previous state and await the next press.
        onRejected: {
            console.log("[PayloadDrop] DROP cancelled by operator")
        }
    }

    ColumnLayout {
        id:                 mainColumn
        anchors.centerIn:   parent
        spacing:            ScreenTools.defaultFontPixelHeight / 2

        QGCLabel {
            Layout.alignment:       Qt.AlignHCenter
            text:                   qsTr("Payload Drop")
            font.bold:              true
        }

        // ---- Live RC Channel 9 readout: updated in real time on every RC9 change ----
        QGCLabel {
            Layout.alignment:       Qt.AlignHCenter
            font.pointSize:         ScreenTools.smallFontPointSize
            text:                   controller.rc9Pwm < 0
                                        ? qsTr("RC%1: --").arg(root.rcTriggerChannel)
                                        : qsTr("RC%1: %2 µs (%3)")
                                            .arg(root.rcTriggerChannel)
                                            .arg(controller.rc9Pwm)
                                            .arg(controller.rc9Pwm > root.rcTriggerThresholdUs ? qsTr("high") : qsTr("low"))
        }

        // ---- Remove Pin button: saffron, thin red border. Turns neon green (still red
        //      border) once AUX OUT 10 acknowledges pin removal — this colour change is the
        //      only acknowledgement feedback (no dialog/toast/modal). ----
        Rectangle {
            id:                     removePinButton
            Layout.alignment:       Qt.AlignHCenter
            Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 16
            Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 2.5
            radius:                 ScreenTools.defaultFontPixelHeight / 3
            color:                  controller.pinRemoved ? "#39FF14" : "#F4C430"  // Neon green once ack'd, else saffron
            border.width:           1
            border.color:           "red"
            opacity:                1.0

            QGCLabel {
                anchors.centerIn:   parent
                text:               qsTr("Remove Pin")
                color:              "black"
                font.bold:          true
            }

            MouseArea {
                anchors.fill:   parent
                enabled:        !controller.pinRemoved && !controller.pinReleaseRequested && controller.vehicle
                // Execute the pin release immediately on click — no confirmation dialog. The
                // controller fires the non-blocking AUX OUT 9 command and records the request on its
                // worker thread; ACK is handled asynchronously, so the GUI thread is never blocked.
                onClicked: {
                    console.log("[PayloadDrop] Remove Pin pressed -> controller.requestPinRelease() (AUX OUT 9)")
                    controller.requestPinRelease()
                }
            }
        }

        // ---- DROP button: circular, red (disabled) -> sky blue (dropped) ----
        Rectangle {
            id:                     dropButton
            Layout.alignment:       Qt.AlignHCenter
            Layout.preferredWidth:  ScreenTools.defaultFontPixelHeight * 5
            Layout.preferredHeight: Layout.preferredWidth
            radius:                 width / 2
            color:                  controller.dropCompleted ? "skyblue" : "red"
            border.width:           1
            border.color:           controller.dropCompleted ? "red" : "white"
            opacity:                _dropEnabled ? 1.0 : 0.45

            readonly property bool _dropEnabled: controller.pinRemoved && !controller.dropCompleted && controller.vehicle

            QGCLabel {
                anchors.centerIn:   parent
                text:               qsTr("DROP")
                color:              "white"
                font.bold:          true
                font.pointSize:     ScreenTools.largeFontPointSize
            }

            MouseArea {
                anchors.fill:   parent
                enabled:        dropButton._dropEnabled
                // A single press raises a modal confirmation dialog rather than dropping directly.
                // The actual release is only issued if the operator confirms (dialog onAccepted).
                onClicked: {
                    if (confirmDropDialog.visible) {
                        return  // A confirmation is already open — never stack multiple dialogs.
                    }
                    console.log("[PayloadDrop] DROP pressed -> showing confirmation dialog")
                    confirmDropDialog.open()
                }
            }
        }
    }
}
