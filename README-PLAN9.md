# Plan 9 Binutils Support

This is GNU Binutils with added support for Plan 9 object files and executables.

## Plan 9 Architectures Supported

- **amd64** (x86_64) - Plan 9 AMD64
- **386** (i386) - Plan 9 386
- **arm** - Plan 9 ARM
- **arm64** (aarch64) - Plan 9 ARM64
- **power** - Plan 9 PowerPC
- **power64** - Plan 9 PowerPC 64-bit

## Features

- **objdump**: Disassemble Plan 9 executables
- **objcopy**: Convert between Plan 9 and other formats
- **gas**: Assemble for Plan 9 targets
- **ld**: Link Plan 9 objects

## Building

Automated bootstrap (preferred):

```bash
# Optionally set DESTDIR and PREFIX
export PREFIX=/usr/local
export DESTDIR=/tmp/p9b-out   # optional

./bootstrap.sh
```

Or standard GNU Binutils build process:

```bash
./configure --prefix=/usr/local
make -j$(nproc)
make install
```

## Usage Examples

```bash
# Disassemble Plan 9 ARM64 executable
objdump -d program.7.out

# Convert Plan 9 executable to ELF
objcopy -O elf64-little program.7.out program.elf

# Assemble for Plan 9 ARM64
as --64 -o program.7 program.s

# Link Plan 9 objects
ld -m plan9_arm64 -o program.7.out program.7
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

## Smoke Tests

There is a lightweight smoke test that exercises objdump/nm/strings/disassembly on a sample set of Plan 9 ARM64 binaries and objects.

Run it from the repo root:

```bash
make smoke
```

Environment variables you can override:

- `BUILD_DIR` (default: `./build`) — path to the binutils build tree with tools such as `binutils/objdump`.
- `SAMPLES_DIR` (default: `../arm64_9front_samples`) — path to sample files (executables/objects).

The script automatically skips text/scripts and non-Plan 9 object files; genuine tool issues on real Plan 9 executables will still be reported as failures.

## License

Same as GNU Binutils (GPL v3+)
