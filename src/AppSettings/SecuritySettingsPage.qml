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

/// Security Settings page — change the application-lock password.
///
/// Validation and storage are delegated to AppLockManager (QGroundControl.appLockManager):
/// the current password must verify against the stored salted hash, the new password and its
/// confirmation must match and be non-empty. Only the salted hash is persisted, never plain text.
Item {
    id:             root
    implicitHeight: mainLayout.implicitHeight

    property var  _lock:       QGroundControl.appLockManager
    readonly property real _fieldWidth: ScreenTools.defaultFontPixelWidth * 24

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    function _changePassword() {
        // Returns "" on success, otherwise a ready-to-display error message.
        var err = root._lock.changePassword(currentField.text, newField.text, confirmField.text)
        if (err === "") {
            statusLabel.text     = qsTr("Password Updated Successfully")
            statusLabel._isError = false
            currentField.text    = ""
            newField.text        = ""
            confirmField.text    = ""
        } else {
            statusLabel.text     = err
            statusLabel._isError = true
        }
        statusLabel.visible = true
    }

    ColumnLayout {
        id:    mainLayout
        width: parent.width

        SettingsGroupLayout {
            Layout.fillWidth:   true
            heading:            qsTr("Security Settings")
            headingDescription: qsTr("Change the password required to unlock QGroundControl on startup.")

            RowLayout {
                Layout.fillWidth: true
                QGCLabel { text: qsTr("Current Password"); Layout.fillWidth: true }
                QGCTextField {
                    id:                     currentField
                    echoMode:               TextInput.Password
                    Layout.preferredWidth:  root._fieldWidth
                }
            }

            RowLayout {
                Layout.fillWidth: true
                QGCLabel { text: qsTr("New Password"); Layout.fillWidth: true }
                QGCTextField {
                    id:                     newField
                    echoMode:               TextInput.Password
                    Layout.preferredWidth:  root._fieldWidth
                }
            }

            RowLayout {
                Layout.fillWidth: true
                QGCLabel { text: qsTr("Confirm New Password"); Layout.fillWidth: true }
                QGCTextField {
                    id:                     confirmField
                    echoMode:               TextInput.Password
                    Layout.preferredWidth:  root._fieldWidth
                    onAccepted:             root._changePassword()
                }
            }

            RowLayout {
                Layout.fillWidth: true

                QGCLabel {
                    id:                 statusLabel
                    Layout.fillWidth:   true
                    visible:            false
                    wrapMode:           Text.WordWrap
                    property bool       _isError: false
                    color:              _isError ? qgcPal.warningText : qgcPal.colorGreen
                }

                QGCButton {
                    text:       qsTr("Change Password")
                    onClicked:  root._changePassword()
                }
            }
        }
    }
}
