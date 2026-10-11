# Glassy Black & Pink

Glassy Black & Pink is a global theme for KDE Plasma 6.7 through 6.x on Arch-based systems. It installs as one pacman package and appears in System Settings beside the other global themes.

The theme contains:

- a widget style and window decoration built from a [fork of KDE Breeze](https://github.com/inayayousfi/glassy-blacknpink-breeze), kept as a Git submodule in `breeze/`;

- a KWin glass effect in `glass/` that gives whole windows, menus and panels a rounded glass surface;

- the color scheme, Plasma style, icon and cursor theme, black splash screen and black wallpaper;

- a top panel with a clock island holding paged Plasma controls, and an application dock, on every display.

Files under `usr/` and `etc/` keep the path they are installed to.

## Install

```sh
git clone --recurse-submodules https://github.com/inayayousfi/glassy-blacknpink.git
cd glassy-blacknpink
./install.sh
```

`install.sh` builds the package with `makepkg`, installs it through `pkexec`, turns the glass effect and panel script on for your user, and loads the glass into the running KWin. Then choose Glassy Black & Pink in System Settings under Colors & Themes, Global Theme. Set `GLASSY_BLACKNPINK_ELEVATE` to use another command than `pkexec` for the installation.

The widget style, decoration and glass are compiled against KWin, KDecoration and Qt. At every login, the package's `glassy-blacknpink-rebuild` service runs `install.sh` from the clone the package was built from, and rebuilds the package when those libraries or the theme's sources changed. Keep the clone where you installed it from, or run `install.sh` again from its new place.

## Tests

`test/run.sh` runs the tests that need no installed build. The `test/test-glass-*.py` tests check the installed glass in an offscreen Qt window and in a virtual KWin session.

## Credits

The glass shader adapts its refraction and lighting from OverShifted's LiquidGlass; see `glass/LICENSE-OverShifted-LiquidGlass`. The cursor artwork takes its crosshair idea and pink palette from AarogyaGaming's public-domain Crosshair Cursors; see `cursors/README.md`.
