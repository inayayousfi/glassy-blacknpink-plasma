#!/usr/bin/env bash
# Runs the tests that need no installed build. The glass tests in
# test-glass-*.py need the installed package and are run by hand.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
bash "$repo_root/test/test-theme.sh"
bash "$repo_root/test/test-install.sh"
node "$repo_root/test/test-layout.js" \
    "$repo_root/usr/share/plasma/look-and-feel/glassy-blacknpink.desktop/contents/layouts/org.kde.plasma.desktop-layout.js"
node "$repo_root/test/test-panels.js" \
    "$repo_root/usr/share/kwin/scripts/glassy-blacknpink-panels/contents/code/main.js"
printf '%s\n' 'All Glassy Black & Pink tests passed.'
