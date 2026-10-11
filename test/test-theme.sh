#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
test_root="$(mktemp -d)"
trap 'rm -rf "$test_root"' EXIT
fail() {
    printf '%s\n' "$*" >&2
    exit 1
}
plasma_theme_root="$repo_root/usr/share/plasma/desktoptheme/glassy-blacknpink"
plasma_global_theme_root="$repo_root/usr/share/plasma/look-and-feel/glassy-blacknpink.desktop"
cursor_theme_root="$repo_root/usr/share/icons/glassy-blacknpink"
layout_script="$plasma_global_theme_root/contents/layouts/org.kde.plasma.desktop-layout.js"

grep -Fq 'panel.lengthMode = "fit";' "$layout_script" \
    || fail "The dock does not fit its content"
grep -Fq 'tasks.writeConfig("fill", false);' "$layout_script" \
    || fail "The task manager still fills the dock"
node -e 'JSON.parse(require("node:fs").readFileSync(process.argv[1], "utf8"))' \
    "$repo_root/usr/share/kwin/scripts/glassy-blacknpink-panels/metadata.json"
plasma_plasmoid_root="$repo_root/usr/share/plasma/plasmoids"
for widget in overview session power island; do
    node -e 'const metadata = JSON.parse(require("node:fs").readFileSync(process.argv[1], "utf8")); if (metadata.KPlugin.Id !== process.argv[2]) process.exit(1)' \
        "$plasma_plasmoid_root/glassy-blacknpink.$widget/metadata.json" "glassy-blacknpink.$widget"
    [ -f "$plasma_plasmoid_root/glassy-blacknpink.$widget/contents/ui/main.qml" ] \
        || fail "Glassy Black & Pink $widget widget has no QML entry point"
done
node -e '
const fs = require("node:fs");
const root = process.argv[1];
const overview = fs.readFileSync(`${root}/glassy-blacknpink.overview/contents/ui/main.qml`, "utf8");
const session = fs.readFileSync(`${root}/glassy-blacknpink.session/contents/ui/main.qml`, "utf8");
const power = fs.readFileSync(`${root}/glassy-blacknpink.power/contents/ui/main.qml`, "utf8");
const island = fs.readFileSync(`${root}/glassy-blacknpink.island/contents/ui/main.qml`, "utf8");
if (!overview.includes(`text: qsTr("Overview")`) || !overview.includes("font.pointSize: 14") || !overview.includes("Layout.minimumWidth: implicitWidth") || !overview.includes("Layout.fillHeight: true") || !overview.includes("invokeShortcut Overview") || !overview.includes("CanFillArea")) process.exit(1);
const ordered = (source, values) => values.every((value, index) => source.indexOf(value) >= 0 && (index === 0 || source.indexOf(values[index - 1]) < source.indexOf(value)));
if (!ordered(session, [`qsTr("Lock")`, `qsTr("Log Out")`, `qsTr("Switch User")`])) process.exit(1);
if (!session.includes("enabled: session.canSwitchUser")) process.exit(1);
if (!ordered(power, [`qsTr("Restart")`, `qsTr("Shut Down")`, `qsTr("Sleep")`, `qsTr("Hibernate")`])) process.exit(1);
if (session.includes("ToolTip") || power.includes("ToolTip")) process.exit(1);
if (!session.includes("popupType: QQC2.Popup.Window") || !power.includes("popupType: QQC2.Popup.Window")) process.exit(1);
if (island.includes("/tmp/") || island.includes("ISLAND_") || island.includes("configuration.readyWidgets") || island.includes("configuration.trayGroups")) process.exit(1);
if (!island.includes("function closeAfterDeactivation()") || !island.includes("function onVisibleChanged() { if (!target.visible) island.closeAfterDeactivation(); }")) process.exit(1);
for (const widget of ["systemmonitor.cpu", "systemmonitor.memory", "systemmonitor.net", "calendar", "notifications", "systemtray"]) {
    if (!island.includes(`"org.kde.plasma.${widget}"`)) process.exit(1);
}
for (const capability of ["canLock", "canSwitchUser", "canLogout", "canSuspend", "canHibernate", "canReboot", "canShutdown"]) {
    if (!island.includes(`enabled: session.${capability}`)) process.exit(1);
}
' "$plasma_plasmoid_root"
grep -Fxq 'Inherits=breeze-dark,breeze_cursors' "$cursor_theme_root/index.theme" \
    || fail "Glassy Black & Pink icon and cursor theme does not inherit Breeze"
for cursor in default pointer progress text wait size_hor size_ver; do
    [ -s "$cursor_theme_root/cursors/$cursor" ] \
        || fail "Glassy Black & Pink cursor theme is missing $cursor"
done
node -e '
const fs = require("node:fs");
const path = require("node:path");
const root = process.argv[1];
for (const name of fs.readdirSync(root)) {
    const file = path.join(root, name);
    if (!fs.statSync(file).isFile() || fs.lstatSync(file).isSymbolicLink()) continue;
    const data = fs.readFileSync(file);
    const count = data.readUInt32LE(12);
    if (!count || data.toString("ascii", 0, 4) !== "Xcur") process.exit(1);
    for (let i = 0; i < count; i++) {
        const slot = data.readUInt32LE(16 + i * 12 + 4);
        const offset = data.readUInt32LE(16 + i * 12 + 8);
        if (slot !== 40 || offset + 36 + 40 * 40 * 4 > data.length ||
            data.readUInt32LE(offset + 8) !== 40 ||
            data.readUInt32LE(offset + 16) !== 40 ||
            data.readUInt32LE(offset + 20) !== 40) process.exit(1);
    }
}
' "$cursor_theme_root/cursors" \
    || fail "Glassy Black & Pink cursors artwork and cursor slots must both be 40 px"
cursor_build_dir="$test_root/cursor-build"
python3 "$repo_root/cursor-art/build.py" --output-dir "$cursor_build_dir" \
    || fail "Glassy Black & Pink cursors SVG artwork did not build"
for cursor in default crosshair help no-drop up-arrow person location size_hor size_ver size_fdiag size_bdiag progress wait; do
    cmp -s "$cursor_theme_root/cursors/$cursor" "$cursor_build_dir/$cursor" \
        || fail "Glassy Black & Pink cursors $cursor does not match its SVG source"
done
python3 - "$cursor_theme_root/cursors/default" "$cursor_theme_root/cursors/crosshair" <<'PY' || fail "Glassy Black & Pink cursors contours or centers are incorrect"
import pathlib
import struct
import sys

images = {}
for file in sys.argv[1:]:
    data = pathlib.Path(file).read_bytes()
    offset = struct.unpack_from('<I', data, 24)[0]
    pixels = struct.unpack_from('<1600I', data, offset + 36)
    images[pathlib.Path(file).name] = pixels
    for y in range(40):
        for x in range(20):
            assert pixels[y * 40 + x] == pixels[y * 40 + 39 - x]
    if file.endswith('/default'):
        for y in range(40):
            for x in range(40):
                assert pixels[y * 40 + x] == pixels[x * 40 + 39 - y]
    for x, y in ((19, 19), (20, 19), (19, 20), (20, 20)):
        pixel = pixels[y * 40 + x]
        if file.endswith('/default'):
            assert pixel == 0x66000000
        else:
            assert pixel >> 24 == 255 and (pixel >> 16) & 255 > (pixel >> 8) & 255
for index, (normal, precision) in enumerate(zip(images['default'], images['crosshair'])):
    if index % 40 in range(17, 23) and index // 40 in range(17, 23):
        assert normal == 0x66000000
        if index % 40 in (19, 20) and index // 40 in (19, 20):
            assert precision == 0xffff4ead
    else:
        assert normal == precision
PY
python3 - "$cursor_theme_root/cursors" <<'PY' || fail "Glassy Black & Pink resize arrows or shared cross are incorrect"
import pathlib
import struct
import sys

root = pathlib.Path(sys.argv[1])
for name in ('size_hor', 'size_ver', 'size_fdiag', 'size_bdiag'):
    data = (root / name).read_bytes()
    assert struct.unpack_from('<I', data, 12)[0] == 2
    for index in range(2):
        offset = struct.unpack_from('<I', data, 24 + 12 * index)[0]
        assert struct.unpack_from('<3I', data, offset + 24) == (20, 20, 167)
        pixels = struct.unpack_from('<1600I', data, offset + 36)
        for y in range(17, 23):
            for x in range(17, 23):
                assert pixels[y * 40 + x] == 0x66000000
        if index == 0:
            nw, ne, sw, se = (pixels[y * 40 + x] for x, y in
                               ((11, 11), (28, 11), (11, 28), (28, 28)))
            if name == 'size_fdiag':
                assert nw and se and not ne and not sw
            elif name == 'size_bdiag':
                assert ne and sw and not nw and not se
            else:
                assert not (nw or ne or sw or se)
PY
for cursor in pointer grab grabbing move dnd-move dnd-copy text; do
    [ "$(readlink "$cursor_theme_root/cursors/$cursor")" = crosshair ] \
        || fail "Glassy Black & Pink cursor theme does not use Precision Select for $cursor"
done
node -e 'const metadata = JSON.parse(require("node:fs").readFileSync(process.argv[1], "utf8")); if (metadata.KPlugin.Id !== "glassy-blacknpink") process.exit(1)' \
    "$plasma_theme_root/metadata.json"
grep -Fxq 'FallbackTheme=default' "$plasma_theme_root/plasmarc" \
    || fail "Glassy Black & Pink Plasma style does not inherit Breeze"
node -e 'const metadata = JSON.parse(require("node:fs").readFileSync(process.argv[1], "utf8")); if (metadata.KPackageStructure !== "Plasma/LookAndFeel" || metadata.KPlugin.Id !== "glassy-blacknpink.desktop") process.exit(1)' \
    "$plasma_global_theme_root/metadata.json"
grep -Fxq 'ColorScheme=GlassyBlacknpink' "$plasma_global_theme_root/contents/defaults" \
    || fail "Glassy Black & Pink global theme does not select its color scheme"
grep -Fxq 'name=glassy-blacknpink' "$plasma_global_theme_root/contents/defaults" \
    || fail "Glassy Black & Pink global theme does not select its Plasma theme"
grep -Fxq 'cursorTheme=glassy-blacknpink' "$plasma_global_theme_root/contents/defaults" \
    || fail "Glassy Black & Pink global theme does not select its cursor theme"
for setting in widgetStyle=GlassyBlacknpink library=glassyblacknpink BorderSize=None Theme=glassy-blacknpink \
    Image=glassy-blacknpink Theme=glassy-blacknpink.desktop 'font=Iosevka Nerd Font,12,-1,5,50,0,0,0,0,0'; do
    grep -Fxq "$setting" "$plasma_global_theme_root/contents/defaults" \
        || fail "Glassy Black & Pink global theme does not set $setting"
done
[ -f "$plasma_global_theme_root/contents/splash/Splash.qml" ] \
    || fail "Glassy Black & Pink global theme has no splash screen"
grep -Fq 'id="thick-hint-right-margin" x="95" y="-56" width="4" height="4"' \
    "$plasma_theme_root/widgets/panel-background.svg" \
    || fail "Glassy Black & Pink Plasma style does not set a 4-pixel floating-panel trailing margin"
grep -Fq 'id="thick-hint-left-margin" x="95" y="-20" width="4" height="8"' \
    "$plasma_theme_root/widgets/panel-background.svg" \
    || fail "Glassy Black & Pink Plasma style changed the Breeze floating-panel leading margin"
grep -Fq 'id="hint-top-margin" x="20" y="10" width="4" height="4"' \
    "$plasma_theme_root/widgets/panel-background.svg" \
    || fail "Glassy Black & Pink Plasma style changed the top panel's vertical content margin"
grep -Fq 'id="hint-left-margin" x="0" y="30" width=".00000001" height="4"' \
    "$plasma_theme_root/widgets/panel-background.svg" \
    || fail "Glassy Black & Pink Plasma style does not extend the Overview control to the screen left"
resvg "$plasma_theme_root/widgets/panel-background.svg" \
    "$test_root/panel-background.png"

# The layout command, run with Plasma simulated.
layout_home="$test_root/home"
layout_bin="$test_root/bin"
layout_command="$repo_root/usr/bin/glassy-blacknpink-layout"
mkdir -p "$layout_home/.config" "$layout_home/.local/share" "$layout_bin"
printf '[KDE]\nLookAndFeelPackage=glassy-blacknpink.desktop\n' >"$layout_home/.config/kdeglobals"
cat >"$layout_bin/qdbus6" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$*" >>"$HOME/qdbus.log"
if [ "$#" -eq 2 ]; then
    exit "${PLASMA_DBUS_STATUS:-0}"
fi
printf '%s\n' "${PLASMA_EVALUATE_OUTPUT:-GLASSY_BLACKNPINK_STATUS=ok:version=5:screens=2}"
EOF
chmod +x "$layout_bin/qdbus6"
run_layout() {
    env -u XDG_CONFIG_HOME -u XDG_DATA_HOME -u XDG_STATE_HOME \
        HOME="$layout_home" XDG_DATA_DIRS="$repo_root/usr/share" PATH="$layout_bin:/usr/bin:/bin" "$@" "$layout_command"
}
printf 'default panel configuration\n' >"$layout_home/.config/plasma-org.kde.plasma.desktop-appletsrc"

# The first run backs up the panels once and records the layout version.
run_layout
[ "$(<"$layout_home/.local/state/glassy-blacknpink/layout-version")" = 5 ] \
    || fail "The layout command did not record its version"
grep -q 'evaluateScript' "$layout_home/qdbus.log" \
    || fail "The layout command did not send the theme's layout to Plasma"
shopt -s nullglob
plasma_backups=("$layout_home/.config/plasma-org.kde.plasma.desktop-appletsrc.backup."*)
[ "${#plasma_backups[@]}" -eq 1 ] \
    || fail "The layout command did not back up its initial panel configuration once"
[ "$(<"${plasma_backups[0]}")" = 'default panel configuration' ] \
    || fail "The panel backup changed its contents"

run_layout
plasma_backups=("$layout_home/.config/plasma-org.kde.plasma.desktop-appletsrc.backup."*)
[ "${#plasma_backups[@]}" -eq 1 ] \
    || fail "The layout command repeated its first-run backup"

if run_layout PLASMA_EVALUATE_OUTPUT='GLASSY_BLACKNPINK_STATUS=missing:org.kde.plasma.icontasks' >/dev/null 2>&1; then
    fail "The layout command accepted a missing required widget"
fi

layout_status=0
run_layout PLASMA_DBUS_STATUS=1 || layout_status=$?
[ "$layout_status" -eq 75 ] \
    || fail "The layout command did not defer when Plasma was inactive"

# Under another global theme the panels belong to that theme.
printf '[KDE]\nLookAndFeelPackage=org.kde.breeze.desktop\n' >"$layout_home/.config/kdeglobals"
: >"$layout_home/qdbus.log"
run_layout || fail "The layout command failed under another global theme"
[ ! -s "$layout_home/qdbus.log" ] \
    || fail "The layout command changed the panels of another global theme"

# The services and the autostart entry, checked against this repository's commands.
unit_dir="$test_root/units"
mkdir -p "$unit_dir"
for unit in "$repo_root"/usr/lib/systemd/user/*.service; do
    sed "s#^ExecStart=/usr/bin/#ExecStart=$repo_root/usr/bin/#" "$unit" >"$unit_dir/${unit##*/}"
done
systemd-analyze verify "$unit_dir"/*.service
desktop-file-validate "$repo_root/etc/xdg/autostart/glassy-blacknpink-layout.desktop"
bash -n "$repo_root/install.sh" "$repo_root/usr/bin/glassy-blacknpink-layout" "$repo_root/usr/bin/glassy-blacknpink-rebuild"
printf '%s\n' 'Theme files, cursors, widgets and layout command tests passed.'
