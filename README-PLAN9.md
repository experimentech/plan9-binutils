# Plan 9 Binutils Support

This is GNU Binutils with added support for Plan 9 object files and executables.

## Plan 9 Architectures Supported

- **amd64** (x86_64) - Plan 9 AMD64
- **386** (i386) - Plan 9 386
- **arm** - Plan 9 ARM
- **arm64** (aarch64) - Plan 9 ARM64
- **power** - Plan 9 PowerPC
- **power64** - Plan 9 PowerPC 64-bit

## Current status (Plan 9 ARM64)

- Readers (BFD): Plan 9 executables and objects are recognized as `plan9-arm64`.
- Tools: `objdump`, `nm`, `strings`, etc. work on Plan 9 AArch64 executables (sections, symbols, disassembly).
- Assembler (gas): AArch64 Plan 9 build compiles with non-ELF/COFF code paths guarded.
- Writer paths: minimal object writer stubs exist (ongoing work; not a focus of this snapshot).

Other Plan 9 architectures may be wired in similarly, but ARM64 is the focus of the current work.

## Building (out-of-tree)

Recommended out-of-tree build for Plan 9 ARM64 tools (binutils only):

```bash
mkdir -p build-plan9-aarch64
cd build-plan9-aarch64
../configure \
	--target=aarch64-unknown-plan9 \
	--disable-nls \
	--disable-werror

make -j$(nproc) all-binutils
```

Key outputs will be under `build-plan9-aarch64/binutils/` (e.g. `objdump`, `nm-new`).

## Usage Examples

```bash
# Disassemble a Plan 9 ARM64 executable using the freshly built tools
build-plan9-aarch64/binutils/objdump -f -h -d ../arm64_9front_samples/arm64_executables/catclock

# List symbols with nm
build-plan9-aarch64/binutils/nm-new -n ../arm64_9front_samples/arm64_executables/catclock | head
```

## Plan 9 File Extensions

- `.7` - ARM64 object files
- `.7.out` - ARM64 executables
- `.8` - 386 object files  
- `.8.out` - 386 executables
- `.q` - PowerPC object files
- `.q.out` - PowerPC executables

## Technical Details

The Plan 9 support is implemented through:

- BFD backend (`bfd/bfd-plan9.c`)
- GAS object format support (configure selects fmt=plan9 for *-*-plan9* targets)
- Linker emulation scripts
- Target-specific configurations

## Notes

- The build is fully automated. You no longer need to patch generated files.
- Legacy helper scripts that edited generated files have been deprecated.

## Smoke tests

Two helper scripts live under `tools/`:

- `tools/smoke_plan9_binutils.sh` — quick check against a small set of samples (headers, sections, nm symbols). Good for fast sanity.
- `tools/smoke_plan9_scan.sh` — broader scan across a samples directory.

Defaults assume:

- Build dir: `build-plan9-aarch64/binutils`
- Sample executables: `../arm64_9front_samples/arm64_executables`

Run a quick check:

```bash
tools/smoke_plan9_binutils.sh
```

Or scan all executables in the default samples dir:

```bash
tools/smoke_plan9_scan.sh
```

## Documentation

- docs/9front-executable-files.md — 9front native executable layout, header fields, symbol blob and LC encoding.
- docs/9front-object-files.md — Plan 9 object stream format, ANAME/ASIGNAME, zaddr encoding, archives.
- docs/9front-runtime-library.md — dlm (dynamic module) import/relocation table format and decoding.
- docs/9front-syscalls.csv — syscall numbers table (CSV).
- docs/9front-syscalls.json — syscall numbers table (JSON).
- docs/9front-syscalls.md — syscall overview and references to man pages.
- docs/plan9-aarch64-analysis.md — analysis notes and sample inspection for ARM64 on 9front.

## License

Same as GNU Binutils (GPL v3+)
