9front runtime library (dlm) — relocations and loader data
=========================================================

This note explains the runtime relocation model used by 9front’s dynamically-loadable modules (dlm) and the writer-side emission performed by the *l linkers (asmdyn/dynreloc) across the architectures. It is a companion to the executables and object-stream documents. The focus is arm64/amd64 with notes where other arches differ.

Concepts
--------
- Static native executables: contain fully resolved text/data; no relocation metadata is preserved. Addresses are absolute in the file. Reconstructing relocations from such files is generally infeasible.
- Dynamic modules (dlm): linkers set the high-bit in the Plan 9 magic and emit additional relocation tables so the loader can fix up references at runtime when the module is mapped at a load address chosen by the kernel/loader.
- Writer entry points: `datblk`/`datfill` detect relocatable references in data; `dynreloc` records a relocation entry; `asmdyn` writes the accumulated relocation records to the file after symbols and the LC stream.

Relocation entry kinds (typical set)
------------------------------------
Exact encodings are architecture-specific and implemented in each `sys/src/cmd/*l/asm.c`. Common kinds include:

- R_ADDR32 / R_ADDR64: absolute address write of a symbol’s runtime address into a 32/64-bit field.
- R_PCRELxx: PC-relative relocations used for branch/call instructions that embed offsets.
- R_GOT / R_TLS variants: present in ELF paths; Plan 9 native dlm paths mostly use direct absolute or text/data-relative forms.

When dlm is enabled, the data emission path avoids fully resolving these and instead appends relocation metadata via `dynreloc()`; the loader reads the table and applies fixups after mapping.

File layout additions in dlm mode
---------------------------------
Writers set `magic |= 0x80000000` and, after writing the symbol blob and LC stream, call `asmdyn()` to emit one or more relocation tables. The tables are placed after LC and before any trailer padding; the header does not have a dedicated `relsize` field, so the loader finds them based on internal table headers written by `asmdyn()` (see the arch’s `asm.c`).

arm64 specifics (7l)
--------------------
- The arm64 writer (7l) records relocations encountered in `datfill()` for addresses that reference symbols. It later writes a compact relocation list in `asmdyn()` containing tuples (offset-in-data, relocation-kind, symbol-index or addend info). Branch/call relocations in text are typically resolved at link time for static builds; dlm modules may carry additional metadata if the architecture requires late binding.
- The header still includes `textsize`, `datsize`, `bsssize`, `symsize`, and `lcsize`; the relocation region follows `HEADR + text + data + symsize + lcsize`.

amd64 specifics (9l)
--------------------
- Similar to arm64: relocation entries are collected and emitted by `asmdyn()`; value write helpers (`lput`, `llput`) write 32/64-bit addresses as appropriate. The relocation table structure mirrors the arm64 variant conceptually but the exact opcodes and field sizes differ.

Loader expectations
-------------------
The runtime loader for dlm modules walks the relocation table and applies fixups based on the relocation kind and the symbol’s resolved load address. For absolute relocations it writes the fully relocated address; for PC-relative branches it computes the delta between the callsite and the symbol. The loader knows the table format for the target architecture (see the corresponding loader code in `sys/src/libc` or the module loader sources).

Practical guidance for tools
----------------------------
- To detect dlm: check the top bit of the Plan 9 magic (0x80000000). If set, expect relocation tables following LC.
- The exact table format is arch-specific; consult the target arch’s `asmdyn()` in `sys/src/cmd/*l/asm.c` to parse entries. For arm64, entries typically include: data offset within the data segment, a small relocation kind tag, and an index/addend identifying the symbol.
- For static native executables, there is no relocation table; values in data are already fixed up.

References
----------
- `sys/src/cmd/7l/asm.c` — arm64 writer: `dynreloc` and `asmdyn` logic.
- `sys/src/cmd/9l/asm.c` — amd64 writer: similar dynamic relocation emission.
- Linkers’ data emission helpers `datblk`/`datfill` where relocatable references are recognized.
