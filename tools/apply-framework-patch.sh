#!/usr/bin/env bash
# Bring the snesrecomp submodule (upstream, at the pinned commit) to what this port runs on: framework/snesrecomp.patch.
# Run by tools/regen.sh; safe to run again. An external SNESRECOMP_ROOT is taken as it is. docs/framework.md.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PATCH="$ROOT/framework/snesrecomp.patch"
FRAMEWORK="${SNESRECOMP_ROOT:-snesrecomp}"
case "$FRAMEWORK" in /*) ;; *) FRAMEWORK="$ROOT/$FRAMEWORK" ;; esac

if [ "$FRAMEWORK" != "$ROOT/snesrecomp" ]; then
  echo "== Framework from $FRAMEWORK, not patched =="
elif git -C "$FRAMEWORK" apply --reverse --check "$PATCH" 2>/dev/null; then
  echo "== Framework patch already applied =="
elif git -C "$FRAMEWORK" apply --check "$PATCH" 2>/dev/null; then
  git -C "$FRAMEWORK" apply "$PATCH"
  echo "== Framework patch applied =="
else
  echo "apply-framework-patch: snesrecomp is at neither the pinned commit nor the patched state." >&2
  echo "  Run: git submodule update --init --recursive --force snesrecomp, then git -C snesrecomp clean -fd" >&2
  exit 1
fi
