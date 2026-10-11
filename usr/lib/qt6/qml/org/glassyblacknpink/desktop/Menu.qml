// Qt Quick pop-ups are drawn inside their window, where KWin's glass
// cannot reach them, so they get a solid black background.
import QtQuick
import org.kde.desktop as Desktop

Desktop.Menu {
    Component.onCompleted: background.color = "black"
}
