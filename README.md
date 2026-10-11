# Glassy Black & Pink

Glassy Black & Pink is a global theme for KDE Plasma 6.7 through 6.x on Arch-based systems. It installs as one pacman package and appears in System Settings beside the other global themes.

The theme contains:

- a widget style and window decoration built from KDE Breeze, whose modified sources sit at the root of this repository;

- a KWin glass effect in `glass/` that gives whole windows, menus and panels a rounded glass surface;

- the color scheme, Plasma style, icon and cursor theme, black splash screen and black wallpaper;

- a top panel with a clock island holding paged Plasma controls, and an application dock, on every display.

Files under `usr/` and `etc/` keep the path they are installed to.

## Install

```sh
git clone https://github.com/inayayousfi/glassy-blacknpink-plasma.git
cd glassy-blacknpink-plasma
./install.sh
```

`install.sh` builds the package with `makepkg`, installs it through `pkexec`, turns the glass effect and panel script on for your user, and loads the glass into the running KWin. Then choose Glassy Black & Pink in System Settings under Colors & Themes, Global Theme. Set `GLASSY_BLACKNPINK_ELEVATE` to use another command than `pkexec` for the installation.

The widget style, decoration and glass are compiled against KWin, KDecoration and Qt. At every login, the package's `glassy-blacknpink-rebuild` service runs `install.sh` from the clone the package was built from, and rebuilds the package when those libraries or the theme's sources changed. Keep the clone where you installed it from, or run `install.sh` again from its new place.

## Breeze

The root holds KDE Breeze's sources, from [invent.kde.org/plasma/breeze](https://invent.kde.org/plasma/breeze), with the theme's changes to its widget style and window decoration; `README.breeze.md` is Breeze's own README. The theme's own files sit beside them: `glass/`, `cursor-art/`, `usr/`, `etc/`, `test/`, `install.sh` and `PKGBUILD`.

The `breeze-upstream` branch holds Breeze exactly as KDE published it, one commit per version taken, without KDE's history. To move to a newer Breeze:

1. Check out `breeze-upstream` and replace its files with the new KDE Breeze sources, for example from a release archive.

2. Commit them as one commit that names the KDE version or commit.

3. Check out `main` and merge `breeze-upstream`. Git applies the theme's changes to the new sources; resolve any conflicts, keeping the theme's `README.md`.

## Tests

`test/run.sh` runs the tests that need no installed build. The `test/test-glass-*.py` tests check the installed glass in an offscreen Qt window and in a virtual KWin session.

## Credits

The glass shader combines ideas and formulas from several projects. Eamon Liu's [liquid-glass-js](https://www.npmjs.com/package/liquid-glass-js) gives the rim profile, the refraction along the edge, the color fringing and the rim light; see `glass/LICENSE-liquid-glass-js`. ybouane's [@ybouane/liquidglass](https://liquid-glass.ybouane.com/) gives the Fresnel reflection and the inner stroke; see `glass/LICENSE-ybouane-liquidglass`. The squircle surface with exponent 4 and the refraction along the edge normal follow the model described in [Liquid Glass in the Browser](https://kube.io/blog/liquid-glass-css-svg/) on kube.io and Htet Lin Aung's [liquid-glass](https://github.com/Hla-aung/liquid-glass); only ideas are used, no code. sentinelcmd's public-domain (CC0) [Liquid Glass Frutiger Aero UI](https://godotshaders.com/shader/liquid-glass-ui-customizable/) Godot shader gives the top sheen. The shader started from OverShifted's [LiquidGlass](https://github.com/OverShifted/LiquidGlass); see `glass/LICENSE-OverShifted-LiquidGlass`. The cursor artwork takes its crosshair idea and pink palette from AarogyaGaming's public-domain Crosshair Cursors; see `cursor-art/README.md`.
