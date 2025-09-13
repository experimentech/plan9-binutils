Plan 9 aarch64 (arm64) — format, tools, and observations
========================================================

This note collects observations and concrete examples from working with 9front arm64 binaries and toolchains. It focuses on the native Plan 9 object/executable formats, symbol and LC encodings, and integration into GNU binutils/BFD.

Highlights
---------
- A native 9front arm64 executable uses a Plan 9 header (magic 4*28*28+7 with fat and dlm bits as applicable), then text, data, symbol blob, and LC stream in that order. Some writers append a 64-bit entry (llput) after lcsize in the header region.
- Symbols are encoded by `putsymb()` with value (32 or 64-bit via hi/lo words), a type-byte (letter+0x80), then a name string (or special z/Z two-byte encoding for path/history entries).
- LC stream is a small bytecode with pc step opcodes (>=129 add multiples of MINLC) and line delta opcodes (1..64 and 65..128 for +/- small deltas; 0 then s32 for large deltas).
- Dynamic modules set the top bit of the magic (dlm) and include relocation tables emitted by `asmdyn()` following LC.

Binutils/BFD integration
------------------------
- A dedicated BFD backend recognizes the Plan 9 arm64 format ("plan9-arm64") and synthesizes .text/.data/.bss sections based on the header sizes. It can canonicalize symbols of types T/D/B/L and exposes them to `objdump` and `nm`.
- Disassembly via aarch64 opcodes works once the file is recognized and sections are defined; `objdump -d` shows correct instructions for samples such as `catclock`.
- The backend decodes the LC stream length to allow for future line info integration. For now it ignores LC when dumping.

Assembler (gas) notes
---------------------
- Building gas for aarch64-unknown-plan9 required guarding ELF/COFF/DWARF-only code paths and excluding incompatible stubs. With these guards, gas compiles for the Plan 9 target.

Testing and samples
-------------------
- Smoke scripts exercise `objdump` and `nm` against real 9front arm64 executables. They assert presence of sections (.text/.data/.bss), proper file format string, and visible symbols of expected kinds.

Open items
----------
- Expand symbol kind coverage and integrate LC decoding into `objdump --line-numbers` for Plan 9 files.
- Parse and expose relocation info for dlm modules (`asmdyn()` tables) in BFD.
- Add convenience Makefile targets to run smoke tests.
