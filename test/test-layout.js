#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");

class Widget {
    constructor(type) {
        this.type = type;
        this.config = new Map();
    }

    readConfig(key, fallback) {
        return this.config.has(key) ? this.config.get(key) : fallback;
    }

    writeConfig(key, value) {
        this.config.set(key, value);
    }

    remove() {
        this.removed = true;
    }
}

const createdPanels = [];
class Panel {
    constructor() {
        this.config = new Map();
        this.panelWidgets = [];
        this.removed = false;
        createdPanels.push(this);
    }

    addWidget(type) {
        const widget = new Widget(type);
        this.panelWidgets.push(widget);
        return widget;
    }

    widgets() {
        return this.panelWidgets.filter(widget => !widget.removed);
    }

    readConfig(key, fallback) {
        return this.config.has(key) ? this.config.get(key) : fallback;
    }

    writeConfig(key, value) {
        this.config.set(key, value);
    }

    remove() {
        this.removed = true;
    }
}

const staleDock = new Panel();
staleDock.screen = 0;
staleDock.writeConfig("glassyBlacknpinkManaged", "true");
staleDock.writeConfig("glassyBlacknpinkRole", "dock");
staleDock.writeConfig("glassyBlacknpinkScreen", 0);
staleDock.writeConfig("glassyBlacknpinkLayoutVersion", "1");
const staleTasks = staleDock.addWidget("org.kde.plasma.icontasks");
staleTasks.writeConfig("launchers", ["applications:org.kde.dolphin.desktop"]);

const layout = fs.readFileSync(process.argv[2], "utf8");
const knownWidgetTypes = [
    "glassy-blacknpink.island",
    "org.kde.plasma.systemmonitor.cpu",
    "org.kde.plasma.systemmonitor.memory",
    "org.kde.plasma.systemmonitor.net",
    "org.kde.plasma.calendar",
    "org.kde.plasma.notifications",
    "org.kde.plasma.systemtray",
    "org.kde.plasma.kickerdash",
    "org.kde.plasma.icontasks",
    "glassy-blacknpink.overview",
];

function runLayout(initialPanels, version, options = {}) {
    const output = [];
    const createdPanelCount = createdPanels.length;
    const context = {
        Map,
        Number,
        Panel,
        String,
        knownWidgetTypes,
        panels: () => initialPanels,
        print: value => output.push(value),
        screenCount: options.screenCount || 1,
        screenGeometry: () => ({x: 0, y: 0, width: 1920, height: 1080}),
    };
    // Plasma applying the global theme's layout passes neither input.
    if (version !== undefined) {
        context.glassyBlacknpinkFirstRun = options.firstRun || false;
        context.glassyBlacknpinkLayoutVersion = version;
    }
    vm.runInNewContext(layout, context);
    return {created: createdPanels.slice(createdPanelCount), output};
}

const upgrade = runLayout([staleDock], "4");

assert.equal(staleDock.removed, true, "stale dock was not replaced");
const replacementTop = upgrade.created.find(panel => panel.readConfig("glassyBlacknpinkRole", "") === "top");
assert.ok(replacementTop, "replacement top panel was not created");
assert.equal(replacementTop.height, 68, "replacement top panel has the wrong height");
assert.equal(replacementTop.lengthMode, "fit", "top panel does not fit the island");
assert.deepEqual(
    replacementTop.widgets().map(widget => widget.type),
    ["glassy-blacknpink.island"],
    "replacement top panel has the wrong controls",
);
const replacementDock = upgrade.created.find(panel => panel.readConfig("glassyBlacknpinkRole", "") === "dock");
assert.ok(replacementDock, "replacement dock was not created");
assert.equal(replacementDock.height, 47, "replacement dock has the wrong height");
assert.equal(replacementDock.lengthMode, "fit", "replacement dock does not fit its content");
assert.deepEqual(
    replacementDock.widgets().map(widget => widget.type),
    ["org.kde.plasma.kickerdash", "glassy-blacknpink.overview", "org.kde.plasma.icontasks"],
    "replacement dock does not place Overview next to the application dashboard",
);
const replacementLauncher = replacementDock.widgets().find(widget => widget.type === "org.kde.plasma.kickerdash");
assert.ok(replacementLauncher, "replacement dock does not use the full-screen application dashboard");
assert.equal(replacementLauncher.readConfig("alphaSort", false), true, "application dashboard is not alphabetical");
assert.equal(replacementLauncher.readConfig("showRecentApps", true), false, "application dashboard shows recent apps");
assert.equal(replacementLauncher.readConfig("showRecentDocs", true), false, "application dashboard shows recent documents");
assert.equal(
    replacementLauncher.readConfig("highlightNewlyInstalledApps", true),
    false,
    "application dashboard highlights newly installed apps",
);
const replacementTasks = replacementDock.widgets().find(widget => widget.type === "org.kde.plasma.icontasks");
assert.deepEqual(
    replacementTasks.readConfig("launchers", []),
    ["applications:org.kde.dolphin.desktop"],
    "replacement dock lost its manual launchers",
);
assert.equal(
    replacementTasks.readConfig("showOnlyCurrentDesktop", true),
    false,
    "replacement task manager hides windows from other virtual desktops",
);
assert.equal(replacementTasks.readConfig("fill", true), false, "replacement task manager fills the dock");
assert.match(upgrade.output.at(-1), /GLASSY_BLACKNPINK_STATUS=ok:version=4:screens=1/);

const disconnectedTop = new Panel();
disconnectedTop.screen = -1;
disconnectedTop.writeConfig("glassyBlacknpinkManaged", "true");
disconnectedTop.writeConfig("glassyBlacknpinkRole", "top");
disconnectedTop.writeConfig("glassyBlacknpinkScreen", 0);
disconnectedTop.writeConfig("glassyBlacknpinkLayoutVersion", "4");
const disconnectedDock = new Panel();
disconnectedDock.screen = -1;
disconnectedDock.writeConfig("glassyBlacknpinkManaged", "true");
disconnectedDock.writeConfig("glassyBlacknpinkRole", "dock");
disconnectedDock.writeConfig("glassyBlacknpinkScreen", 0);
disconnectedDock.writeConfig("glassyBlacknpinkLayoutVersion", "4");
disconnectedDock.addWidget("org.kde.plasma.kickerdash");
disconnectedDock.addWidget("glassy-blacknpink.overview");
disconnectedDock.addWidget("org.kde.plasma.icontasks");

const reconnect = runLayout([disconnectedTop, disconnectedDock], "4");
assert.equal(disconnectedTop.removed, false, "temporarily unassigned top panel was replaced");
assert.equal(disconnectedDock.removed, false, "temporarily unassigned dock was replaced");
assert.equal(disconnectedTop.screen, 0, "top panel did not return to its intended screen");
assert.equal(disconnectedDock.screen, 0, "dock did not return to its intended screen");
assert.equal(reconnect.created.length, 0, "reconnection created duplicate managed panels");

const unrelated = new Panel();
const managedTop = new Panel();
managedTop.screen = 0;
managedTop.writeConfig("glassyBlacknpinkManaged", "true");
managedTop.writeConfig("glassyBlacknpinkRole", "top");
managedTop.writeConfig("glassyBlacknpinkScreen", 0);
managedTop.writeConfig("glassyBlacknpinkLayoutVersion", "4");
const managedClock = managedTop.addWidget("org.kde.plasma.digitalclock");
const extraDiskWidget = managedTop.addWidget("org.kde.plasma.systemmonitor.diskactivity");
const managedDock = new Panel();
managedDock.screen = 0;
managedDock.writeConfig("glassyBlacknpinkManaged", "true");
managedDock.writeConfig("glassyBlacknpinkRole", "dock");
managedDock.writeConfig("glassyBlacknpinkScreen", 0);
managedDock.writeConfig("glassyBlacknpinkLayoutVersion", "4");
const managedLauncher = managedDock.addWidget("org.kde.plasma.kickerdash");
managedLauncher.writeConfig("alphaSort", false);
managedLauncher.writeConfig("showRecentApps", true);
managedLauncher.writeConfig("showRecentDocs", true);
managedDock.addWidget("glassy-blacknpink.overview");
const managedTasks = managedDock.addWidget("org.kde.plasma.icontasks");
managedTasks.writeConfig("showOnlyCurrentDesktop", true);

runLayout([unrelated, managedTop, managedDock], "4", {firstRun: true});
assert.equal(unrelated.removed, false, "missing layout state deleted an unrelated panel");
assert.equal(managedTop.removed, false, "missing layout state replaced an existing managed top panel");
assert.equal(extraDiskWidget.removed, true, "extra disk widget was retained");
assert.equal(managedClock.removed, true, "legacy clock was not replaced");
assert.deepEqual(managedTop.widgets().map(widget => widget.type), ["glassy-blacknpink.island"], "legacy top panel was not migrated in place");
assert.equal(managedDock.removed, false, "missing layout state replaced an existing managed dock");
const updatedLauncher = managedDock.widgets().find(widget => widget.type === "org.kde.plasma.kickerdash");
assert.equal(updatedLauncher.readConfig("alphaSort", false), true, "retained dashboard is not alphabetical");
assert.equal(updatedLauncher.readConfig("showRecentApps", true), false, "retained dashboard shows recent apps");
assert.equal(updatedLauncher.readConfig("showRecentDocs", true), false, "retained dashboard shows recent documents");
assert.equal(
    managedTasks.readConfig("showOnlyCurrentDesktop", true),
    false,
    "retained task manager hides windows from other virtual desktops",
);

const retainedIsland = managedTop.widgets()[0];
const retainedDockWidgets = managedDock.widgets().slice();
retainedIsland.writeConfig("savedWidgetState", "keep");
runLayout([unrelated, managedTop, managedDock], "4");
assert.equal(managedTop.widgets()[0], retainedIsland, "reconciliation recreated the island");
assert.equal(retainedIsland.readConfig("savedWidgetState", ""), "keep", "reconciliation lost island widget state");
assert.equal(managedTop.widgets().length, 1, "reconciliation duplicated the island");
assert.deepEqual(managedDock.widgets(), retainedDockWidgets, "reconciliation recreated an already-correct dock");

const secondDisplay = runLayout([managedTop, managedDock], "4", {screenCount: 2});
const secondTop = secondDisplay.created.find(panel => panel.screen === 1 && panel.readConfig("glassyBlacknpinkRole", "") === "top");
assert.deepEqual(secondTop.widgets().map(widget => widget.type), ["glassy-blacknpink.island"], "new display did not receive an island");
assert.equal(managedTop.widgets()[0], retainedIsland, "adding a display recreated the first island");
assert.deepEqual(managedDock.widgets(), retainedDockWidgets, "adding a display recreated the first dock");

const missingCalendar = knownWidgetTypes.indexOf("org.kde.plasma.calendar");
knownWidgetTypes.splice(missingCalendar, 1);
const missingWidget = runLayout([managedTop, managedDock], "4");
assert.equal(missingWidget.created.length, 0, "missing island dependency changed the panels");
assert.match(missingWidget.output[0], /GLASSY_BLACKNPINK_STATUS=missing:org.kde.plasma.calendar/);
knownWidgetTypes.splice(missingCalendar, 0, "org.kde.plasma.calendar");

const outdatedDisconnectedDock = new Panel();
outdatedDisconnectedDock.screen = -1;
outdatedDisconnectedDock.writeConfig("glassyBlacknpinkManaged", "true");
outdatedDisconnectedDock.writeConfig("glassyBlacknpinkRole", "dock");
outdatedDisconnectedDock.writeConfig("glassyBlacknpinkScreen", 1);
outdatedDisconnectedDock.writeConfig("glassyBlacknpinkLayoutVersion", "1");
const outdatedDisconnectedTasks = outdatedDisconnectedDock.addWidget("org.kde.plasma.icontasks");
outdatedDisconnectedTasks.writeConfig("launchers", ["applications:org.kde.kate.desktop"]);
outdatedDisconnectedTasks.writeConfig("showOnlyCurrentDesktop", true);

runLayout([outdatedDisconnectedDock], "4");
assert.equal(outdatedDisconnectedDock.removed, false, "outdated dock for a disconnected display was removed");
assert.deepEqual(
    outdatedDisconnectedTasks.readConfig("launchers", []),
    ["applications:org.kde.kate.desktop"],
    "outdated disconnected dock lost its launchers",
);
assert.equal(
    outdatedDisconnectedTasks.readConfig("showOnlyCurrentDesktop", true),
    false,
    "disconnected task manager hides windows from other virtual desktops",
);

const fromTheme = runLayout([], undefined);
assert.deepEqual(
    fromTheme.created.map(panel => [panel.readConfig("glassyBlacknpinkRole", ""), panel.readConfig("glassyBlacknpinkLayoutVersion", "")]).sort(),
    [["dock", "5"], ["top", "5"]],
    "applying the global theme's layout did not create current panels",
);
assert.match(fromTheme.output.at(-1), /GLASSY_BLACKNPINK_STATUS=ok:version=5:screens=1/);

console.log("KDE Plasma layout tests passed.");
