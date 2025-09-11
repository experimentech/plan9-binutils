Binutils (Plan 9 enabled) — building

This tree is wired for a reproducible, out-of-tree build using Autoconf 2.69. Use the provided bootstrap helper; it regenerates autotools in each subproject, configures into ./build, builds, and can optionally install into a DESTDIR.

Quick start

- Prereqs: autoconf2.69, automake (>=1.16), libtool, gcc/g++, zlib, zstd.
- From repo root:
  - ./bootstrap.sh
  - Results land under build/, e.g. build/gas/as-new, build/binutils/objdump, build/ld/ld-new, build/gprofng/src/gprofng

Options (env vars)

- BUILD_DIR: where to configure/build (default: ./build)
- PREFIX: installation prefix for configure (default: /usr/local)
- DESTDIR: if set, bootstrap will run “make install DESTDIR=$DESTDIR” after the build
- JOBS: parallelism for make (default: nproc)
- CONFIGURE_FLAGS: extra flags passed to top-level configure

Notes

- Autoconf is pinned to 2.69 on purpose; newer versions can work with further retooling but aren’t required.
- The script pre-cleans any in-tree configure artifacts to ensure out-of-tree builds don’t conflict.
- Plan 9 support is integrated in BFD and GAS. libbfd always contains plan9_object_vec so GAS can link unconditionally.
- gprofng executables link with libiberty explicitly and guard against --as-needed dropping transitive DSOs.

Generated files

Do not commit generated outputs (configure, Makefile.in, .gmo, etc.). If you ran bootstrap locally, you may see these as modified; they should be ignored in version control. See .gitignore for the patterns we exclude.
# Building plan9-binutils

This repo uses a uniform Autotools/Libtool setup across subprojects (Autoconf 2.69, Automake 1.16.x, Libtool 2.4.7). A helper script bootstraps, configures, builds, and optionally installs.

## Quick start

- Native build with an out-of-tree build dir at ./build and install into a DESTDIR:

```bash
# Optional: set DESTDIR to stage files, PREFIX to change install prefix
export DESTDIR=/tmp/p9b-out
export PREFIX=/usr/local

# Run the bootstrap script from repo root
./bootstrap.sh
```

- To just configure/build without installing:

```bash
unset DESTDIR
./bootstrap.sh
```

## Options

Environment variables recognized by bootstrap.sh:
- PREFIX: installation prefix (default /usr/local)
- DESTDIR: staging directory for make install (optional)
- BUILD_DIR: out-of-tree build directory (default ./build)
- JOBS: make -j parallelism (defaults to nproc)
- CONFIGURE_FLAGS: extra flags forwarded to the top-level configure

Examples:
- Disable gprofng: `CONFIGURE_FLAGS="--disable-gprofng" ./bootstrap.sh`
- Use system zlib: `CONFIGURE_FLAGS="--with-system-zlib" ./bootstrap.sh`

## Notes

- The script re-runs autoreconf -fi in: ., bfd, opcodes, libctf, gas, binutils, ld, gprof, gprofng, libsframe, zlib, etc.
- We pin Autoconf to autoconf2.69 via AUTOCONF=autoconf2.69 to avoid version drift.
- If a subproject lacks configure.ac, it is skipped.

## Plan 9 smoke tests

After a build, you can run basic read tests of sample 9front arm64 files. Point the script at your samples directory (by default it looks for `../arm64_9front_samples` relative to the repo):

```bash
# From repo root, after ./bootstrap.sh
SAMPLES_DIR=../arm64_9front_samples ./scripts/plan9-smoke.sh
```

This exercises `objdump`, `nm`, and `strings` against the sample executables and object files. It exits non-zero on failure.

## Troubleshooting

- Missing m4 macros: ensure Automake and Libtool dev packages are installed. The subprojects use `AC_CONFIG_AUX_DIR([..])` and `AC_CONFIG_MACRO_DIRS([..])` to find shared helpers in the repo root.
- If make install fails in po/ due to mkinstalldirs, re-run `./bootstrap.sh` to refresh generated files; the build sets MKINSTALLDIRS to the parent’s mkinstalldirs.
- If you edited generated files (configure, Makefile.in, aclocal.m4, config.h[.in], *.gmo, *.info) by hand, port those changes into the corresponding `*.ac`/`*.am` or source files instead, then re-run `./bootstrap.sh`. Generated files are ignored by git per `.gitignore` to prevent accidental commits.
