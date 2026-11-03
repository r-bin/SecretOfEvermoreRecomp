#!/usr/bin/env bash
# Regenerate src/gen/*.c for Secret of Evermore from a verified ROM.
#
# Generated C is derived from copyrighted ROM data and is never committed;
# every developer regenerates from their own copy. The ROM digests come from
# rom_identity.txt, which the build and the release workflow read too, so
# there is one place to change when a revision changes.
#
# Flags:
#   --rom <path>  ROM to generate from. Defaults to SNESRECOMP_ROM, a known
#                 filename at the repo root, then the path in rom.cfg (which
#                 the setup wizard writes). The ROM does not have to live in
#                 the repo — keeping it on your own drive is the better
#                 habit, and SNESRECOMP_ROM sets it once for a shell.
#   --no-verify   skip the ROM digest check (for a revision this project has
#                 not been pinned to yet — expect the generated C to differ)
#   --cfg-roots   seed analysis from every func declaration in recomp/*.cfg
#                 (on by default: recomp/bank8x..91.cfg are the CDL seeds)
#   -h|--help     this message
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT"

VERIFY=1
CFG_ROOTS=1
ROM=""
while [ $# -gt 0 ]; do
  case "$1" in
    --rom) ROM=$2; shift 2 ;;
    --no-verify) VERIFY=0; shift ;;
    --cfg-roots) CFG_ROOTS=1; shift ;;
    -h|--help) sed -n '2,/^set -euo/p' "$0" | sed -n '/^# /p' | sed 's/^# //'; exit 0 ;;
    *) echo "regen.sh: unknown flag: $1 (try --help)" >&2; exit 2 ;;
  esac
done

SNESRECOMP_ROOT="${SNESRECOMP_ROOT:-snesrecomp}"
CLI="$SNESRECOMP_ROOT/snesrecomp_cli.py"
if [ ! -f "$CLI" ]; then
  echo "regen.sh: $CLI missing — run: git submodule update --init --recursive" >&2
  exit 1
fi

PYTHON=${PYTHON:-}
if [ -z "$PYTHON" ]; then
  # Windows App Execution Aliases can exist on PATH without running Python.
  for _snes_python in python3 python; do
    if "$_snes_python" -c 'import sys; sys.exit(sys.version_info[0] != 3)' >/dev/null 2>&1; then
      PYTHON=$_snes_python
      break
    fi
  done
fi
if [ -z "$PYTHON" ]; then
  echo "regen.sh: no python3 on PATH" >&2
  exit 1
fi

# The framework parses rom_identity.txt (the same parser CI uses), so this
# script carries no copy of a digest to forget on a revision bump.
IDENTITY="$SNESRECOMP_ROOT/tools/rom_identity.py"
EXPECTED_CRC32="${SNESRECOMP_EXPECTED_CRC32:-$("$PYTHON" "$IDENTITY" "$ROOT/rom_identity.txt" --get expected_crc32)}"
EXPECTED_SHA256="${SNESRECOMP_EXPECTED_SHA256:-$("$PYTHON" "$IDENTITY" "$ROOT/rom_identity.txt" --get expected_sha256)}"

# The framework owns the search: --rom, then SNESRECOMP_ROM, then a known
# name at the root, then rom.cfg (the path the setup wizard recorded).
RESOLVE=(resolve-rom --project-root "$ROOT"
         --name "rom.sfc" --name "secretofevermore.sfc" --name "secretofevermore.smc")
if [ -n "$ROM" ]; then RESOLVE+=(--rom "$ROM"); fi
if ! ROM=$("$PYTHON" "$CLI" "${RESOLVE[@]}"); then
  echo "regen.sh: pass --rom /path/to/rom.sfc, set SNESRECOMP_ROM, put it" >&2
  echo "          at the repo root, or record its path in rom.cfg. You must" >&2
  echo "          legally own a copy of Secret of Evermore." >&2
  exit 1
fi
ROM=${ROM%$'\r'}  # Windows Python ends lines with CRLF

VERIFY_ARGS=()
if [ "$VERIFY" -eq 1 ]; then
  VERIFY_ARGS=(--expected-crc32 "$EXPECTED_CRC32" --expected-sha256 "$EXPECTED_SHA256")
  echo "== Verifying $ROM =="
  "$PYTHON" "$CLI" verify-rom --rom "$ROM" "${VERIFY_ARGS[@]}"
else
  echo "== Skipping ROM verification (--no-verify) =="
fi

# Bus-exact compiled code (snesrecomp v2/bus_exact.py, branch aot-bus-exact): every instruction charged as the
# interpreter charges it, so native runs keep MesenCE's timing. SNESRECOMP_EXACT_BUS=0 generates the old
# block-folded charges (docs/imperfections/compiled-cycle-charges.md).
export SNESRECOMP_EXACT_BUS="${SNESRECOMP_EXACT_BUS:-1}"
# Compiled resume (snesrecomp v2, branch aot-resume): every instruction of a compiled variant is also an entry, so
# code a CPU slice stopped in goes on compiled instead of interpreted (docs/progress.md, native share).
# SNESRECOMP_AOT_RESUME=0 at regen leaves it out; =0 at run time turns it off, =2 keeps only suspended frames.
export SNESRECOMP_AOT_RESUME="${SNESRECOMP_AOT_RESUME:-1}"

GEN_ARGS=(--rom "$ROM" --cfg-dir recomp --out-dir src/gen
          --funcs-h recomp/funcs.h --project-root "$ROOT")
if [ "$CFG_ROOTS" -eq 1 ]; then GEN_ARGS+=(--cfg-roots); fi
if [ "$VERIFY" -eq 1 ]; then GEN_ARGS+=("${VERIFY_ARGS[@]}"); fi

echo "== Generating src/gen =="
"$PYTHON" "$CLI" generate "${GEN_ARGS[@]}"

# Comments from everscript-vscode's ROM and WRAM maps: before each compiled instruction its assembly with
# names, before each function its routine (docs/game-rtl.md, "Names in the generated C"). Only comment lines
# are added. The table comes from `npm run recomp` there; without it the C stays as generated.
EVERSCRIPT="${EVERSCRIPT:-$ROOT/../everscript-vscode}"
ANNOTATIONS="$EVERSCRIPT/out/recomp/asar/recomp/annotations.tsv"
if [ -f "$ANNOTATIONS" ] && [ -f "$EVERSCRIPT/dist/platform/snes/hirom.js" ] && command -v node >/dev/null 2>&1; then
  echo "== Annotating src/gen from $ANNOTATIONS =="
  node "$EVERSCRIPT/tools/recomp/annotate.mts" src/gen "$ANNOTATIONS"
else
  echo "== No annotations (run npm run recomp -- --no-build in $EVERSCRIPT for names in the C) =="
fi

echo
echo "Done. Build with:"
echo "  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j"
