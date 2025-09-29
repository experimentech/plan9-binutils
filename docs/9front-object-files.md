# 9front / Plan 9 object files — notes and references

This document summarizes how the 9front toolchain (assembler, compiler backends and linker) represents and operates on object files and archives. It is based on the 9front source in this workspace (notably the linker/assembler code under `sys/src/cmd/*l` and related headers).

Paths referenced in this document point into this workspace or into a local 9front tree; the most relevant sources are:

- Linkers/assemblers (examples):
  - `/path/to/ganges/9front/sys/src/cmd/6l/obj.c` (amd64 linker)
  - `/path/to/ganges/9front/sys/src/cmd/8l/obj.c` (386 linker)
  - `/path/to/ganges/9front/sys/src/cmd/6l/l.h` and `/path/to/ganges/9front/sys/src/cmd/8l/l.h` (linker headers)
  - `/path/to/ganges/9front/sys/src/cmd/6c/6.out.h` and `/path/to/ganges/9front/sys/src/cmd/8c/8.out.h` (compiler backend/assembly constants)

Use these files as the primary authoritative references when implementing tools that read or emit Plan 9 object formats in this tree.

---

## High-level overview

- 9front uses the traditional Plan 9 toolchain layout: architecture-specific assemblers (`*c`, `*l`), linkers (`*l`), and object manipulation code live under `sys/src/cmd/`.
- The linker (`6l`, `8l`, etc.) reads object files and archive members, resolves symbols, applies relocations, lays out text/data/bss, and writes a final executable image.
- Linkers support several header types (controlled with `-H`): Plan 9 native executable headers, ELF, and historical formats. `HEADTYPE`, `HEADR`, `INITTEXT`, `INITDAT`, `INITRND`, and `INITENTRY` are the main linker parameters (see `l.h` and the `main` function in `*l/obj.c`).

## Object-file detection

- `isobjfile(char *f)` is used to detect whether a file looks like a Plan 9 object file.
  - It reads the first 5 bytes and checks for certain patterns (in `6l/obj.c` and `8l/obj.c`).
  - If not recognized by that heuristic, it reads the archive magic (SARMAG / `ARMAG`) to detect a `.a` archive.
  - See: `6l/obj.c` / `8l/obj.c` — the `isobjfile()` function.

## Archives (.a) and symbol headers

- Archive format: classic UNIX ar is used for `.a` libraries. The code checks the first `SARMAG` bytes against `ARMAG` (see `<ar.h>` definitions).
- The linker expects a symbol header entry in some libraries. Typical flow in `objfile()`:
  - Read archive header, locate the special symbol table member (commonly `__.SYMDEF` or `SYMDEF`).
  - Load that member into memory and iterate symbol/class entries to find members that satisfy unresolved symbols.
  - For each matching entry, the linker seeks to the member offset and calls `ldobj()` to load that object member.
  - See `objfile()` in `8l/obj.c` and `6l/obj.c` for details of archive handling and symbol header usage.

## `ldobj()` — reading an object file into the linker's internal structures

- `ldobj(int f, long c, char *pn)` is the core loader. It reads the raw .o bytes and decodes the assembler op stream and symbol records into the linker's `Prog` and `Sym` structures.
- The object file content is an assembler/bytecode sequence produced by the compiler backend or assembler; instructions like `ANAME`, `ASIGNAME`, and other opcodes indicate symbol records and code/data directives. The `ldobj` loop reads opcode bytes and reconstructs `Prog` nodes, symbol definitions, and relocations.
- See `ldobj()` in `6l/obj.c` and `8l/obj.c`.

## Symbol records, names and signatures

- Symbol table entries are represented by `Sym` structs (see `l.h` in each arch directory). Key fields:
  - `name` — string name
  - `type` — kind (e.g., `STEXT`, `SDATA`, `SXREF`, `SIMPORT`, `SEXPORT`)
  - `value` — numeric value (address or constant)
  - `version`, `sig` — versioning and signature information
- The linker maintains a global symbol hash and resolves references while processing object files and libraries.
- Export/import/dynamic module support uses `EXPTAB` and flags such as `doexp` and `dlm` in the linker.
- See `l.h` and `6c/6.out.h` for symbolic constants and `ldobj`/`addlib`/`readundefs` in `*l/obj.c` for usage.

## Relocations and addresses

- The assembler/compiler encode addressing (`Adr`) records and relocation data in the opstream.
- The linker reconstructs `Adr` structures with fields such as `type`, `offset`, `sym`, `index`, and uses them to apply relocations into final text/data images.
- Functions and helpers: `zaddr()` (zero/parse address encoding), `vaddr()` for computing values, and `dynreloc()` for dynamic relocations.
- The header `l.h` defines the encoding masks used by `zaddr()` (T_INDEX, T_OFFSET, T_SYM, T_FCONST, T_SCONST, T_TYPE).

## Header types and layout constants

- `HEADTYPE` selects executable header format; the code sets up these values:
  - `HEADR` — bytes reserved for the executable header
  - `INITTEXT` — initial text segment address
  - `INITDAT` — initial data address
  - `INITRND` — rounding for the data segment
  - `INITENTRY` — entry symbol name
- Typical Plan 9 values (examples from 6l/8l):
  - Plan 9 native (`HEADTYPE == 2`): `HEADR = 32` (or 32 + extra), `INITTEXT` often 4096 + HEADR, `INITRND = 4096`.
  - ELF and other formats are supported with different `HEADR`/`INITTEXT` defaults.
- The linker prints header info when `-v` is enabled; the relevant setup is in `main()` of each `*l/obj.c`.

## Writing output: asmb / asmins / asmsym

- The final layout and writing is performed by functions like `asmb()`, `asmins()`, `asmsym()` or `asmdyn()`:
  - `asmb()` does the main assembly: it emits program text and data segments, symbol tables, and performs relocation writes.
  - `asmsym()` writes the symbol table portion.
  - `asmdyn()` emits dynamic linking tables when building shared modules.
- Output helpers: `lput()`/`lputl()` (write integers in target endianness) and `wput()` etc. are declared in `l.h`.

## Architecture-specific encodings

- The assembler/opcodes and addressing encodings are architecture-specific and are declared in files like `6c/6.out.h`, `8c/8.out.h`.
- These headers define opcode enums (`enum as`), addressing classes (`Y*`), and other architecture-specific constants. The linker uses `optab` and `opindex` to map instructions to assembler encoding tables.
- For relocations and instruction layouts, refer to `l.h` and `*l/optab.c` / `*l/asm.c`.

## Archive symbol header format (practical notes)

- The linkers expect an archive member that lists exported/symbols with offsets. The code in `objfile()` reads the symbol header member entirely into memory and parses null-separated records; each record contains a 4-byte offset encoded in bytes e[1..4] after a name, then the code seeks to that offset in the archive and extracts the member.
- If the archive format is invalid or the special header is missing/malformed, the linker marks the archive as bad.
- See the `objfile()` implementation in `8l/obj.c` and `6l/obj.c`.

## Notes on reading object internals

- The object stream is not an ELF/COFF-like structured file in the sense of section headers; rather it is the assembler opcode stream created by the compiler back-end / assembler. The linker must parse opcodes and symbol definitions to reconstruct sections.
- Opcode names like `ANAME`, `ASIGNAME`, etc. appear in the assembler stream; the linker dispatches on these opcodes while reading with `ldobj()`.

## Key functions (for reference)

- `isobjfile(char *f)` — quick object/archive detection (`*l/obj.c`).
- `objfile(char *file)` — open an object or archive and process its contents; invokes `ldobj()` for members.
- `ldobj(int f, long c, char *pn)` — parse an object stream from an open file descriptor.
- `dirparse()` / `dirread()` — used elsewhere for directory reading (not object-specific, but used by tools).
- `asmb()`, `asmins()`, `asmsym()` — emit final binary structures and symbol tables.

## Where to look for exact encodings

- Architecture opcode tables and encodings: `sys/src/cmd/6c/6.out.h`, `sys/src/cmd/8c/8.out.h`.
- Linker headers and writing helpers: `sys/src/cmd/6l/l.h`, `sys/src/cmd/8l/l.h`.
- Linker object readers and archive handling: `sys/src/cmd/6l/obj.c`, `sys/src/cmd/8l/obj.c`.
- For a compact view of the assembler op formats: read `ldobj()` implementations in the respective `*l/obj.c` files — they show the opstream layout (ANAME/ASIGNAME etc).

## Practical guidance / next steps

- If you need to write or parse Plan 9 object files programmatically, the canonical approach is to reuse or port the `ldobj()` parsing logic. The format is "assembler stream + embedded symbol records" rather than a strict sectioned binary like ELF.
- If you need to produce linkable object files, either invoke the compiler back-end/assembler for a target architecture (`6c`/`8c`/`asm`) or follow the encoder logic in `optab`/`asm.c` for the architecture.
- If you want, I can produce:
  - A compact cheat-sheet of object stream opcode encodings (derived from `6.out.h`/`8.out.h`).
  - A small parser program that prints symbol table and relocation entries for a `.o` file using the same decoding logic as `ldobj()`.

  ## Exact, byte-level encodings (what a linker writer needs)

  Below are the concrete encodings observed in the `ldobj()`/`zaddr()` code paths in `*l/obj.c` and the constants in `*c/*.out.h`. These are sufficient to implement a parser and simple linker that can read object streams produced by the 9front assembler/back-ends.

  - Endianness: all multi-byte integers in the object stream are little-endian. The loader consistently decodes 4- and 8-byte values using little-endian ordering (e.g. `p[0] | p[1]<<8 | p[2]<<16 | p[3]<<24`).
  - Opcodes: each record begins with a 16-bit opcode in little-endian (2 bytes). The `o` value in `ldobj()` is read as `o = bloc[0] | (bloc[1] << 8)`.

  ### General Prog record layout (most opcodes)

  For a normal instruction/prog record (the common case), the byte layout is:

  - bytes 0..1 : opcode (uint16 little-endian)
  - bytes 2..5 : line number (int32 little-endian)
  - bytes 6.. : encoded `from` address (variable length)
  - ... next : encoded `to` address (variable length)

  The loader constructs a `Prog` node: it reads the opcode, the line, then calls `zaddr()` first for the `from` operand (starting at offset 6) and then again for the `to` operand (at the next offset returned by the first `zaddr()` call). `zaddr()` returns the number of bytes consumed so you can iterate.

  ### Special records: ANAME and ASIGNAME (symbol table / name records)

  These opcodes are handled specially by `ldobj()` and are how an object file advertises symbols for the current file and for the local symbol-index table used by `zaddr()`.

  - ANAME layout (bytes):
    - 0..1 : opcode == ANAME (uint16)
    - 2    : `v` (a byte encoding the kind: e.g. `D_EXTERN`, `D_STATIC`, `D_FILE`, etc.)
    - 3    : `o` (a single-byte symbol index into the local `h[]` table)
    - 4..N : NUL-terminated symbol name string (ASCII / C string)

  - ASIGNAME layout (same as ANAME with an extra 4-byte signature):
    - 0..1 : opcode == ASIGNAME (uint16)
    - 2..5 : 4-byte signature (uint32 little-endian)
    - 6    : `v` (type byte)
    - 7    : `o` (symbol index byte)
    - 8..N : NUL-terminated symbol name

  Implementation notes from `ldobj()`:
  - `ldobj()` keeps a local array `Sym *h[NSYM]` (NSYM is typically 50). When it reads an ANAME/ASIGNAME it calls `lookup(name, version)` to obtain a `Sym *` and stores it in `h[o]`.
  - Later, when an operand encoding contains a T_SYM flag, `zaddr()` uses the single-byte index to choose `a->sym = h[index]`.

  This means symbol references that are encoded with T_SYM refer to the file-local symbol table set up by the ANAME entries (not a global symbol table index). A linker must track `h[]` per object file while parsing, and then convert those `Sym *` references into its global symbol map.

  ### Address encoding (what `zaddr()` parses)

  Each address/operand in the stream is encoded as a small structure beginning with a single type-byte `t` followed by optional fields. The order and meaning (as implemented in `zaddr()`) is:

  - byte 0: `t` (flags/encoding mask)
    - bits defined in `l.h` / `*.out.h`: T_TYPE, T_INDEX, T_OFFSET, T_FCONST, T_SYM, T_SCONST, T_64, etc.
  - if (t & T_INDEX): 2 bytes follow
    - byte 1: index (a small register/index number)
    - byte 2: scale (a scale factor)
  - if (t & T_OFFSET): 4 bytes follow (signed/unsigned offset, little-endian). This becomes `a->offset` low 32 bits.
    - if also (t & T_64): another 4 bytes follow which are the high 32 bits; `a->offset` becomes a 64-bit value formed as `high<<32 | low`.
  - if (t & T_SYM): 1 byte follows — an index into the local `h[]` table; `a->sym = h[index]`.
  - if (t & T_FCONST): 8 bytes follow — IEEE representation (two 4-byte words as loader reads `ieee.l` and `ieee.h`).
  - if (t & T_SCONST): NSNAME bytes follow (copy into `a->scon[]`), where NSNAME is typically 8.
  - if (t & T_TYPE): 1 byte follows — this sets `a->type` (a D_* value such as `D_EXTERN`, `D_ADDR`, `D_CONST`, etc.).

  Byte-level decoding pseudocode (based on `zaddr()`):

  - read t = p[0]; c = 1
  - if (t & T_INDEX): a->index = p[c]; a->scale = p[c+1]; c += 2
  - if (t & T_OFFSET): low = p[c..c+3]; c += 4; a->offset = low
    - if (t & T_64): high = p[c..c+3]; c += 4; a->offset = ((vlong)high<<32) | (a->offset & 0xFFFFFFFF)
  - a->sym = nil
  - if (t & T_SYM): a->sym = h[p[c]]; c++
  - if (t & T_FCONST): a->ieee.l = p[c..c+3]; a->ieee.h = p[c+4..c+7]; c += 8
  - if (t & T_SCONST): memcpy(a->scon, p+c, NSNAME); c += NSNAME
  - if (t & T_TYPE): a->type = p[c]; c++

  Notes:
  - NSYM (size of the local symbol index table) and NSNAME (string constant length) are defined in the architecture-specific `*.out.h` files; common values seen in this tree are NSYM=50, NSNAME=8.
  - The order of fields in the stream is important: `T_INDEX` (index/scale) appears first, then `T_OFFSET` (+optional T_64), then `T_SYM`, then floats/strings, then `T_TYPE`.

  ### Symbol/ANAME semantics and common opcode markers

  - ANAME entries with `v` set to `D_EXTERN` or `D_STATIC` will mark the `Sym` as `SXREF` if it is currently undefined.
  - ANAME with `v == D_FILE` is used to record source-file history (SFILE symbols) and influences path history tracking used by the compiler/linker.
  - Typical section-switching opcodes the linker expects:
    - `ATEXT` — starts a text function; sets the symbol's type to STEXT and records the current pc in `s->value`.
    - `ADATA` / `AGLOBL` / `ADYNT` / `AINIT` — data and initializer directives.
    - `AEND` — end of this object's stream.

  See the large opcode enums in `sys/src/cmd/6c/6.out.h` and `sys/src/cmd/8c/8.out.h` for the full enumerations; your linker should at minimum recognise and handle `ANAME`/`ASIGNAME`, `ATEXT`, `ADATA`, `AGLOBL`, and `AEND` (others can be treated as generic instructions where you parse their operands but don't need to understand every mnemonic).

  ### Archive symbol header format (how libraries are scanned)

  The code in `objfile()` reads the first archive member (expected name is `SYMDEF`/`__.SYMDEF`) and treats it as a concatenation of symbol records. The practical behaviour is:

  - The archive symbol header is read entirely into memory. The linker iterates over it using:
    - for(e = start; e < stop; e = strchr(e+5, 0) + 1) { ... }
  - In each record, the loader expects the name to start at `e+5`. The 4-byte member offset is read from bytes `e[1]..e[4]` (little-endian). The code then seeks to that offset in the archive file and reads the archive member header to find the actual object data.

  Put simply: each symbol-entry record contains a small fixed-width header (the code treats bytes 1..4 as the offset) followed by the symbol name beginning at offset 5 from the record start. The record parser uses `strchr(e+5,0)` to find the end of the name. If the archive layout is malformed the linker rejects the archive.

  ### Constants and sizes you must respect

  - NSYM (local symbol table entries per object): typically 50 (see `6.out.h` / `8.out.h`). This limits the single-byte index used by T_SYM.
  - NSNAME (S-constant size): typically 8.
  - The type/flag bits used in the `t` byte for `zaddr()` are defined in `l.h` / `*.out.h`:
    - T_TYPE = 1<<0
    - T_INDEX = 1<<1
    - T_OFFSET = 1<<2
    - T_FCONST = 1<<3
    - T_SYM = 1<<4
    - T_SCONST = 1<<5
    - T_64 = 1<<6 (present when an offset is 64-bit)

  ### Example: minimal object stream fragments (illustrative)

  Below are tiny, conceptual examples showing how bytes might look. These are illustrative (not a dump produced by a real assembler) but follow the loader's parsing logic.

  - ANAME for symbol `foo` with index 3 and type `D_EXTERN`:

    bytes:
    - [0..1] = opcode ANAME (e.g. 0xNN 0xMM little-endian)
    - [2] = D_EXTERN (one byte)
    - [3] = 0x03 (index 3)
    - [4..] = 'f' 'o' 'o' '\0'

  - Instruction record with an operand that references that symbol via the local index (T_SYM):

    bytes:
    - [0..1] = opcode for some instruction (uint16)
    - [2..5] = line number (uint32)
    - [6] = t (flags) with T_SYM set, maybe T_OFFSET set as well
    - [7] = index byte (e.g. 0x03)
    - [ ... ] = encoding for the `to` operand (again begins with its own t byte)

  The loader will call `zaddr()` to decode the `from` operand (starting at offset 6), which will see T_SYM and set `a->sym = h[3]`.

  ### How to implement a linker using this information

  1. Parse the object file stream by copying `ldobj()`'s high-level loop:
     - read a small input buffer
     - read `o = read16()` (opcode)
     - if `o == ANAME`/`ASIGNAME`: decode the local table entry and populate `h[index]` with a newly created/registered global symbol object (keep the mapping from the local `h[]` to the global symbol object)
     - else: read line, `zaddr()`-decode `from` and `to` operands, then create an intermediate `Prog` or relocation entry representing the instruction/data.

  2. Convert local symbol pointers `a->sym` (which point into the per-object `h[]` table) into indices into your global symbol table. For relocations keep a record: (where to relocate, which global symbol, addend, relocation type) so you can apply relocations after layout.

  3. When encountering `ATEXT` / `ADATA` / `AGLOBL`, allocate space in your text/data layout and record symbol addresses (for STEXT/SDATA/SBSS) using the eventual relocated addresses.

  4. After you've parsed all input objects and libraries and resolved symbol bindings, apply relocations using the records collected from address encodings. Use `Roffset`/`Rindex` constants (see `l.h`) when creating import/export indexes for dynamic modules.

  5. Emit final executable header per `HEADTYPE` (examples and defaults are set in each `*l/main()`), then write the laid-out text/data/bss and any symbol/export tables.

  ### Where to consult source for missing details

  - opcode enumeration (complete): `sys/src/cmd/6c/6.out.h`, `sys/src/cmd/8c/8.out.h` — you'll want to copy these enums into your tool so you can interpret/opcode numbers if you need to.
  - linker write helpers and relocation constants: `sys/src/cmd/6l/l.h`, `sys/src/cmd/8l/l.h` (there are `lput()`/`lputl()` helpers and `Roffset`/`Rindex` constants used for import encoding).
  - how symbol headers are formed by tools: consult the tools which build archives on your system (typically `ar`/`ranlib`) or the platform's archive helpers; the linker expects the `__.SYMDEF` layout described above.

  ---

  If you'd like, I can next:

  - produce a minimal reference parser in C (one source file) that implements `ldobj()`/`zaddr()` logic and prints each decoded `Prog`/symbol/relocation; or
  - generate a compact cheat-sheet that maps each opcode mnemonic to its numeric opcode for a single architecture (e.g. amd64 `6c/6.out.h`) so you can map opcodes to handlers while implementing the linker.

  Tell me which architecture you want prioritized (amd64 / 386 / arm / arm64) and I will generate a small, buildable parser program that emits a human-readable listing of an object file's contents.

---

Document generated by static inspection of the 9front sources in this workspace. If you want a deeper, verbatim extraction of the opcode enums, symbol field layouts, or a runnable parser, tell me which architecture(s) to target (e.g., amd64 / 386 / arm) and I will produce that next.
