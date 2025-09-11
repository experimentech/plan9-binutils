#!/usr/bin/env bash
set -euo pipefail

# Simple, reproducible bootstrap for this binutils tree.
# - Regenerates autotools in subdirs
# - Configures out-of-tree by default (./build)
# - Builds and optionally installs to DESTDIR

root_dir=$(cd "$(dirname "$0")" && pwd)
build_dir=${BUILD_DIR:-"$root_dir/build"}
prefix=${PREFIX:-/usr/local}
destdir=${DESTDIR:-}
jobs=${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 1)}
target=${TARGET:-}

echo "==> Bootstrapping in $root_dir"
mkdir -p "$build_dir"

pushd "$root_dir" >/dev/null

# Pre-clean any previously in-source configured subtrees to allow out-of-tree builds
subdirs=( . bfd opcodes libctf gas binutils ld gprof gprofng libsframe zlib etc )
for d in "${subdirs[@]}"; do
  if [[ -d "$d" ]]; then
    if [[ -f "$d/Makefile" || -f "$d/config.status" || -f "$d/config.cache" || -d "$d/autom4te.cache" ]]; then
      echo "==> pre-clean in $d"
      (
        cd "$d"
        # Try standard distclean first if Makefile exists
        if [[ -f Makefile ]]; then
          make distclean || true
        fi
        # Remove common Autotools remnants that block out-of-tree configure
        rm -f config.status config.cache config.log libtool stamp-h stamp-h1
        rm -rf autom4te.cache .deps .libs
      )
    fi
  fi
done

# Regenerate top-level and all relevant subprojects
for d in "${subdirs[@]}"; do
  if [[ -f "$d/configure.ac" ]]; then
    echo "==> autoreconf -fi in $d"
    (
      cd "$d" && \
      AUTOCONF=autoconf2.69 \
      AUTOHEADER=autoheader2.69 \
      AUTOM4TE=autom4te2.69 \
      AUTORECONF=autoreconf2.69 \
      ACLOCAL_PATH="$root_dir:$root_dir/config:$root_dir/bfd" \
      ACLOCAL="aclocal -I $root_dir -I $root_dir/config -I $root_dir/bfd" \
      autoreconf -fi
    )
  fi
done

popd >/dev/null

echo "==> Configuring in $build_dir"
cd "$build_dir"
cfg_cmd=("$root_dir/configure"
  --enable-ld \
  --enable-gold=no \
  --enable-gprofng \
  --with-zstd \
  --enable-shared \
  --enable-targets=${ENABLE_TARGETS:-all} \
  --prefix="$prefix")

if [[ -n "$target" ]]; then
  echo "==> Using target: $target"
  cfg_cmd+=(--target="$target")
fi

if [[ -n "${CONFIGURE_FLAGS:-}" ]]; then
  # shellcheck disable=SC2206
  extra=( ${CONFIGURE_FLAGS} )
  cfg_cmd+=("${extra[@]}")
fi

"${cfg_cmd[@]}"

echo "==> Building (jobs=$jobs)"
make -j"$jobs" V=1

if [[ -n "$destdir" ]]; then
  echo "==> Installing into DESTDIR=$destdir"
  make -j1 install V=1 DESTDIR="$destdir"
fi

echo "==> Done"
