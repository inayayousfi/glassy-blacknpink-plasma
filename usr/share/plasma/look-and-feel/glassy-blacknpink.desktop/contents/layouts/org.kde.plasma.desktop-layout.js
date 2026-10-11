// Raising this version rebuilds the managed panels that an older layout made.
const layoutVersion = typeof glassyBlacknpinkLayoutVersion === "undefined" ? "5" : glassyBlacknpinkLayoutVersion;
// The login command says whether it has applied this layout before. Plasma
// applying the global theme's layout starts from an empty desktop, which is
// a first run as well.
const firstRun = typeof glassyBlacknpinkFirstRun === "undefined" ? true : glassyBlacknpinkFirstRun;

const requiredWidgetTypes = [
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

const missingWidgetTypes = requiredWidgetTypes.filter(type => !knownWidgetTypes.includes(type));
if (missingWidgetTypes.length > 0) {
    print(`GLASSY_BLACKNPINK_STATUS=missing:${missingWidgetTypes.join(",")}`);
} else {
    function markPanel(panel, role, screen) {
        panel.currentConfigGroup = [];
        panel.writeConfig("glassyBlacknpinkManaged", "true");
        panel.writeConfig("glassyBlacknpinkRole", role);
        panel.writeConfig("glassyBlacknpinkScreen", screen);
        panel.writeConfig("glassyBlacknpinkLayoutVersion", layoutVersion);
    }

    function panelRole(panel) {
        panel.currentConfigGroup = [];
        if (String(panel.readConfig("glassyBlacknpinkManaged", "false")) !== "true") {
            return "";
        }
        return String(panel.readConfig("glassyBlacknpinkRole", ""));
    }

    function panelVersion(panel) {
        panel.currentConfigGroup = [];
        return String(panel.readConfig("glassyBlacknpinkLayoutVersion", ""));
    }

    function panelScreen(panel) {
        panel.currentConfigGroup = [];
        return Number(panel.readConfig("glassyBlacknpinkScreen", panel.screen));
    }

    function addTopPanelWidgets(panel) {
        panel.addWidget("glassy-blacknpink.island");
    }

    function configureTaskManager(tasks) {
        tasks.currentConfigGroup = ["General"];
        tasks.writeConfig("showOnlyCurrentDesktop", false);
        tasks.writeConfig("fill", false);
    }

    function configureLauncher(launcher) {
        launcher.currentConfigGroup = ["General"];
        launcher.writeConfig("alphaSort", true);
        launcher.writeConfig("highlightNewlyInstalledApps", false);
        launcher.writeConfig("showRecentApps", false);
        launcher.writeConfig("showRecentDocs", false);
    }

    function addDockWidgets(panel, launchers = []) {
        configureLauncher(panel.addWidget("org.kde.plasma.kickerdash"));
        panel.addWidget("glassy-blacknpink.overview");

        const tasks = panel.addWidget("org.kde.plasma.icontasks");
        tasks.currentConfigGroup = ["General"];
        tasks.writeConfig("launchers", launchers);
        tasks.writeConfig("showOnlyCurrentScreen", true);
        tasks.writeConfig("showOnlyCurrentActivity", true);
        configureTaskManager(tasks);
    }

    function replacePanelWidgets(panel, role) {
        const widgets = panel.widgets();
        if (role === "top" && widgets.length === 1 && widgets[0].type === "glassy-blacknpink.island") {
            return;
        }
        if (role === "dock"
            && widgets.length === 3
            && widgets[0].type === "org.kde.plasma.kickerdash"
            && widgets[1].type === "glassy-blacknpink.overview"
            && widgets[2].type === "org.kde.plasma.icontasks") {
            return;
        }
        let launchers = [];
        if (role === "dock") {
            const tasks = panel.widgets().find(widget => widget.type === "org.kde.plasma.icontasks");
            if (tasks) {
                tasks.currentConfigGroup = ["General"];
                launchers = tasks.readConfig("launchers", []);
            }
        }
        panel.widgets().slice().forEach(widget => widget.remove());
        if (role === "top") {
            addTopPanelWidgets(panel);
        } else {
            addDockWidgets(panel, launchers);
        }
    }

    function configureTopPanel(panel, screen) {
        panel.screen = screen;
        panel.location = "top";
        panel.alignment = "center";
        panel.lengthMode = "fit";
        panel.height = 68;
        panel.hiding = "autohide";
        panel.floating = false;
        panel.opacity = "adaptive";
        markPanel(panel, "top", screen);
    }

    function configureDock(panel, screen) {
        panel.screen = screen;
        panel.location = "bottom";
        panel.alignment = "center";
        panel.lengthMode = "fit";
        panel.height = 47;
        panel.hiding = "autohide";
        panel.floating = true;
        panel.opacity = "translucent";
        panel.widgets().forEach(widget => {
            if (widget.type === "org.kde.plasma.kickerdash") {
                configureLauncher(widget);
            } else if (widget.type === "org.kde.plasma.icontasks") {
                configureTaskManager(widget);
            }
        });
        markPanel(panel, "dock", screen);
    }

    const initialPanels = panels();
    const existingManaged = initialPanels.filter(panel => panelRole(panel) !== "");
    const destructiveFirstRun = firstRun && existingManaged.length === 0;
    const managed = destructiveFirstRun ? [] : existingManaged;
    const retained = new Map();
    const stale = [];
    const staleDockLaunchers = new Map();
    managed.forEach(panel => {
        const role = panelRole(panel);
        const screen = panelScreen(panel);
        if (role === "dock") {
            panel.widgets().forEach(widget => {
                if (widget.type === "org.kde.plasma.icontasks") {
                    configureTaskManager(widget);
                }
            });
        }
        if (screen < 0 || screen >= screenCount) {
            return;
        }
        const key = `${role}:${screen}`;
        if ((role !== "top" && role !== "dock") || panelVersion(panel) !== layoutVersion || retained.has(key)) {
            if (role === "dock" && !staleDockLaunchers.has(screen)) {
                const tasks = panel.widgets().find(widget => widget.type === "org.kde.plasma.icontasks");
                if (tasks) {
                    tasks.currentConfigGroup = ["General"];
                    staleDockLaunchers.set(screen, tasks.readConfig("launchers", []));
                }
            }
            stale.push(panel);
        } else {
            retained.set(key, panel);
        }
    });

    const created = [];
    try {
        for (let screen = 0; screen < screenCount; screen += 1) {
            const topKey = `top:${screen}`;
            let top = retained.get(topKey);
            if (!top) {
                top = new Panel();
                created.push(top);
                addTopPanelWidgets(top);
            } else {
                replacePanelWidgets(top, "top");
            }
            configureTopPanel(top, screen);

            const dockKey = `dock:${screen}`;
            let dock = retained.get(dockKey);
            if (!dock) {
                dock = new Panel();
                created.push(dock);
                addDockWidgets(dock, staleDockLaunchers.get(screen) || []);
            } else {
                replacePanelWidgets(dock, "dock");
            }
            configureDock(dock, screen);
        }

        if (destructiveFirstRun) {
            initialPanels.forEach(panel => panel.remove());
        } else {
            stale.forEach(panel => panel.remove());
        }

        print(`GLASSY_BLACKNPINK_STATUS=ok:version=${layoutVersion}:screens=${screenCount}`);
    } catch (error) {
        created.forEach(panel => panel.remove());
        print(`GLASSY_BLACKNPINK_STATUS=error:${error}`);
    }
}
