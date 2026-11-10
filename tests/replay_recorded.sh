#!/usr/bin/env bash
# A committed replay against its stored MesenCE run (tests/recorded/): WRAM, VRAM, CGRAM and OAM at every 60th NMI from
# power-on, no MesenCE needed. Every byte counts; nothing is copied in or masked.
#
#   tests/replay_recorded.sh <game executable> <rom> <replay name>     (ctest: -DSOE_TEST_ROM=/path/to/rom.sfc)
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
# The launcher's mod selection (build/mods/preloaded/state.toml) is shared with a game the user may be playing: widescreen
# experiment E6 and the practice hotkeys change the game by design, so the stored runs are checked with them off whatever
# is selected.
export SOE_WS_ALIVE=0 SOE_CHEATS=0
exec node "$ROOT/tools/compare/compare.mts" "recomp:$1=$2" --rom "$2" \
    --replay "$ROOT/tools/compare/replays/$3.mmo" --regions wram,vram,cgram,oam \
    --reference "$ROOT/tests/recorded/$3.crc" --out "${TMPDIR:-/tmp}/soe-replay-$3.md"
