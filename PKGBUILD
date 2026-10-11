# Build this package with install.sh. It passes the fingerprint of these
# sources and of the KDE libraries they link to, and builds from the clone so
# the login check can rebuild from the same place.
pkgname=glassy-blacknpink
pkgver=2.0.0
pkgrel=1
pkgdesc='Glassy Black & Pink global theme for KDE Plasma, with a glass KWin effect and a widget style built from Breeze'
arch=('x86_64')
options=('!debug')
url='https://github.com/inayayousfi/glassy-blacknpink'
license=('GPL-2.0-or-later' 'GPL-3.0-or-later' 'LGPL-2.1-or-later' 'MIT')
depends=('breeze' 'frameworkintegration' 'kcmutils' 'kcolorscheme' 'kconfig' 'kcoreaddons'
         'kdecoration' 'kguiaddons' 'ki18n' 'kiconthemes' 'kirigami' 'kwidgetsaddons'
         'kwin' 'kwindowsystem' 'plasma-workspace' 'qt6-base' 'qt6-declarative' 'qt6-svg'
         'qt6-tools' 'ttf-iosevka-nerd')
makedepends=('cmake' 'ninja' 'extra-cmake-modules')

_effect_id() {
    printf 'glassy_blacknpink_glass_%s\n' "${GLASSY_BLACKNPINK_FINGERPRINT:0:12}"
}

build() {
    [[ -n "${GLASSY_BLACKNPINK_FINGERPRINT:-}" ]] || {
        printf 'Build Glassy Black & Pink with install.sh\n' >&2
        return 1
    }
    cmake -S "$startdir/breeze" -B "$srcdir/breeze" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
        -DBUILD_QT5=OFF -DBUILD_TESTING=OFF
    cmake --build "$srcdir/breeze" --parallel 4
    cmake -S "$startdir/glass" -B "$srcdir/glass" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
        -DGLASS_EFFECT_ID="$(_effect_id)"
    cmake --build "$srcdir/glass" --parallel 4
}

package() {
    DESTDIR="$pkgdir" cmake --install "$srcdir/breeze"
    DESTDIR="$pkgdir" cmake --install "$srcdir/glass"
    cp -a --no-preserve=ownership "$startdir/usr" "$startdir/etc" "$pkgdir/"
    # KDE ignores an icon theme whose listed folder is missing.
    install -d "$pkgdir/usr/share/icons/glassy-blacknpink/scalable/apps"
    install -d "$pkgdir/usr/lib/systemd/user/graphical-session.target.wants"
    ln -s ../glassy-blacknpink-rebuild.service \
        "$pkgdir/usr/lib/systemd/user/graphical-session.target.wants/glassy-blacknpink-rebuild.service"
    install -Dm644 "$startdir/glass/LICENSE-OverShifted-LiquidGlass" \
        "$pkgdir/usr/share/licenses/$pkgname/LICENSE-OverShifted-LiquidGlass"

    local data="$pkgdir/usr/share/$pkgname"
    install -d "$data"
    printf '%s\n' "$GLASSY_BLACKNPINK_FINGERPRINT" >"$data/fingerprint"
    _effect_id >"$data/effect-id"
    printf '%s\n' "$startdir" >"$data/source"
}
