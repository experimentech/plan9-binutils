#!/usr/bin/env bash
set -euo pipefail

# Broad scan over sample Plan 9 ARM64 executables using objdump/nm.
# Reports PASS/FAIL per file and a summary at the end.

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build-plan9-aarch64/binutils}"
SAMPLES_DIR="${SAMPLES_DIR:-$(cd "$ROOT_DIR/.." && pwd)/arm64_9front_samples/arm64_executables}"

OBJDUMP="$BUILD_DIR/objdump"
NM="$BUILD_DIR/nm-new"

if [[ ! -x "$OBJDUMP" || ! -x "$NM" ]]; then
  echo "error: expected objdump and nm-new in $BUILD_DIR (build all-binutils first)" >&2
  exit 1
fi

pass=0
fail=0
skip=0

shopt -s nullglob
mapfile -t files < <(find "$SAMPLES_DIR" -maxdepth 1 -type f -printf '%f\n' | sort)

for f in "${files[@]}"; do
  path="$SAMPLES_DIR/$f"
  # quick format sniff
  fmt=$($OBJDUMP -f "$path" 2>/dev/null || true)
  if ! grep -Fq "file format plan9-arm64" <<<"$fmt"; then
    echo "SKIP: $f (not plan9-arm64)"
    ((skip++))
    continue
  fi
  hdr=$($OBJDUMP -h "$path" 2>/dev/null || true)
  ok=1
  grep -Fq ".text" <<<"$hdr" || ok=0
  grep -Fq ".data" <<<"$hdr" || ok=0
  grep -Fq ".bss"  <<<"$hdr" || ok=0
  if [[ $ok -eq 0 ]]; then
    echo "FAIL: $f (missing sections)"
    ((fail++))
    continue
  fi
  if ! $NM -n "$path" >/dev/null 2>&1; then
    echo "FAIL: $f (nm failed)"
    ((fail++))
    continue
  fi
  echo "PASS: $f"
  ((pass++))
  # light throttling to keep output readable
  sleep 0.01

done

echo "Summary: PASS=$pass FAIL=$fail SKIP=$skip"
if [[ $fail -ne 0 ]]; then
  exit 1
fi
