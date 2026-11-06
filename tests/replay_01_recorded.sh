#!/usr/bin/env bash
# Replay 01 against the stored MesenCE run (tests/recorded/): WRAM, VRAM, CGRAM and OAM at every 60th NMI, no MesenCE needed.
#
#   tests/replay_01_recorded.sh <game executable> <rom>     (ctest: -DSOE_TEST_ROM=/path/to/rom.sfc)
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
exec node "$ROOT/tools/compare/compare.mts" "recomp:$1=$2" --rom "$2" \
    --replay "$ROOT/tools/compare/replays/01-no-inputs-4m.evsmv" --regions wram,vram,cgram,oam \
    --reference "$ROOT/tests/recorded/01-no-inputs-4m.crc" --out "${TMPDIR:-/tmp}/soe-replay-01.md"
