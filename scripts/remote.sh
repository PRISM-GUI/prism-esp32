#!/bin/bash
# Sync this repository to the Coder workspace `prism` and run a command there, in
# ~/prism-esp32 with ESP-IDF's environment (~/esp/esp-idf). PRISM is the workspace's ~/PRSIM
# checkout: there subprojects/prism is a symlink to it (ln -sfn ~/PRSIM
# ~/prism-esp32/subprojects/prism, once), which the sync leaves alone.
#   scripts/remote.sh 'scripts/build.sh selftest'
. ~/.config/lpp-coder/env
cd "$(dirname "$0")/.." || exit 1
timeout 600 rsync -a --delete --exclude 'build*/' --exclude .git/ --exclude /subprojects/prism \
    --exclude /managed_components/ --exclude /sdkconfig -e coder-rsh ./ prism:prism-esp32/ || exit 1
timeout 1500 coder ssh prism -- "cd ~/prism-esp32 && . ~/esp/esp-idf/export.sh > /dev/null && $1"
