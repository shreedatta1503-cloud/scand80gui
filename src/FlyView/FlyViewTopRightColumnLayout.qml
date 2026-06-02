import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlyView
import QGroundControl.FlightMap

ColumnLayout {
    spacing: ScreenTools.defaultFontPixelHeight / 2

    TerrainProgress {
        Layout.fillWidth: true
    }

    // We use a Loader to load the photoVideoControlComponent only when we have an active vehicle and a camera manager.
    // This make it easier to implement PhotoVideoControl without having to check for the mavlink camera
    // to be null all over the place
    Loader {
        id:                 photoVideoControlLoader
        Layout.alignment:   Qt.AlignRight
        sourceComponent:    globals.activeVehicle && globals.activeVehicle.cameraManager ? photoVideoControlComponent : undefined

        property real rightEdgeCenterInset: visible ? parent.width - x : 0

        Component {
            id: photoVideoControlComponent

            PhotoVideoControl {
            }
        }
    }

    // Navigation Lights widget. Always visible (never gated on vehicle state) and placed
    // directly above the Payload Drop widget. Acts as a status indicator + toggle for AUX OUT 13.
    NavigationLightsWidget {
        Layout.alignment: Qt.AlignRight
    }

    // Payload Drop widget. Self-managing: hidden until RC Channel 9 activity is
    // detected, hides again after a successful drop. Loaded only when a vehicle is connected.
    Loader {
        id:                 payloadDropLoader
        Layout.alignment:   Qt.AlignRight
        sourceComponent:    globals.activeVehicle ? payloadDropComponent : undefined

        Component {
            id: payloadDropComponent

            PayloadDropWidget {
            }
        }
    }
}
