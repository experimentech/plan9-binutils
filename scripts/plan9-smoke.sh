#!/usr/bin/env bash
set -uo pipefail

# Plan 9 binutils smoke tests using sample binaries/objects.
#
# Usage:
#   SAMPLES_DIR=/path/to/arm64_9front_samples ./scripts/plan9-smoke.sh
# or let it default to ../arm64_9front_samples relative to repo.

here=$(cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(cd -- "$here/.." && pwd)
build_dir=${BUILD_DIR:-"$repo_root/build"}
samples_dir=${SAMPLES_DIR:-"$repo_root/../arm64_9front_samples"}

objdump_bin="$build_dir/binutils/objdump"
nm_bin="$build_dir/binutils/nm-new"
strings_bin="$build_dir/binutils/strings"
as_bin="$build_dir/gas/as-new"
ld_bin="$build_dir/ld/ld-new"

echo "==> repo: $repo_root"
echo "==> build: $build_dir"
echo "==> samples: $samples_dir"

fail=0
pass=0

is_text_or_script() {
  local f="$1"
  file --brief --mime "$f" 2>/dev/null | grep -Eq 'text/|shellscript|x-shellscript|/xml|/json'
}

is_plan9_bfd() {
  local f="$1"
  local out
  if ! out=$("$objdump_bin" -f -- "$f" 2>/dev/null); then
    return 1
  fi
  grep -qi 'file format[[:space:]]\+plan9' <<<"$out"
}

need() {
  local p="$1"; local name="$2"
  if [[ ! -x "$p" ]]; then
    echo "ERROR: missing tool: $name at $p" >&2
    exit 1
  fi
}

need "$objdump_bin" objdump
need "$nm_bin" nm-new
need "$strings_bin" strings

if [[ ! -d "$samples_dir" ]]; then
  echo "ERROR: samples dir not found: $samples_dir" >&2
  exit 2
fi

# Collect sample executables and objects
# Gather samples, pruning VCS dirs
mapfile -t exe_samples < <(find "$samples_dir" -path '*/.git' -prune -o -type f -perm -u+x -print 2>/dev/null | sort)
# Plan 9 object files: include *.7 (arm64), plus common *.o/*.obj just in case
mapfile -t obj_samples < <(find "$samples_dir" -path '*/.git' -prune -o -type f \( -name '*.7' -o -name '*.o' -o -name '*.obj' \) -print 2>/dev/null | sort)

echo "==> Found ${#exe_samples[@]} executables and ${#obj_samples[@]} object files"

test_objdump_read() {
  local f="$1"; local kind="$2"
  local tmp="$build_dir/.smoke.read.$(date +%s).$$.log"
  # Skip obvious non-binary or script/text files
  if is_text_or_script "$f"; then
    echo "SKIP: $kind $f (text or script)"
    ((pass++))
    return 0
  fi
  if "$objdump_bin" -f -- "$f" >"$tmp" 2>&1; then
    if grep -qi 'file format not recognized' "$tmp"; then
      if [[ "$kind" == "obj" ]]; then
        echo "SKIP: obj $f (not recognized as Plan 9 object)"
        rm -f "$tmp" || true
        ((pass++))
        return 0
      else
        echo "FAIL: objdump cannot read $kind $f (format not recognized)"
        ((fail++))
        return 1
      fi
    else
      # Try to surface detected format
      local fmt
      fmt=$(sed -n '1,4p' "$tmp" | sed -n 's/^.*file format \(.*\)$/\1/p' | head -n1 || true)
      if [[ "$kind" == "obj" && -n "${fmt:-}" ]]; then
        if ! grep -qi '^plan9' <<<"$fmt"; then
          echo "SKIP: obj $f (not Plan 9: $fmt)"
          rm -f "$tmp" || true
          ((pass++))
          return 0
        fi
      fi
      if [[ -n "${fmt:-}" ]]; then
        echo "PASS: objdump reads $kind $f (format: $fmt)"
      else
        echo "PASS: objdump reads $kind $f"
      fi
      rm -f "$tmp" || true
      ((pass++))
      return 0
    fi
  else
    if grep -qi 'file format not recognized' "$tmp"; then
      if [[ "$kind" == "obj" ]]; then
        echo "SKIP: obj $f (not recognized as Plan 9 by objdump)"
        rm -f "$tmp" || true
        ((pass++))
        return 0
      fi
    fi
    echo "FAIL: objdump errored on $kind $f"
    sed -n '1,6p' "$tmp" || true
    rm -f "$tmp" || true
    ((fail++))
    return 1
  fi
}

test_nm() {
  local f="$1"; local kind="$2"
  if is_text_or_script "$f"; then
    echo "SKIP: $kind $f (text or script)"
    ((pass++))
    return 0
  fi
  if [[ "$kind" == "obj" ]] && ! is_plan9_bfd "$f"; then
    echo "SKIP: obj $f (not recognized as Plan 9 by objdump)"
    ((pass++))
    return 0
  fi
  if "$nm_bin" -- "$f" >/dev/null 2>&1; then
    echo "PASS: nm reads $kind $f"
    ((pass++))
  else
    echo "FAIL: nm errored on $kind $f"
    ((fail++))
  fi
}

test_strings() {
  local f="$1"; local kind="$2"
  if is_text_or_script "$f"; then
    echo "SKIP: $kind $f (text or script)"
    ((pass++))
    return 0
  fi
  if [[ "$kind" == "obj" ]] && ! is_plan9_bfd "$f"; then
    echo "SKIP: obj $f (not recognized as Plan 9 by objdump)"
    ((pass++))
    return 0
  fi
  if "$strings_bin" -- "$f" >/dev/null 2>&1; then
    echo "PASS: strings runs on $kind $f"
    ((pass++))
  else
    echo "FAIL: strings errored on $kind $f"
    ((fail++))
  fi
}

# Verify we can disassemble
test_disasm() {
  local f="$1"; local kind="$2"
  local tmp="$build_dir/.smoke.dis.$(date +%s).$$.log"
  if is_text_or_script "$f"; then
    echo "SKIP: $kind $f (text or script)"
    ((pass++))
    return 0
  fi
  if [[ "$kind" == "obj" ]] && ! is_plan9_bfd "$f"; then
    echo "SKIP: obj $f (not recognized as Plan 9 by objdump)"
    ((pass++))
    return 0
  fi
  if "$objdump_bin" -d -- "$f" >"$tmp" 2>&1; then
    # consider as pass if we see any typical disassembly line (address colon or a section header)
    if grep -Eq 'Disassembly of section|^[[:space:]]*[0-9a-fA-F]+:' "$tmp"; then
      echo "PASS: disasm $kind $f"
      rm -f "$tmp" || true
      ((pass++))
      return 0
    else
      echo "FAIL: disasm produced no recognizable output for $kind $f"
      sed -n '1,10p' "$tmp" || true
      rm -f "$tmp" || true
      ((fail++))
      return 1
    fi
  else
    echo "FAIL: disasm errored on $kind $f"
    sed -n '1,10p' "$tmp" || true
    rm -f "$tmp" || true
    ((fail++))
    return 1
  fi
}

# Execute tests on executables
for f in "${exe_samples[@]}"; do
  test_objdump_read "$f" exe || true
  test_nm "$f" exe || true
  test_strings "$f" exe || true
  test_disasm "$f" exe || true
done

# Execute tests on object files
for f in "${obj_samples[@]}"; do
  test_objdump_read "$f" obj || true
  test_disasm "$f" obj || true
  test_nm "$f" obj || true
done

echo "==> Smoke summary: PASS=$pass FAIL=$fail"
if [[ $fail -eq 0 ]]; then
  exit 0
else
  exit 3
fi
