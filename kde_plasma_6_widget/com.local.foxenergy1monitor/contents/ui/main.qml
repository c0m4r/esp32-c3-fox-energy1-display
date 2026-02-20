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
    
    Layout.minimumWidth: 200
    Layout.minimumHeight: 200

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
        // Use the user's configured URL instead of hardcoding it
        xhr.open("GET", Plasmoid.configuration.apiUrl, true);
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

    function formatPowerText(watts) {
        if (watts >= 1000) {
            return (watts / 1000).toFixed(1).replace(".", ",") + " kW";
        }
        return Math.round(watts) + " W";
    }

    function getPowerColor(watts) {
        // Use user's configured thresholds
        if (watts >= Plasmoid.configuration.thresholdRed) return "#ff4a4a";
        if (watts >= Plasmoid.configuration.thresholdOrange) return "#ffb000";
        return "#00cc66"; 
    }

    // TASKBAR MODE (Compact)
    compactRepresentation: Item {
        Layout.minimumWidth: compactLabel.implicitWidth + 10
        Layout.minimumHeight: compactLabel.implicitHeight

        PlasmaComponents.Label {
            id: compactLabel
            anchors.centerIn: parent
            text: root.formatPowerText(root.powerActive)
            color: root.getPowerColor(root.powerActive)
            font.bold: true
            // Use user's configured text size
            font.pixelSize: Plasmoid.configuration.textSize
        }

        MouseArea {
            anchors.fill: parent
            onClicked: root.expanded = !root.expanded
        }
    }

    // DESKTOP MODE (Full)
    fullRepresentation: Item {
        Layout.minimumWidth: 250
        Layout.minimumHeight: 220

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 10

            PlasmaComponents.Label {
                text: "Energy Monitor (" + root.statusText + ")"
                font.bold: true
                font.pixelSize: Plasmoid.configuration.desktopTextSize
                Layout.alignment: Qt.AlignHCenter
            }

            GridLayout {
                columns: 2
                columnSpacing: 15
                rowSpacing: 5
                Layout.fillWidth: true

                PlasmaComponents.Label { 
                    text: "Active Power:"
                    font.pixelSize: Plasmoid.configuration.desktopTextSize
                }
                PlasmaComponents.Label {
                    text: root.formatPowerText(root.powerActive)
                    color: root.getPowerColor(root.powerActive)
                    font.bold: true
                    font.pixelSize: Plasmoid.configuration.desktopTextSize
                }

                PlasmaComponents.Label { 
                    text: "Voltage:"
                    font.pixelSize: Plasmoid.configuration.desktopTextSize
                }
                PlasmaComponents.Label { 
                    text: root.voltage + " V"
                    font.pixelSize: Plasmoid.configuration.desktopTextSize
                }

                PlasmaComponents.Label { 
                    text: "Current:"
                    font.pixelSize: Plasmoid.configuration.desktopTextSize
                }
                PlasmaComponents.Label { 
                    text: root.current + " A"
                    font.pixelSize: Plasmoid.configuration.desktopTextSize
                }

                PlasmaComponents.Label { 
                    text: "Reactive Power:"
                    font.pixelSize: Plasmoid.configuration.desktopTextSize
                }
                PlasmaComponents.Label { 
                    text: root.powerReactive + " var"
                    font.pixelSize: Plasmoid.configuration.desktopTextSize
                }

                PlasmaComponents.Label { 
                    text: "Frequency:"
                    font.pixelSize: Plasmoid.configuration.desktopTextSize
                }
                PlasmaComponents.Label { 
                    text: root.frequency + " Hz"
                    font.pixelSize: Plasmoid.configuration.desktopTextSize
                }

                PlasmaComponents.Label { 
                    text: "Power Factor:"
                    font.pixelSize: Plasmoid.configuration.desktopTextSize
                }
                PlasmaComponents.Label { 
                    text: root.powerFactor
                    font.pixelSize: Plasmoid.configuration.desktopTextSize
                }
            }
            Item { Layout.fillHeight: true }
        }
    }
}
