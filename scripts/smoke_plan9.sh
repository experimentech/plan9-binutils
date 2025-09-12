#!/usr/bin/env bash
# Simple smoke tests for Plan 9 AArch64 support using known-good 9front samples.
# Exits non-zero on failure. Uses local build by default.

set -euo pipefail

REPO_ROOT=$(cd "$(dirname "$0")/.." && pwd)
: "${OBJDUMP:=$REPO_ROOT/build-plan9-aarch64/binutils/objdump}"

fail() { echo "[FAIL] $*" >&2; exit 1; }
pass() { echo "[PASS] $*"; }

# Inputs
HELLO=$REPO_ROOT/../arm64_9front_samples/hello_print/7.out
SYSCALLS=$REPO_ROOT/../arm64_9front_samples/syscalls/7.out
OBJTEST=$REPO_ROOT/../arm64_9front_samples/test/exit_msg_test.o

[ -x "$OBJDUMP" ] || fail "objdump not found at $OBJDUMP"
[ -f "$HELLO" ] || fail "missing $HELLO"
[ -f "$SYSCALLS" ] || fail "missing $SYSCALLS"

# 1) hello_print 7.out basic checks
hdr=$($OBJDUMP -f "$HELLO")
[[ "$hdr" == *"file format plan9-arm64"* ]] || fail "hello_print: wrong file format"
[[ "$hdr" == *"architecture: aarch64"* ]] || fail "hello_print: wrong architecture"
# Disassembly should run and contain some AArch64 opcodes
$OBJDUMP -d "$HELLO" | sed -n '1,40p' | grep -q "Disassembly of section .text:" || fail "hello_print: disassembly missing"
pass "hello_print: format/arch/disassembly"

# 2) syscalls 7.out should contain the message bytes in .data; scan raw binary
grep -a -q "syscalls: write + exits on 9front aarch64" "$SYSCALLS" || fail "syscalls: missing expected message in binary"
pass "syscalls: expected message present in binary"

# 3) optional object .o contains test string in .text
if [ -f "$OBJTEST" ]; then
  grep -a -q "test worked!" "$OBJTEST" || fail "object: missing 'test worked!' in binary"
  pass "object: expected string present in .text"
fi

echo "All smoke tests passed."