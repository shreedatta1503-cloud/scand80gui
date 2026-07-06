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
import QGroundControl.Palette
import QGroundControl.ScreenTools

// Navigation Lights widget.
//
// Always visible (never gated on vehicle state). Acts as both a status indicator and a
// toggle for the navigation-lights output on AUX OUT 13 (ArduPilot SERVO13).
//
//   ON : AUX OUT 13 PWM == 750us (solid-on rail, light energised) -> dark-green button with a saffron border.
//   OFF: AUX OUT 13 PWM == 2100us (high rail == idle/TRIM, light de-energised) -> translucent grey button.
//
// The ON/OFF microsecond values MUST match what the physical light module expects for SOLID output.
// This module (per Mission Planner ch13out) uses 750us = solid-on, 2100us = off. Sending an
// intermediate value such as 1000us lands in the module's *blink* band, so keep these literal.
//
// The output is ACTIVE-LOW: the channel idles HIGH (SERVO13_TRIM ~2200us = OFF) and is pulled LOW to
// turn the lights ON. OFF is commanded on the SAME (high) rail as the idle/TRIM state, so a disarm,
// failsafe, safety-switch or competing-function reassertion of the channel default cannot make the
// output toggle (the old OFF=1000us sat on the opposite rail from TRIM=2200us and so blinked).
//
// The visible state is driven *only* by the PWM that Vehicle reports back through
// servoOutputsChanged (SERVO13 == index 12) — never by the act of issuing a command. Clicking
// commands the opposite state; the button repaints only once that new PWM is observed. There are
// no dialogs, toasts or modals anywhere in this widget (status, confirmation, or failure).
Rectangle {
    id:         root
    visible:    true   // Always visible; intentionally not bound to any vehicle state.

    implicitWidth:  mainColumn.implicitWidth  + (_margin * 2)
    implicitHeight: mainColumn.implicitHeight + (_margin * 2)
    radius:     ScreenTools.defaultFontPixelHeight / 2
    color:      Qt.rgba(qgcPal.window.r, qgcPal.window.g, qgcPal.window.b, 0.85)
    border.width: 1
    border.color: qgcPal.text

    Component.onCompleted: console.log("[NavLights] widget created; monitoring AUX OUT",
                                       root.navServo, "(SERVO13)")

    // ---- Configuration (channels are 1-based; ArduPilot AUX OUT n == SERVOn) ----
    property int navServo:      13      // AUX OUT 13 navigation-lights channel
    property int onPwmUs:       750     // Commanded/observed ON PWM (solid-on rail; 1000us = module blink band, avoid)
    property int offPwmUs:      2100    // Commanded/observed OFF PWM (high rail == idle/TRIM, light off)
    property int onThresholdUs: 1500    // PWM at/below which the lights are considered ON (active-low)
    property int confirmToleranceUs: 100 // |observed - target| within which a toggle is "confirmed"
    property int commandTimeoutMs:   3000 // Re-enable the button if no confirmation arrives

    // ---- Internal state ----
    property var  _activeVehicle:   QGroundControl.multiVehicleManager.activeVehicle
    property int  _navPwm:          -1      // Last observed AUX OUT 13 PWM (us); -1 == none yet
    property bool _commandPending:  false   // A toggle was sent, awaiting PWM confirmation
    property int  _pendingTarget:   0       // PWM value the pending toggle is waiting to observe

    // Derived appearance state, computed purely from the observed PWM. Unknown (-1) reads as OFF.
    // ACTIVE-LOW: at/below the threshold == ON (the >= 0 guard keeps unknown (-1) reading as OFF).
    readonly property bool _isOn: root._navPwm >= 0 && root._navPwm <= root.onThresholdUs

    readonly property real _margin: ScreenTools.defaultFontPixelWidth

    QGCPalette { id: qgcPal; colorGroupEnabled: enabled }

    // Consume clicks so they don't fall through to the map/video underneath.
    DeadMouseArea { anchors.fill: parent }

    // ---- AUX OUT 13 monitoring: event-driven, updates the indicator in real time ----
    // servoOutputsChanged is emitted by Vehicle from SERVO_OUTPUT_RAW and delivered to the GUI
    // thread; reading/mutating visual state directly here is therefore thread-safe. No polling.
    Connections {
        target:  root._activeVehicle
        enabled: root._activeVehicle

        function onServoOutputsChanged(servoValues) {
            var index = root.navServo - 1
            if (index < 0 || index >= servoValues.length) {
                return
            }
            var pwm = servoValues[index]
            if (pwm < 0) {
                return  // SERVO13 not present/invalid in this packet; keep the last known state.
            }

            if (pwm !== root._navPwm) {
                console.log("[NavLights] AUX OUT", root.navServo, "PWM", pwm,
                            "us (was", root._navPwm, ") ->", pwm <= root.onThresholdUs ? "ON" : "OFF")
                root._navPwm = pwm   // Repaints the button via the _isOn binding.
            }

            // Clear the click-protection lock once the hardware reaches the requested state.
            if (root._commandPending && Math.abs(pwm - root._pendingTarget) <= root.confirmToleranceUs) {
                console.log("[NavLights] toggle confirmed at", pwm, "us")
                root._commandPending = false
                commandTimeout.stop()
            }
        }
    }

    // When the active vehicle changes, drop any in-flight toggle lock and the stale reading so the
    // indicator falls back to its neutral OFF appearance until the new vehicle reports SERVO13.
    // The Connections block above rebinds to the new vehicle automatically.
    on_ActiveVehicleChanged: {
        root._commandPending = false
        commandTimeout.stop()
        root._navPwm = -1
    }

    // Click-protection / failure fallback: if no confirming PWM arrives in time, re-enable the
    // button and log it. The visual state is left exactly as the hardware last reported (no popup,
    // no optimistic update), satisfying the "UI always reflects actual output" requirement.
    Timer {
        id:         commandTimeout
        interval:   root.commandTimeoutMs
        repeat:     false
        onTriggered: {
            console.log("[NavLights] toggle to", root._pendingTarget,
                        "us not confirmed within", root.commandTimeoutMs, "ms; re-enabling button")
            root._commandPending = false
        }
    }

    ColumnLayout {
        id:                 mainColumn
        anchors.centerIn:   parent
        spacing:            ScreenTools.defaultFontPixelHeight / 2

        QGCLabel {
            Layout.alignment:   Qt.AlignHCenter
            text:               qsTr("Navigation Lights")
            font.bold:          true
        }

        // ---- Navigation Lights toggle/indicator button ----
        Rectangle {
            id:                     navButton
            Layout.alignment:       Qt.AlignHCenter
            Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 16
            Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 2.5
            radius:                 ScreenTools.defaultFontPixelHeight / 3

            // OFF: neutral grey, translucent. ON: dark green with a thin saffron border.
            color:                  root._isOn ? "#006400" : "#808080"
            opacity:                root._isOn ? 1.0 : 0.45
            border.width:           1
            border.color:           root._isOn ? "#FF9933" : qgcPal.text   // Saffron when ON

            QGCLabel {
                anchors.centerIn:   parent
                text:               root._isOn ? qsTr("Navigation Lights: ON")
                                               : qsTr("Navigation Lights: OFF")
                color:              "white"
                font.bold:          true
            }

            MouseArea {
                anchors.fill:   parent
                // Disabled while a toggle is pending (click protection) or with no vehicle.
                enabled:        root._activeVehicle && !root._commandPending
                onClicked: {
                    var target = root._isOn ? root.offPwmUs : root.onPwmUs
                    console.log("[NavLights] toggle pressed -> commanding AUX OUT", root.navServo,
                                "to", target, "us")
                    root._pendingTarget  = target
                    root._commandPending = true
                    // Pass the literal target PWM (single source of truth); active-low mapping and
                    // range validation live in Vehicle::sendNavigationLights().
                    root._activeVehicle.sendNavigationLights(target)
                    commandTimeout.restart()
                }
            }
        }
    }
}
