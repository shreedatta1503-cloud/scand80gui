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
import QtQuick.Dialogs
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

// Payload Drop widget.
//
// Hidden by default; appears when RC Channel 9 activity is detected. The pin must
// first be removed (drives AUX OUT 9 / SERVO9) and confirmed via AUX OUT 10
// (SERVO10) feedback before the DROP action (AUX OUT 11 / SERVO11) is enabled.
// After a successful drop the widget resets and hides itself.
Rectangle {
    id:         root
    visible:    _widgetVisible

    // Trace visibility transitions (widget shown/hidden) on the UI thread.
    onVisibleChanged: console.log("[PayloadDrop] widget visibility ->", visible,
                                  "(RC9 =", root._rc9Pwm, "us )")

    Component.onCompleted: console.log("[PayloadDrop] widget created; waiting for RC Channel",
                                       root.rcTriggerChannel, "activity")
    implicitWidth:  mainColumn.implicitWidth  + (_margin * 2)
    implicitHeight: mainColumn.implicitHeight + (_margin * 2)
    radius:     ScreenTools.defaultFontPixelHeight / 2
    color:      Qt.rgba(qgcPal.window.r, qgcPal.window.g, qgcPal.window.b, 0.85)
    border.width: 1
    border.color: qgcPal.text

    // ---- Configuration (channels are 1-based; ArduPilot AUX OUT n == SERVOn) ----
    property int    rcTriggerChannel:       9       // RC Channel that reveals the widget
    property int    rcTriggerThresholdUs:   1500    // PWM above which Ch9 is considered "active"
    property int    pinFeedbackServo:       10      // AUX OUT 10 limit-switch feedback channel
    property int    pinFeedbackThresholdUs: 1500    // PWM above which the pin is considered removed

    // ---- Internal state ----
    property var  _activeVehicle:       QGroundControl.multiVehicleManager.activeVehicle
    property bool _widgetVisible:       false       // Revealed by RC Ch9 activity
    property bool _pinReleaseRequested: false       // Remove Pin confirmed, awaiting AUX10 feedback
    property bool _pinRemoved:          false       // AUX10 feedback received -> DROP enabled
    property bool _dropCompleted:       false       // DROP command sent successfully
    property int  _rc9Pwm:              -1          // Last seen RC Ch9 PWM (us); -1 == none yet
    property bool _rc9AbsentLogged:     false       // Edge-trigger for the "RC9 absent" diagnostic

    readonly property real _margin: ScreenTools.defaultFontPixelWidth

    QGCPalette { id: qgcPal; colorGroupEnabled: enabled }

    // Consume clicks so they don't fall through to the map/video underneath.
    DeadMouseArea { anchors.fill: parent }

    // Reset to the initial (hidden) state. _rc9Pwm is intentionally preserved so the widget
    // only re-appears on a genuine new RC9 change, not on the next identical RC packet.
    function resetState() {
        console.log("[PayloadDrop] resetState -> hiding widget, clearing pin/drop state")
        _pinReleaseRequested = false
        _pinRemoved          = false
        _dropCompleted       = false
        _widgetVisible       = false
    }

    // ---- RC Channel 9 monitoring: reveal the widget on activity ----
    Connections {
        target: root._activeVehicle
        enabled: root._activeVehicle

        // Ch9 arrives via the dedicated rc9TriggerChanged signal, which Vehicle reads straight from
        // the raw RC_CHANNELS values (before the contiguous-channel truncation that can otherwise
        // hide Ch9 from rcChannelsRawChanged). pwm == -1 means Ch9 is not present in the RC stream;
        // in that case Vehicle has already shown a user-visible message explaining why.
        function onRc9TriggerChanged(pwm) {
            if (pwm < 0) {
                // Ch9 dropped out of the RC stream. Keep the last reading so the readout shows the
                // staleness rather than flicker; the widget simply won't be (re)revealed.
                root._rc9AbsentLogged = true
                return
            }
            root._rc9AbsentLogged = false

            // ---- Change detection ----
            // Reveal on ANY change in the Ch9 PWM value (not on an absolute level/threshold),
            // including the very first reading after startup (previous value == -1), so the widget
            // appears on Ch9 activity even when the value never crosses rcTriggerThresholdUs.
            if (pwm === root._rc9Pwm) {
                return
            }
            console.log("[PayloadDrop] RC9:", pwm, "us (was", root._rc9Pwm, ") -> change detected")
            root._rc9Pwm = pwm

            // Any Ch9 change reveals the widget, unless a drop just completed and is awaiting its
            // acknowledgement dialog. rc9TriggerChanged is queued to the GUI thread, so mutating
            // visual state directly here is thread-safe.
            if (!root._dropCompleted && !root._widgetVisible) {
                console.log("[PayloadDrop] RC9 activity -> revealing widget")
                root._widgetVisible = true
            }
        }

        // ---- AUX OUT 10 feedback: confirm pin removal, enable DROP ----
        function onServoOutputsChanged(servoValues) {
            if (!root._pinReleaseRequested || root._pinRemoved) {
                return
            }
            var index = root.pinFeedbackServo - 1
            if (index < 0 || index >= servoValues.length) {
                return
            }
            var pwm = servoValues[index]
            if (pwm >= 0 && pwm >= root.pinFeedbackThresholdUs) {
                console.log("[PayloadDrop] AUX OUT", root.pinFeedbackServo, "feedback", pwm,
                            "us -> pin removed, enabling DROP")
                root._pinRemoved = true
                pinRemovedDialog.open()
            }
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
            text:                   root._rc9Pwm < 0
                                        ? qsTr("RC%1: --").arg(root.rcTriggerChannel)
                                        : qsTr("RC%1: %2 µs (%3)")
                                            .arg(root.rcTriggerChannel)
                                            .arg(root._rc9Pwm)
                                            .arg(root._rc9Pwm > root.rcTriggerThresholdUs ? qsTr("high") : qsTr("low"))
        }

        // ---- Remove Pin button: rectangular, saffron, thin red border ----
        Rectangle {
            id:                     removePinButton
            Layout.alignment:       Qt.AlignHCenter
            Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 16
            Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 2.5
            radius:                 ScreenTools.defaultFontPixelHeight / 3
            color:                  "#F4C430"   // Saffron
            border.width:           1
            border.color:           "red"
            opacity:                root._pinRemoved ? 0.4 : 1.0

            QGCLabel {
                anchors.centerIn:   parent
                text:               qsTr("Remove Pin")
                color:              "black"
                font.bold:          true
            }

            MouseArea {
                anchors.fill:   parent
                enabled:        !root._pinRemoved && !root._pinReleaseRequested && root._activeVehicle
                onClicked:      confirmRemovePinDialog.open()
            }
        }

        // ---- DROP button: circular, red (disabled) -> sky blue (dropped) ----
        Rectangle {
            id:                     dropButton
            Layout.alignment:       Qt.AlignHCenter
            Layout.preferredWidth:  ScreenTools.defaultFontPixelHeight * 5
            Layout.preferredHeight: Layout.preferredWidth
            radius:                 width / 2
            color:                  root._dropCompleted ? "skyblue" : "red"
            border.width:           1
            border.color:           root._dropCompleted ? "red" : "white"
            opacity:                _dropEnabled ? 1.0 : 0.45

            readonly property bool _dropEnabled: root._pinRemoved && !root._dropCompleted && root._activeVehicle

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
                onClicked: {
                    console.log("[PayloadDrop] DROP pressed -> sendPayloadDrop() (AUX OUT 11)")
                    root._activeVehicle.sendPayloadDrop()
                    root._dropCompleted = true
                    payloadDroppedDialog.open()
                }
            }
        }
    }

    // ---- Dialogs ----

    // Remove Pin confirmation (Step 1/2).
    MessageDialog {
        id:         confirmRemovePinDialog
        title:      qsTr("Remove Pin")
        text:       qsTr("Are you sure?")
        buttons:    MessageDialog.Yes | MessageDialog.No

        onButtonClicked: function(button, role) {
            if (button === MessageDialog.Yes) {
                if (root._activeVehicle) {
                    console.log("[PayloadDrop] Remove Pin confirmed -> sendPayloadPinRelease() (AUX OUT 9)")
                    root._activeVehicle.sendPayloadPinRelease()  // AUX OUT 9
                    root._pinReleaseRequested = true
                }
            }
            confirmRemovePinDialog.close()
        }
    }

    // "Pin Removed" notification once AUX OUT 10 feedback confirms actuation.
    MessageDialog {
        id:         pinRemovedDialog
        title:      qsTr("Pin Removed")
        text:       qsTr("Pin Removed")
        buttons:    MessageDialog.Ok
        onButtonClicked: pinRemovedDialog.close()
    }

    // "Payload Dropped" notification; on dismissal the widget resets and hides.
    MessageDialog {
        id:         payloadDroppedDialog
        title:      qsTr("Payload Dropped")
        text:       qsTr("Payload Dropped")
        buttons:    MessageDialog.Ok
        onButtonClicked: {
            payloadDroppedDialog.close()
            root.resetState()
        }
    }
}
