#!/usr/bin/env sh
# Downloads nestest.nes and its golden trace log into this folder.
# These are freely distributed test ROMs; they're gitignored so the repo stays ROM-free.
set -e
cd "$(dirname "$0")"
BASE="https://raw.githubusercontent.com/christopherpow/nes-test-roms/master/other"
curl -fsSL -o nestest.nes "$BASE/nestest.nes"
curl -fsSL -o nestest.log "$BASE/nestest.log"
echo "Downloaded nestest.nes and nestest.log"
