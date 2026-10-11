#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$repo_root/install.sh"
test_root="$(mktemp -d)"
trap 'rm -rf "$test_root"' EXIT
log="$test_root/calls"
data_dir="$test_root/data"
mkdir "$data_dir"

# A small source tree stands in for the clone; the build and pacman are simulated.
GLASSY_BLACKNPINK_ROOT="$test_root/source"
mkdir -p "$GLASSY_BLACKNPINK_ROOT/test"
printf 'project(breeze)\n' >"$GLASSY_BLACKNPINK_ROOT/CMakeLists.txt"
printf 'one\n' >"$GLASSY_BLACKNPINK_ROOT/theme-file"
kwin_version='kwin 6.7.4-1'

fail() {
    printf '%s\n' "$*" >&2
    exit 1
}
pacman() {
    [[ "$*" == '-Q kwin kdecoration qt6-base' ]] || return 1
    printf '%s\nkdecoration 6.7.4-1\nqt6-base 6.11.0-1\n' "$kwin_version"
}
makepkg() {
    printf 'build:%s:%s\n' "$PWD" "$*" >>"$log"
    [[ -n "$GLASSY_BLACKNPINK_FINGERPRINT" && -d "$BUILDDIR" && "$PKGDEST" == "$BUILDDIR" ]]
    printf '%s\n' "$GLASSY_BLACKNPINK_FINGERPRINT" >"$PKGDEST/fingerprint"
    touch "$PKGDEST/glassy-blacknpink-2.0.0-1-x86_64.pkg.tar.zst"
}
fake_elevate() {
    [[ "$1 $2 $3" == 'pacman -U --noconfirm' && -f "$4" ]]
    printf 'install:%s\n' "${4##*/}" >>"$log"
    local fingerprint
    fingerprint="$(<"${4%/*}/fingerprint")"
    cp "${4%/*}/fingerprint" "$data_dir/fingerprint"
    printf 'glassy_blacknpink_glass_%s\n' "${fingerprint:0:12}" >"$data_dir/effect-id"
}
GLASSY_BLACKNPINK_ELEVATE=fake_elevate
kwriteconfig6() { printf 'config:%s\n' "$*" >>"$log"; }
expect_config() {
    grep -Fxq "config:--file kwinrc --group Plugins --key $1" "$log" || fail "Missing setting: $1"
}

# A first install builds from the clone and turns the glass and panels on.
: >"$log"
glassy_blacknpink_install "$data_dir"
grep -q "^build:$GLASSY_BLACKNPINK_ROOT:--syncdeps --noconfirm$" "$log" || fail "The first install did not build from the clone"
grep -q '^install:glassy-blacknpink-2.0.0-1-x86_64.pkg.tar.zst$' "$log" || fail "The first install did not install the package"
first_effect="$(<"$data_dir/effect-id")"
expect_config 'blurEnabled false'
expect_config "${first_effect}Enabled true"
expect_config 'glassy-blacknpink-panelsEnabled true'
grep -q -- '--delete' "$log" && fail "The first install deleted a setting"

# Nothing changed: no rebuild.
: >"$log"
glassy_blacknpink_install "$data_dir"
grep -q '^build:' "$log" && fail "An unchanged install rebuilt the package"
expect_config "${first_effect}Enabled true"

# Test files do not count as theme sources.
printf 'new test\n' >"$GLASSY_BLACKNPINK_ROOT/test/check"
: >"$log"
glassy_blacknpink_install "$data_dir"
grep -q '^build:' "$log" && fail "A test change rebuilt the package"

# A source change rebuilds under a new effect name; the old name's switch goes.
printf 'two\n' >"$GLASSY_BLACKNPINK_ROOT/theme-file"
: >"$log"
glassy_blacknpink_install "$data_dir"
grep -q '^build:' "$log" || fail "A source change did not rebuild the package"
second_effect="$(<"$data_dir/effect-id")"
[[ "$second_effect" != "$first_effect" ]] || fail "A rebuild kept the old effect name"
expect_config "${first_effect}Enabled --delete"
expect_config "${second_effect}Enabled true"

# A KWin update rebuilds the plugins it links to.
kwin_version='kwin 6.7.5-1'
: >"$log"
glassy_blacknpink_install "$data_dir"
grep -q '^build:' "$log" || fail "A KWin update did not rebuild the package"

# Links count by where they point, including links to folders.
mkdir "$GLASSY_BLACKNPINK_ROOT/folder"
ln -s folder "$GLASSY_BLACKNPINK_ROOT/link"
glassy_blacknpink_install "$data_dir" >/dev/null 2>"$test_root/errors"
[[ ! -s "$test_root/errors" ]] || fail "The fingerprint failed on a link to a folder: $(<"$test_root/errors")"
ln -sfn theme-file "$GLASSY_BLACKNPINK_ROOT/link"
: >"$log"
glassy_blacknpink_install "$data_dir" >/dev/null
grep -q '^build:' "$log" || fail "A changed link did not rebuild the package"

# Without the Breeze sources there is nothing to build.
mv "$GLASSY_BLACKNPINK_ROOT/CMakeLists.txt" "$test_root/CMakeLists.txt"
: >"$log"
if glassy_blacknpink_install "$data_dir" 2>/dev/null; then fail "The install accepted missing Breeze sources"; fi
grep -q '^build:' "$log" && fail "The install built without the Breeze sources"
mv "$test_root/CMakeLists.txt" "$GLASSY_BLACKNPINK_ROOT/CMakeLists.txt"

effect_id=glassy_blacknpink_glass_fixture
printf '%s\n' "$effect_id" >"$data_dir/effect-id"
session_active=true
loaded_effects=$'blur\nglassy_blacknpink_glass_old\nother_effect'
load_succeeds=true
shaders_valid=true
qdbus6() {
    printf 'dbus:%s\n' "$*" >>"$log"
    [[ "$2" != /KWin ]] || {
        "$session_active" || return 1
        return 0
    }
    case "$3" in
        *.loadedEffects) printf '%s\n' "$loaded_effects" ;;
        *.loadEffect) [[ "$4" == blur ]] && printf 'true\n' || printf '%s\n' "$load_succeeds" ;;
        *.debug) "$shaders_valid" && printf 'valid=1\n' || printf 'valid=0\n' ;;
    esac
}
systemctl() { printf 'systemctl:%s\n' "$*" >>"$log"; }

# Loading replaces KDE's blur and older glass builds, and drops their switches.
: >"$log"
glassy_blacknpink_activate "$data_dir"
grep -q 'loadEffect glassy_blacknpink_glass_fixture' "$log"
grep -q 'unloadEffect blur' "$log"
grep -q 'unloadEffect glassy_blacknpink_glass_old' "$log"
grep -q 'unloadEffect other_effect' "$log" && fail "Activation unloaded an unrelated effect"
grep -Fq 'key glassy_blacknpink_glass_oldEnabled --delete' "$log"
grep -Fq 'key blurEnabled false' "$log"
unload_line="$(grep -n 'Effects.unloadEffect glassy_blacknpink_glass_old$' "$log" | cut -d: -f1)"
load_line="$(grep -n 'Effects.loadEffect glassy_blacknpink_glass_fixture$' "$log" | cut -d: -f1)"
[[ "$unload_line" -lt "$load_line" ]] || fail "The old glass was unloaded after the new one loaded"

# An already loaded build is reloaded so KWin reads the new plugin.
: >"$log"
loaded_effects=$'glassy_blacknpink_glass_fixture\nother_effect'
glassy_blacknpink_activate "$data_dir"
grep -q 'Effects.unloadEffect glassy_blacknpink_glass_fixture' "$log"
grep -q 'Effects.loadEffect glassy_blacknpink_glass_fixture' "$log"
grep -q 'reconfigureEffect glassy_blacknpink_glass_fixture' "$log"

# A glass that fails to load or to compile its shaders falls back to KDE's blur.
for failure in load shader; do
    : >"$log"
    loaded_effects=$'blur\nglassy_blacknpink_glass_old\nother_effect'
    load_succeeds=true
    shaders_valid=true
    if [[ "$failure" == load ]]; then load_succeeds=false; else shaders_valid=false; fi
    if glassy_blacknpink_activate "$data_dir" >/dev/null 2>&1; then fail "Activation accepted a failed $failure"; fi
    grep -q 'Effects.loadEffect blur' "$log"
    grep -q 'unloadEffect other_effect' "$log" && fail "The fallback unloaded an unrelated effect"
    grep -q 'blurEnabled true' "$log"
done
load_succeeds=true
shaders_valid=true

# The login run reloads KWin and Plasma after loading the glass.
glassy_blacknpink_fingerprint() { cat "$data_dir/fingerprint"; }
printf '%s\n' "$effect_id" >"$data_dir/effect-id"
GLASSY_BLACKNPINK_DATA="$data_dir"
: >"$log"
glassy_blacknpink_main >/dev/null
grep -Fxq 'dbus:org.kde.KWin /KWin org.kde.KWin.reconfigure' "$log" || fail "The login run did not reload KWin"
grep -Fxq 'systemctl:--user try-restart plasma-plasmashell.service' "$log" || fail "The login run did not restart Plasma"

# Outside a Plasma session the glass waits for the next login.
: >"$log"
session_active=false
glassy_blacknpink_activate "$data_dir" >/dev/null
grep -q '/Effects' "$log" && fail "Activation used KWin outside a session"
printf '%s\n' 'Install, rebuild, activation, fallback and login tests passed.'
