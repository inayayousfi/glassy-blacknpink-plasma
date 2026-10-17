#!/usr/bin/env bash

# Builds Glassy Black & Pink into one pacman package and installs it when its
# sources or the KDE libraries it links to changed, then turns its glass and
# panel script on for the current user and loads them into the running KWin.
# Set GLASSY_BLACKNPINK_ELEVATE to the command that runs pacman as root.
# Set GLASSY_BLACKNPINK_REPORT to a file to also collect why it failed there.

GLASSY_BLACKNPINK_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GLASSY_BLACKNPINK_DATA=/usr/share/glassy-blacknpink

glassy_blacknpink_fail() {
    printf '%s\n' "$1" >&2
    [[ -z "${GLASSY_BLACKNPINK_REPORT:-}" ]] || printf '%s\n' "$1" >>"$GLASSY_BLACKNPINK_REPORT"
}

# The first compiler or CMake errors of a failed build, or makepkg's last
# message when the build never reached the compiler.
glassy_blacknpink_build_errors() {
    local errors
    errors="$(grep -E -m 3 ': (fatal )?error: |^CMake Error' "$1" | sed "s#$GLASSY_BLACKNPINK_ROOT/##")"
    [[ -n "$errors" ]] || errors="$(grep -E '^==> ' "$1" | tail -n 1)"
    printf '%s' "$errors"
}

glassy_blacknpink_fingerprint() {
    # The plugins also follow the KWin, decoration and Qt libraries they link to.
    {
        (cd "$GLASSY_BLACKNPINK_ROOT" \
            && find . \( -name .git -o -path ./test \) -prune -o -type f -print0 \
            | sort -z | xargs -0 sha256sum)
        # Some links point to folders, so record where each link points.
        (cd "$GLASSY_BLACKNPINK_ROOT" \
            && find . \( -name .git -o -path ./test \) -prune -o -type l -printf '%p -> %l\n' | LC_ALL=C sort)
        pacman -Q kwin kdecoration qt6-base
    } | sha256sum | cut -d' ' -f1
}

glassy_blacknpink_install() {
    local data_dir="${1:-$GLASSY_BLACKNPINK_DATA}"
    [[ -f "$GLASSY_BLACKNPINK_ROOT/CMakeLists.txt" ]] || {
        glassy_blacknpink_fail "The Breeze sources are missing from $GLASSY_BLACKNPINK_ROOT"
        return 1
    }
    local fingerprint installed="" previous_effect=""
    [[ ! -r "$data_dir/fingerprint" ]] || installed="$(<"$data_dir/fingerprint")"
    [[ ! -r "$data_dir/effect-id" ]] || previous_effect="$(<"$data_dir/effect-id")"
    fingerprint="$(glassy_blacknpink_fingerprint)" || {
        glassy_blacknpink_fail 'Could not read the theme sources or the installed KWin, KDecoration and Qt versions'
        return 1
    }
    if [[ "$installed" != "$fingerprint" ]]; then
        command -v makepkg >/dev/null || {
            glassy_blacknpink_fail 'makepkg is required to build Glassy Black & Pink'
            return 1
        }
        local build_dir errors
        build_dir="$(mktemp -d)" || {
            glassy_blacknpink_fail 'Could not create a temporary build folder'
            return 1
        }
        if ! (set -o pipefail
            cd "$GLASSY_BLACKNPINK_ROOT" \
            && BUILDDIR="$build_dir" PKGDEST="$build_dir" SRCDEST="$build_dir" LOGDEST="$build_dir" \
                GLASSY_BLACKNPINK_FINGERPRINT="$fingerprint" makepkg --syncdeps --noconfirm 2>&1 \
                | tee "$build_dir/build.log"); then
            glassy_blacknpink_fail "Glassy Black & Pink build failed; build files retained in $build_dir"
            errors="$(glassy_blacknpink_build_errors "$build_dir/build.log")"
            [[ -z "$errors" ]] || glassy_blacknpink_fail "$errors"
            return 1
        fi
        "${GLASSY_BLACKNPINK_ELEVATE:-pkexec}" pacman -U --noconfirm "$build_dir"/glassy-blacknpink-*.pkg.tar.zst || {
            glassy_blacknpink_fail "Installing the built package was refused or failed; it is kept in $build_dir"
            return 1
        }
        rm -rf "$build_dir"
        printf 'Installed Glassy Black & Pink; running applications use its style after they restart\n'
    fi

    local effect_id
    [[ -r "$data_dir/effect-id" ]] || {
        glassy_blacknpink_fail 'Glassy Black & Pink has no glass effect identifier'
        return 1
    }
    effect_id="$(<"$data_dir/effect-id")"
    [[ "$effect_id" == glassy_blacknpink_glass_* ]] || {
        glassy_blacknpink_fail "Invalid glass effect identifier: $effect_id"
        return 1
    }
    # KDE enables its own blur by default, and only one blur can be active.
    # Each build has its own effect name; drop the switch of the build it replaced.
    {
        kwriteconfig6 --file kwinrc --group Plugins --key blurEnabled false \
            && if [[ -n "$previous_effect" && "$previous_effect" != "$effect_id" ]]; then
                kwriteconfig6 --file kwinrc --group Plugins --key "${previous_effect}Enabled" --delete
            fi \
            && kwriteconfig6 --file kwinrc --group Plugins --key "${effect_id}Enabled" true \
            && kwriteconfig6 --file kwinrc --group Plugins --key glassy-blacknpink-panelsEnabled true
    } || {
        glassy_blacknpink_fail 'Could not turn the glass and panel script on in kwinrc'
        return 1
    }
}

glassy_blacknpink_activate() {
    local data_dir="${1:-$GLASSY_BLACKNPINK_DATA}"
    if ! qdbus6 org.kde.KWin /KWin >/dev/null 2>&1; then
        printf 'The glass will load at the next KDE Plasma login\n'
        return 0
    fi
    local effect_id loaded effect since ready=false
    [[ -r "$data_dir/effect-id" ]] || {
        glassy_blacknpink_fail 'Glassy Black & Pink has no glass effect identifier'
        return 1
    }
    effect_id="$(<"$data_dir/effect-id")"
    [[ "$effect_id" == glassy_blacknpink_glass_* ]] || {
        glassy_blacknpink_fail "Invalid glass effect identifier: $effect_id"
        return 1
    }
    loaded="$(qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.loadedEffects)" || {
        glassy_blacknpink_fail 'Could not ask KWin which effects are loaded'
        return 1
    }
    # KWin's blur capability is a boolean, not a count of loaded providers.
    # Retiring any old provider after loading the replacement disables it for clients.
    while IFS= read -r effect; do
        case "$effect" in
            blur)
                qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.unloadEffect blur \
                    && kwriteconfig6 --file kwinrc --group Plugins --key blurEnabled false || {
                    glassy_blacknpink_fail "Could not turn off KDE's standard blur"
                    return 1
                }
                ;;
            glassy_blacknpink_glass_*)
                qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.unloadEffect "$effect" || {
                    glassy_blacknpink_fail "Could not unload the earlier glass $effect from KWin"
                    return 1
                }
                [[ "$effect" == "$effect_id" ]] && continue
                kwriteconfig6 --file kwinrc --group Plugins --key "${effect}Enabled" --delete || {
                    glassy_blacknpink_fail "Could not remove the switch of the earlier glass $effect from kwinrc"
                    return 1
                }
                ;;
        esac
    done <<<"$loaded"
    since="$(date '+%Y-%m-%d %H:%M:%S')"
    if [[ "$(qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.loadEffect "$effect_id")" == true ]]; then
        if qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.reconfigureEffect "$effect_id" \
            && [[ "$(qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.debug "$effect_id" '')" == valid=1 ]]; then
            ready=true
        fi
    fi
    if [[ "$ready" != true ]]; then
        qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.unloadEffect "$effect_id" || true
        kwriteconfig6 --file kwinrc --group Plugins --key "${effect_id}Enabled" false
        if [[ "$(qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.loadEffect blur)" == true ]]; then
            kwriteconfig6 --file kwinrc --group Plugins --key blurEnabled true
            printf "Restored KDE's standard blur after the glass failed to initialize\n"
        fi
        glassy_blacknpink_fail "KWin could not initialize the glass: $effect_id"
        # KWin logs why it refused the plugin, and the glass logs what it could not set up.
        local reasons
        reasons="$({
            journalctl --user --unit plasma-kwin_wayland --since "$since" --output cat QT_CATEGORY=glassy_blacknpink_glass
            journalctl --user --unit plasma-kwin_wayland --since "$since" --output cat QT_CATEGORY=kwin_core | grep -F "$effect_id"
        } 2>/dev/null | awk '!seen[$0]++')"
        [[ -z "$reasons" ]] || glassy_blacknpink_fail "$reasons"
        return 1
    fi
}

glassy_blacknpink_main() {
    local status=0
    glassy_blacknpink_install || status=1
    if glassy_blacknpink_activate; then
        if qdbus6 org.kde.KWin /KWin >/dev/null 2>&1; then
            qdbus6 org.kde.KWin /KWin org.kde.KWin.reconfigure
            # Reloading the KWin effect temporarily removes the advertised blur
            # capability. Existing Wayland clients then lose their blur regions
            # and do not submit them again when the replacement loads. Restart
            # Plasma so the panels register their regions against the new effect.
            systemctl --user try-restart plasma-plasmashell.service
        fi
    else
        status=1
    fi
    return "$status"
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    glassy_blacknpink_main
fi
