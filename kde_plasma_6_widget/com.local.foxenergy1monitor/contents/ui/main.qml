import QtQuick
import QtQuick.Layouts
import org.kde.plasma.plasmoid
import org.kde.plasma.components as PlasmaComponents

PlasmoidItem {
    id: root

    // Widget Data Properties
    property string statusText: "Loading..."
    property string voltage: "0.0"
    property string current: "0.0"
    property real powerActive: 0.0
    property string powerReactive: "0.0"
    property string frequency: "0.0"
    property string powerFactor: "0.0"

    // Set preferred sizes for Desktop mode
    Layout.minimumWidth: 200
    Layout.minimumHeight: 200

    // Fetch data every 5 seconds
    Timer {
        id: dataTimer
        interval: 5000 
        running: true
        repeat: true
        triggeredOnStart: true
        onTriggered: fetchData()
    }

    function fetchData() {
        var xhr = new XMLHttpRequest();
        xhr.open("GET", "http://192.168.0.101/0000/get_current_parameters", true);
        xhr.onreadystatechange = function() {
            if (xhr.readyState === XMLHttpRequest.DONE) {
                if (xhr.status === 200) {
                    try {
                        var data = JSON.parse(xhr.responseText);
                        if (data.status === "ok") {
                            root.statusText = "Online";
                            root.voltage = data.voltage;
                            root.current = data.current;
                            root.powerActive = parseFloat(data.power_active);
                            root.powerReactive = data.power_reactive;
                            root.frequency = data.frequency;
                            root.powerFactor = data.power_factor;
                        } else {
                            root.statusText = "Error: " + data.status;
                        }
                    } catch(e) {
                        root.statusText = "JSON Parse Error";
                    }
                } else {
                    root.statusText = "Offline";
                }
            }
        }
        xhr.send();
    }

    // Logic to format watts to kW and use a comma
    function formatPowerText(watts) {
        if (watts >= 1000) {
            return (watts / 1000).toFixed(1).replace(".", ",") + " kW";
        }
        return Math.round(watts) + " W";
    }

    // Logic for color coding
    function getPowerColor(watts) {
        if (watts >= 4000) return "#ff4a4a"; // Red
        if (watts >= 2500) return "#ffb000"; // Orange
        return "#00cc66";                    // Green
    }

    // --- TASKBAR MODE (Compact) ---
    // Notice how it's no longer Plasmoid.compactRepresentation
    compactRepresentation: Item {
        Layout.minimumWidth: compactLabel.implicitWidth + 10
        Layout.minimumHeight: compactLabel.implicitHeight

        PlasmaComponents.Label {
            id: compactLabel
            anchors.centerIn: parent
            text: root.formatPowerText(root.powerActive)
            color: root.getPowerColor(root.powerActive)
            font.bold: true
            font.pixelSize: 14
        }

        // Allow clicking the taskbar text to open the popup window
        MouseArea {
            anchors.fill: parent
            onClicked: root.expanded = !root.expanded
        }
    }

    // --- DESKTOP / POPUP MODE (Full) ---
    // Notice how it's no longer Plasmoid.fullRepresentation
    fullRepresentation: Item {
        Layout.minimumWidth: 250
        Layout.minimumHeight: 220

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 10

            // Header / Status
            PlasmaComponents.Label {
                text: "Energy Monitor (" + root.statusText + ")"
                font.bold: true
                Layout.alignment: Qt.AlignHCenter
            }

            // Data Grid
            GridLayout {
                columns: 2
                columnSpacing: 15
                rowSpacing: 5
                Layout.fillWidth: true

                // Active Power (Colored)
                PlasmaComponents.Label { text: "Active Power:" }
                PlasmaComponents.Label {
                    text: root.formatPowerText(root.powerActive)
                    color: root.getPowerColor(root.powerActive)
                    font.bold: true
                }

                // Other stats
                PlasmaComponents.Label { text: "Voltage:" }
                PlasmaComponents.Label { text: root.voltage + " V" }

                PlasmaComponents.Label { text: "Current:" }
                PlasmaComponents.Label { text: root.current + " A" }

                PlasmaComponents.Label { text: "Reactive Power:" }
                PlasmaComponents.Label { text: root.powerReactive + " var" }

                PlasmaComponents.Label { text: "Frequency:" }
                PlasmaComponents.Label { text: root.frequency + " Hz" }

                PlasmaComponents.Label { text: "Power Factor:" }
                PlasmaComponents.Label { text: root.powerFactor }
            }
            
            Item { Layout.fillHeight: true } // Spacer
        }
    }
}
