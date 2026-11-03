#!/usr/bin/env bash
# A save state must bring the game back where it was saved, loaded mid-session and loaded before the first frame.
# Reference: power-on, slot 0 saved after frame 600 (glue SOE_SAVE_STATE_AT), dumped after frame 900. Then:
#  - mid-session: load at frame 120, run 298 more frames: the same NMI count, VRAM, CGRAM and OAM as the reference
#    (WRAM differs only in the RNG state, scratch and stack: the clocks are not saved, so the beam phase differs);
#  - at launch: load before the first frame: it must not hang (the frame driver's clock arithmetic wrapped below 0),
#    and must reach the same NMI count with the same CGRAM and OAM (a small VRAM region and lag counters may differ:
#    the first field after such a load runs on a clock the state did not have).
# Pins two bugs (docs/bugs-and-todos.md, "Save state loaded at launch hung").
#
#   tests/save_state_resume.sh <game executable> <rom>     (ctest: -DSOE_TEST_ROM=/path/to/rom.sfc)
set -euo pipefail
EXE=$1 ROM=$2
WORK=$(mktemp -d)   # short: snesrecomp keeps the save root in 96 bytes
trap 'rm -rf "$WORK"' EXIT
export SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE=/dev/null
mkdir -p "$WORK/saves"

run() {   # <name> <script> [env...]: at most 60 s
    local name=$1 script=$2; shift 2
    mkdir -p "$WORK/$name"
    printf '%b' "$script" > "$WORK/$name.txt"
    env "$@" SNESRECOMP_DUMP_DIR="$WORK/$name" SOE_COMPARE_SAVE_ROOT="$WORK/saves" \
        perl -e 'alarm 60; exec @ARGV' "$EXE" --no-launcher --script "$WORK/$name.txt" "$ROM" > "$WORK/$name.log" 2>&1 \
        || { echo "$name: the game did not finish (exit $?)"; return 1; }
}
run ref    'turbo on\nwait 900\ndump s\nquit\n' SOE_SAVE_STATE_AT=600
[ -f "$WORK/saves/save0.sav" ] || { echo "no save0.sav written"; exit 1; }
fail=0
run mid    'turbo on\nwait 120\nloadstate 0\nwait 298\ndump s\nquit\n' || fail=1
run launch 'turbo on\nloadstate 0\nwait 298\ndump s\nquit\n' || fail=1

nmi() { od -An -tu2 -j256 -N2 "$WORK/$1/s.wram.bin" | tr -d ' '; }
same() {  # <case> <regions...>
    local c=$1; shift
    [ -f "$WORK/$c/s.wram.bin" ] || { echo "$c: no dump"; fail=1; return; }
    [ "$(nmi ref)" = "$(nmi "$c")" ] || { echo "$c: NMI counter $(nmi "$c"), reference $(nmi ref)"; fail=1; }
    for r in "$@"; do cmp -s "$WORK/ref/s.$r.bin" "$WORK/$c/s.$r.bin" || { echo "$c: $r differs"; fail=1; }; done
}
same mid vram cgram oam
same launch cgram oam
[ $fail = 0 ] && echo "save states resume: mid-session and at launch, NMI $(nmi ref)"
exit $fail
