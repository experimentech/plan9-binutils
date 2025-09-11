#!/usr/bin/env bash
set -euo pipefail

# Warn about local modifications to generated Autotools/Automake files.
# This helps catch accidental edits that will be lost on autoreconf/distclean.

repo_root=$(cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"

patterns=(
  '(^|/)configure$'
  '(^|/)aclocal\.m4$'
  '(^|/)Makefile\.in$'
  '\\.gmo$'
  '\\.info$'
  '(^|/)config\.h(\.in)?$'
  '(^|/)config\.in$'
  '(^|/)config\.log$'
  '(^|/)config\.status$'
  '(^|/)stamp-h(1)?$'
  '(^|/)libtool$'
  '(^|/)ltmain\.sh$'
  '(^|/)mdate-sh$'
  '(^|/)ylwrap$'
)

regex=$(IFS='|'; echo "${patterns[*]}")

mods=$(git ls-files -m | grep -E "$regex" || true)

if [[ -z "$mods" ]]; then
  echo "No local edits detected in generated files."
  exit 0
fi

echo "WARNING: Local edits to generated files detected (these will be overwritten by autoreconf/clean):"
echo "$mods" | sed -n '1,200p'
echo
echo "Suggested next steps:"
echo "  - Port your changes into the corresponding *.ac/*.am or source files."
echo "  - Then re-run ./bootstrap.sh"
echo
exit 1
