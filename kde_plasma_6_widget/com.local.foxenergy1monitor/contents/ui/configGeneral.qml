import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ScrollView {
    id: scrollView
    
    // Map the UI elements to the settings in main.xml
    property alias cfg_apiUrl: apiUrlField.text
    property alias cfg_textSize: textSizeField.value
    property alias cfg_desktopTextSize: desktopTextSizeField.value
    property alias cfg_thresholdOrange: orangeField.value
    property alias cfg_thresholdRed: redField.value
    
    Kirigami.FormLayout {
        id: page

    // Spacer Item
    Item {
        Layout.preferredHeight: Kirigami.Units.gridUnit // Adds roughly one line of space
    }

    TextField {
        id: apiUrlField
        Kirigami.FormData.label: "API URL:"
        Layout.fillWidth: true
        Layout.minimumWidth: Kirigami.Units.gridUnit * 28
    }

    SpinBox {
        id: textSizeField
        Kirigami.FormData.label: "Taskbar Text Size:"
        from: 8
        to: 48
    }

    SpinBox {
        id: desktopTextSizeField
        Kirigami.FormData.label: "Desktop Text Size:"
        from: 8
        to: 48
    }

    SpinBox {
        id: orangeField
        Kirigami.FormData.label: "Orange Threshold (W):"
        from: 0
        to: 20000
        stepSize: 100
    }

    SpinBox {
        id: redField
        Kirigami.FormData.label: "Red Threshold (W):"
        from: 0
        to: 20000
        stepSize: 100
    }
    }
}
