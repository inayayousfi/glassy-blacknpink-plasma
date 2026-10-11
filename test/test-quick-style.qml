// Run with the qml tool under the theme's color scheme, whose window colors
// are transparent: the Qt Quick style must give in-window pop-ups a solid
// black background and leave every other control to KDE's style.
import QtQuick
import QtQuick.Controls

Window {
    width: 400
    height: 300
    visible: true

    Menu { id: menu; MenuItem { text: "Open" } }
    ComboBox { id: combo; model: ["One", "Two"] }
    Popup { id: popup; width: 100; height: 50 }
    Dialog { id: dialog; width: 100; height: 50 }
    Button { id: button; text: "Button" }

    function solidBlack(color) {
        return color.r === 0 && color.g === 0 && color.b === 0 && color.a === 1;
    }

    // Each pop-up takes its colors from the window once it opens.
    Timer {
        interval: 500
        running: true
        onTriggered: {
            menu.open();
            combo.popup.open();
            popup.open();
            dialog.open();
            Qt.callLater(check);
        }
    }

    function check() {
        const failures = [];
        for (const [name, background] of [["Menu", menu.background], ["ComboBox dropdown", combo.popup.background],
                                          ["Popup", popup.background], ["Dialog", dialog.background]]) {
            if (!background || !solidBlack(background.color))
                failures.push(name + " background is " + (background ? background.color + " with alpha " + background.color.a : "missing"));
        }
        if (!String(button.background).includes("StyleItem"))
            failures.push("Button did not come from KDE's style: its background is " + button.background);
        for (const failure of failures)
            console.error(failure);
        Qt.exit(failures.length ? 1 : 0);
    }
}
