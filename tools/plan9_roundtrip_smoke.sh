#!/usr/bin/env bash
set -euo pipefail

# smoke test: assemble + link a minimal AArch64 program and verify it's a Plan 9 executable
# Default BUILD_DIR to the repository root's build-plan9-aarch64 relative to this script.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR=${BUILD_DIR:-"$SCRIPT_DIR/build-plan9-aarch64"}
BINUTILS_DIR="$BUILD_DIR/binutils"
GAS_BIN="$BUILD_DIR/gas/as-new"
LD_BIN="$BUILD_DIR/ld/ld-new"
OBJDUMP_BIN="$BINUTILS_DIR/objdump"
NM_BIN="$BINUTILS_DIR/nm-new"

if [ ! -x "$GAS_BIN" ]; then echo "Missing assembler: $GAS_BIN" >&2; exit 2; fi
if [ ! -x "$LD_BIN" ]; then echo "Missing linker: $LD_BIN" >&2; exit 2; fi
if [ ! -x "$OBJDUMP_BIN" ]; then echo "Missing objdump: $OBJDUMP_BIN" >&2; exit 2; fi
if [ ! -x "$NM_BIN" ]; then echo "Missing nm: $NM_BIN" >&2; exit 2; fi

TMPDIR=$(mktemp -d /tmp/plan9-roundtrip.XXXXXX)
trap 'rm -rf "$TMPDIR"' EXIT
ASM=$TMPDIR/test.s
OBJ=$TMPDIR/test.o
EXE=$TMPDIR/test.exe

cat > "$ASM" <<'EOF'
    .text
    .global main
main:
    mov x0, #0
    ret
EOF

echo "assembling -> $OBJ"
"$GAS_BIN" -o "$OBJ" "$ASM"

echo "linking -> $EXE"
"$LD_BIN" -o "$EXE" "$OBJ"

echo "verifying with objdump"
OD_OUT=$(mktemp)
"$OBJDUMP_BIN" -t "$EXE" > "$OD_OUT" 2>&1
if ! grep -q 'file format plan9' "$OD_OUT"; then
    echo "objdump did not report Plan 9 output format" >&2
    cat "$OD_OUT" >&2
    exit 3
fi
if ! grep -q '\bmain\b' "$OD_OUT"; then
    echo "objdump did not list 'main' symbol" >&2
    cat "$OD_OUT" >&2
    exit 4
fi

echo "verifying with nm"
NM_OUT=$(mktemp)
"$NM_BIN" -a "$EXE" > "$NM_OUT" 2>&1
if ! grep -q '\bmain\b' "$NM_OUT"; then
    echo "nm did not list 'main'" >&2
    cat "$NM_OUT" >&2
    exit 5
fi

printf "SMOKE PASS: assembled and linked Plan 9 executable at %s\n" "$EXE"
exit 0
