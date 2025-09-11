#!/usr/bin/env bash
set -euo pipefail

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
mapfile -t exe_samples < <(find "$samples_dir" -type f -perm -u+x -print 2>/dev/null | sort)
# Plan 9 object files: include *.7 (arm64), plus common *.o/*.obj just in case
mapfile -t obj_samples < <(find "$samples_dir" -type f \( -name '*.7' -o -name '*.o' -o -name '*.obj' \) -print 2>/dev/null | sort)

echo "==> Found ${#exe_samples[@]} executables and ${#obj_samples[@]} object files"

test_objdump_read() {
  local f="$1"; local kind="$2"
  if "$objdump_bin" -f -- "$f" >"$build_dir/.smoke.tmp" 2>&1; then
    if grep -qi 'file format not recognized' "$build_dir/.smoke.tmp"; then
      echo "FAIL: objdump cannot read $kind $f (format not recognized)"
      ((fail++))
      return 1
    else
      # Try to surface detected format
      local fmt
      fmt=$(sed -n '1,4p' "$build_dir/.smoke.tmp" | sed -n 's/^.*file format \(.*\)$/\1/p' | head -n1 || true)
      if [[ -n "${fmt:-}" ]]; then
        echo "PASS: objdump reads $kind $f (format: $fmt)"
      else
        echo "PASS: objdump reads $kind $f"
      fi
      ((pass++))
      return 0
    fi
  else
    echo "FAIL: objdump errored on $kind $f"
    sed -n '1,4p' "$build_dir/.smoke.tmp" || true
    ((fail++))
    return 1
  fi
}

test_nm() {
  local f="$1"; local kind="$2"
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
  if "$strings_bin" -- "$f" >/dev/null 2>&1; then
    echo "PASS: strings runs on $kind $f"
    ((pass++))
  else
    echo "FAIL: strings errored on $kind $f"
    ((fail++))
  fi
}

# Execute tests on executables
for f in "${exe_samples[@]}"; do
  test_objdump_read "$f" exe
  test_nm "$f" exe
  test_strings "$f" exe
done

# Execute tests on object files
for f in "${obj_samples[@]}"; do
  test_objdump_read "$f" obj
  test_nm "$f" obj
done

echo "==> Smoke summary: PASS=$pass FAIL=$fail"
[[ $fail -eq 0 ]] || exit 3
exit 0
