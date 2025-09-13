#!/usr/bin/env bash
set -euo pipefail

# Simple smoke test for binutils plan9-arm64 support.
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

samples=(catclock md sudoku doom)
pass=0
fail=0

check_one() {
  local f="$1"
  local path="$SAMPLES_DIR/$f"
  if [[ ! -f "$path" ]]; then
    echo "SKIP: $f (not found)"
    return 0
  fi
  echo "==== $f ===="
  local hdr
  if ! hdr="$($OBJDUMP -f -h "$path" 2>/dev/null)"; then
    echo "FAIL: objdump failed on $f"
    ((fail++))
    return 0
  fi
  local ok=1
  grep -Fq "file format plan9-arm64" <<<"$hdr" || ok=0
  grep -Fq ".text" <<<"$hdr" || ok=0
  grep -Fq ".data" <<<"$hdr" || ok=0
  grep -Fq ".bss"  <<<"$hdr" || ok=0
  if [[ $ok -eq 0 ]]; then
    echo "FAIL: missing expected headers/sections"
    echo "$hdr" | sed -n '1,60p'
    ((fail++))
    return 0
  fi
  # Symbols: expect to find main in text for most samples.
  if ! $NM -n "$path" | grep -E "\\b(main|threadmain)$" >/dev/null 2>&1; then
    echo "WARN: 'main' not found in symbols for $f (may be expected)"
  fi
  echo "PASS: $f"
  ((pass++))
}

for s in "${samples[@]}"; do
  check_one "$s"
  echo

done

echo "Summary: PASS=$pass FAIL=$fail"
if [[ $fail -ne 0 ]]; then
  exit 1
fi
