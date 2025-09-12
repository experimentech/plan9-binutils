#!/usr/bin/env bash
# Analyze Plan 9 aarch64 objects and executables with our local binutils build.
# Usage: OBJDUMP=... NM=... scripts/analyze_plan9.sh file1 [file2 ...]
# Outputs <file>.report.txt alongside each input.

set -euo pipefail

# Default tool locations; allow override via env
REPO_ROOT=$(cd "$(dirname "$0")/.." && pwd)
: "${OBJDUMP:=$REPO_ROOT/build-plan9-aarch64/binutils/objdump}"
: "${NM:=$REPO_ROOT/build-plan9-aarch64/binutils/nm-new}"

if [ $# -lt 1 ]; then
  echo "Usage: $0 <file...>" >&2
  exit 2
fi

for f in "$@"; do
  if [ ! -f "$f" ]; then
    echo "Skipping missing file: $f" >&2
    continue
  fi
  out="${f}.report.txt"
  {
    echo "== $f =="
    echo "Date: $(date -Iseconds)"
    echo "Size: $(stat -c '%s bytes' "$f" 2>/dev/null || stat -f '%z bytes' "$f")"
    echo "SHA256: $(command -v sha256sum >/dev/null 2>&1 && sha256sum "$f" | awk '{print $1}' || shasum -a 256 "$f" | awk '{print $1}')"
    echo
    echo "-- objdump -f --"
    # Pick target for objects explicitly to avoid mis-detection as an executable
    if [[ "$f" == *.o ]]; then tgt_obj="-b plan9-object"; else tgt_obj=""; fi
    "$OBJDUMP" $tgt_obj -f "$f" || true
    echo
    echo "-- objdump -t (symbols) --"
    "$OBJDUMP" $tgt_obj -t -C "$f" || true
    echo
    echo "-- objdump -r (relocations) --"
    "$OBJDUMP" $tgt_obj -r "$f" || true
    echo
    echo "-- objdump -s (all sections dump) --"
    "$OBJDUMP" $tgt_obj -s "$f" | sed -n '1,400p' || true
    echo
    echo "-- objdump -d (disassembly) --"
    # Force aarch64 disassembly; our default target fallback should handle format
    "$OBJDUMP" -d -m aarch64 "$f" | sed -n '1,400p' || true
    echo
    echo "-- nm -a --"
  if [[ "$f" == *.o ]]; then nm_tgt="--target=plan9-object"; else nm_tgt=""; fi
  "$NM" $nm_tgt -a -C "$f" || true
  } >"$out"
  echo "Wrote $out"

done
