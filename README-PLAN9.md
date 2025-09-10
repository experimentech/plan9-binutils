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

Standard GNU Binutils build process:

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
- GAS object format support
- Linker emulation scripts
- Target-specific configurations

## License

Same as GNU Binutils (GPL v3+)
