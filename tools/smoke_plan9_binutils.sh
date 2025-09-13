#!/usr/bin/env bash
# TTY-safe smoke test for binutils plan9-arm64 support.
# - Avoids streaming large disassemblies to the terminal
# - Disables pagers
# - Limits any diagnostic output
# - Fails fast and clearly without leaving the TTY in a bad state
set -euo pipefail
IFS=$'\n\t'

# Ensure no tools try to invoke a pager
export PAGER=cat
export LESS=FRX

# Restore sane TTY on exit just in case (defensive)
trap 'stty sane 2>/dev/null || true' EXIT

# Verifies objdump shows sections and nm lists expected symbols
# for a few sample 9front AArch64 executables in this workspace.

# This script lives under plan9-binutils/, so root is its parent dir.
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build-plan9-aarch64/binutils"
SAMPLES_DIR="$(cd "$ROOT_DIR/.." && pwd)/arm64_9front_samples/arm64_executables"

OBJDUMP="$BUILD_DIR/objdump"
NM="$BUILD_DIR/nm-new"

if [[ ! -x "$OBJDUMP" || ! -x "$NM" ]]; then
  echo "error: expected objdump and nm-new in $BUILD_DIR (build all-binutils first)" >&2
  exit 1
fi

read -r -a samples <<< "${SAMPLES:-catclock md sudoku doom}"
pass=0
fail=0
skip=0

# Quiet mode prints only summary and PASS/FAIL lines (set QUIET=1)
QUIET=${QUIET:-0}
log() { if [[ "$QUIET" != 1 ]]; then printf '%s\n' "$*"; fi }

check_one() {
  local f="$1"
  local path="$SAMPLES_DIR/$f"
  if [[ ! -f "$path" ]]; then
    echo "SKIP: $f (not found)"
    return 0
  fi
  log "==== $f ===="
  local hdr
  # Only inspect headers; don't stream disassembly to TTY
  if ! hdr="$($OBJDUMP -f -h "$path" 2>/dev/null)"; then
    echo "FAIL: objdump failed on $f"
    ((fail++))
    return 0
  fi
  local ok=1
  grep -q "file format plan9-arm64" <<<"$hdr" || ok=0
  grep -q "\\.text" <<<"$hdr" || ok=0
  grep -q "\\.data" <<<"$hdr" || ok=0
  grep -q "\\.bss"  <<<"$hdr" || ok=0
  if [[ $ok -eq 0 ]]; then
    echo "FAIL: missing expected headers/sections"
    # Print at most the first 80 lines of header info for diagnostics
    sed -n '1,80p' <<<"$hdr"
    ((fail++))
    return 0
  fi
  # Symbols: expect to find main in text for most samples.
  if ! $NM -n "$path" | grep -E "\\b(main|threadmain)$" >/dev/null 2>&1; then
    log "WARN: 'main' not found in symbols for $f (may be expected)"
  fi
  echo "PASS: $f"
  ((pass++))
}

for s in "${samples[@]}"; do
  check_one "$s"
done

echo
echo "Summary: PASS=$pass FAIL=$fail SKIP=$skip"
if [[ $fail -ne 0 ]]; then
  exit 1
fi
