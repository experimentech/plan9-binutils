9front executable files — precise, code-backed reference
=====================================================

This document describes how the 9front linkers (the *l tools) lay out and write executables. It is a companion to the object-stream decoder notes and is based on the concrete implementations of the writers (`asmb`, `asmsym`, `asmlc`, `asmdyn`, and helpers) found in the per-architecture `*/asm.c` files in `sys/src/cmd/*l` (examples: `9l/asm.c`, `7l/asm.c`, `6l/asm.c`, `8l/asm.c`, `5l/asm.c`). Where the implementations differ between architectures the differences are noted and code locations are referenced.
Deep dive — exact on-disk formats and decoding rules
--------------------------------------------------

This section gives the byte-level decoding rules you need to parse any native 9front executable produced by the linkers in this tree. Everything below is directly derived from the `asmb()` / `asmsym()` / `asmlc()` implementations in the per-arch `*/asm.c` files (see the References section for paths).

1) Detecting the file type
- Read the first 4 bytes:
  - If they equal 0x7f, 'E', 'L', 'F' -> parse as ELF (the writers emit a proper ELF header when HEADTYPE requests it).
  - Otherwise interpret the 4 bytes as the Plan9-style magic. Many writers compute this magic as an expression like `4*N*N + 7` where N is architecture-specific (examples: `4*27*27+7` in `9l`, `4*28*28+7` in `7l`, `4*11*11+7` in some other `asm.c`). The writers often OR a secondary bit 0x00008000 and set the high bit 0x80000000 when `dlm` (dynamic module) is enabled. Treat the magic match as a Plan 9 native header if it fits this pattern.

2) Plan 9 native header layout (canonical)
- The writers vary slightly across architectures (word order, whether an extra 64-bit entry is appended) but share the following elements in order (word = 4 bytes unless the writer uses `llput` for 64-bit entries):

  offset 0x00: magic (4 bytes)
  offset 0x04: textsize (4 bytes)
  offset 0x08: datsize  (4 bytes)
  offset 0x0C: bsssize  (4 bytes)
  offset 0x10: symsize  (4 bytes)
  offset 0x14: entry_lo (4 bytes)    — entry value (lower 32 bits or masked variant)
  offset 0x18: aux/zero  (4 bytes)    — often zero or reserved
  offset 0x1C: lcsize   (4 bytes)    — size of the LC (line) stream
  offset 0x20: entry_hi (8 bytes)    — on some 64-bit writers (they write the full 64-bit entry with llput)

- Notes:
  - Some writers mask the `entry_lo` with `PADDR()` (a macro that clears architecture-specific high bits) before writing.
  - The presence and position of the extra 8 byte `entry_hi` depend on the writer. For example `7l` (arm64) and `9l` (amd64) write `llput(vl)` to append a 64-bit entry.

3) Where segments live in the file
- After the header the writers place segments in this order (subject to per-arch padding rules):
  - Text segment: at file offset HEADR (writers seek(cout, HEADR, 0) then emit instructions). Many writers place text at `HEADR` and expect loaders to map it to `INITTEXT`.
  - Data segment: typically written after `HEADR + textsize`. Some writers use a padding rule and round file offsets with `rnd(HEADR+textsize, INITRND)` (INITRND commonly 0x10000 or 4096) to satisfy platform alignment.
  - Symbol blob: written after text + data. Writers seek to `HEADR + textsize + datsize` (or to the padded offset) and write the symbol blob produced by `asmsym()`/`putsymb()`.
  - LC stream (line number table): written after the symbol blob. `asmlc()` sets `lcsize` which is then written into the header.

4) Exact symbol blob byte format (putsymb rules)
- The symbol writer `putsymb()` uses the following canonical byte sequence for each symbol entry. The code is consistent across many architectures — small differences are: (a) whether an additional 4-byte high word is written before the low 4-bytes (when `HEADTYPE == 2`), and (b) whether values are written in native endianness or swapped versions using different helpers (`lput` vs `lputl`). When writing a parser, consult the relevant architecture's `asm.c` to know whether `lput` (big-endian helper) or `lputl` (little-endian helper) was used.

Symbol entry (per `putsymb()`):
  - optional high-word (4 bytes) when the writer emits a 64-bit value (the code typically does `if(HEADTYPE == 2) lput(v>>32);`).
  - low-word (4 bytes): `lput(v)` (value is typically the runtime address or offset for the symbol). For data/bss this often equals `s->value + INITDAT`.
  - one byte: `t + 0x80` where `t` is the ASCII type tag (the `+0x80` flags the byte as a variable-length symbol record).
  - name bytes: either
      * plain NUL-terminated ASCII string (for most symbol types), or
      * for type 'z' or 'Z': a special two-byte pair encoding used for history/file entries: `putsymb()` writes `CPUT(s[0])` then emits pairs of bytes until it writes two zero bytes — the end-of-record is detected when two consecutive zero bytes appear.

- `putsymb()` book-keeping: after writing an entry, the code increments `symsize` by `4 + 1 + i + 1` where `i` is the number of name bytes (excluding NUL). If `HEADTYPE == 2` it also adds an extra 4 for the high-word.

5) Symbol type letters (common meanings)
- The single-character `t` used by `putsymb()` maps to meaning as follows (common set found across architectures):
  - 'T' : text symbol (function/label)
  - 'D' : data symbol (global initialized data/const)
  - 'B' : bss symbol (uninitialized data)
  - 'f' : file record (internal writer use; `putsymb()` strips the first character when t == 'f')
  - 'z'/'Z' : history/file path encoded with the two-byte pair format
  - 'L' : local text (local label)
  - 'm' : ".frame" entry (frame size)
  - 'a' : auto variable (stack local)
  - 'p' : param variable

  (Writers sometimes add other one-letter encodings; check the arch `asmsym()` implementations if you need more tags.)

6) Line-number table (asmlc) — exact variable-length encoding
- The LC stream is a compact code of small tokens encoding (pc, line) changes. `asmlc()` emits tokens in this algorithm:

  Encoder state: oldpc = INITTEXT, oldlc = 0 (or previous saved), MINLC = 4 (common), lcsize = 0

  For each instruction `p` with a line number different from oldlc:
    1. Compute v = (p->pc - oldpc) / MINLC. Emit repeat tokens of value `s+128` (where s in 1..127) until v is zero. Each emission increases lcsize by 1 and increases oldpc by s*MINLC.
    2. Compute s = p->line - oldlc (signed 32-bit). If s in [-63..64] emit a single byte:
         - 1..64 encodes small positive deltas (emit value = s)
         - 65..128 encodes small negative deltas as (64 - s) (emit value = 64 - s + 64)
       If s is out of the small range, emit 0 followed by a 4-byte signed big-endian delta (the code emits s >>24, >>16, >>8, >>0 via CPUT calls).
    3. Update oldlc and oldpc accordingly.

- Decoder pseudo-code (consume lc stream of length `lcsize`):

  pc = INITTEXT
  line = initial (often 0 or whatever the tools assume)
  remaining = lcsize
  while remaining > 0:
    b = read_u8()
    remaining -= 1
    if b >= 129:
      pc += (b - 128) * MINLC
      continue
    if b == 0:
      delta = read_s32_be()
      line += delta
      remaining -= 4
      continue
    if 1 <= b <= 64:
      line += b
      continue
    /* 65..128 */
    line += (64 - b)

  Notes: MINLC is usually 4 but consult the arch's `asmlc()`; the writers use a MINLC constant in each asm.c.

7) Data emission and relocations (datblk / datfill)
- The writers produce the raw bytes for data using architecture-specific endianness helpers and `inuxi`/`fnuxi` index tables. The emitted bytes are not simply CPU native order but adhere to the architecture's expected byte ordering and may be written through helper functions `lput`, `lputl`, `llput`, `llputl`, `cput`, `wput`, `wputl` which determine endianness.

- When a data word references a symbol the writer does one of two things depending on the build mode:
  - static executable: the writer resolves the symbol at link time and writes the assembled absolute value. The linker's `datfill()` logic computes the final `d` by adjusting the symbol's `s->value` depending on `s->type`:
      * STEXT / SLEAF / SSTRING: add `s->value` (text-relative)
      * SDATA / SBSS: add `s->value + INITDAT` (data/bss relocation)
  - dynamic module (`dlm`) mode: `datfill()` calls `dynreloc()` for those entries instead of fully resolving them. `asmdyn()` is later used to emit a runtime relocation table for loader use.

- Practical parser guidance: If you want to reconstruct relocations from a completed non-dynamic executable you must follow these rules:
  - Read the symbol table to obtain symbol names and values (s->value). For each immediate constant in data, determine whether it was constructed from a symbol; there is no separate relocation table in a fully-linked static native file unless the writer wrote additional dynamic relocation metadata. So reconstructing relocations from a fully-relocated executable is generally impossible without debug metadata. For dynamic modules, `asmdyn()` writes relocation information you can parse.

8) Endianness and word helpers
- The writers use a set of small helpers to place integers into the buffered output (`lput`, `lputl`, `llput`, `llputl`, `cput`, `wput`, `wputl`). Their behavior is architecture-dependent. Example patterns seen in `*/asm.c`:
  - `lput()` often writes the 4-byte value in big-endian order into the output buffer (store >>24, >>16, >>8, >>0).
  - `lputl()` writes the 4-byte value in little-endian order (store >>0, >>8, >>16, >>24).
  - `llput()` writes two `lput()` words consecutively (high 32 bits then low 32 bits) or vice versa depending on the file; the code sometimes writes `llput(v)` to write a full 64-bit value in a canonical per-arch order.

- Parser guidance: do not assume a single endianness for all 9front executable writers — check the `asm.c` for the target architecture. In practice:
  - Many 32-bit writers use `lput` (big-endian helper) for header fields and symbol table low-words.
  - Writers targeting little-endian CPUs may use `lputl`/`llputl` to produce little-endian on-disk values — consult the arch file.

9) ELF output (HEADTYPE 5 / 6)
- When a writer emits ELF the code constructs e_ident, sets class to 32/64-bit, endian to LSB/MSB as appropriate, sets e_type to ET_EXEC and sets e_machine to the architecture number. It then writes program headers (PT_LOAD) describing text and data segments and sometimes an extra PT_NULL or note segment that records the symbol/line table sizes. The writers populate:
  - phdr for text: p_type = PT_LOAD, p_flags = RX, p_offset = HEADR, p_vaddr = INITTEXT, p_filesz = textsize
  - phdr for data: p_type = PT_LOAD, p_flags = RW, p_offset = HEADR+textsize, p_vaddr = INITDAT, p_filesz = datsize

- If you need to parse ELF, prefer a standard ELF parser: the writers emit an ELF header and program headers that conform to the spec.

10) Dynamic modules and relocation metadata
- Writers set the high-bit in the Plan 9 magic (`magic |= 0x80000000`) when building dynamically-loadable modules (`dlm`). In addition they perform these actions:
  - During data emission, call `dynreloc()` to stash a runtime relocation entry for any fields that reference an unresolved or relocatable symbol.
  - After symbol and LC emission call `asmdyn()` to write the relocation table(s) required by the loader.

  Parser guidance: when the magic high-bit is set you should expect an additional relocation table region after the symbol/LC streams. The exact layout is produced by `asmdyn()` (see arch `asm.c`) and is architecture-specific; inspect the arch's `asmdyn()` implementation to decode the relocation items.

11) Practical parsing recipe (step-by-step)
-----------------------------------------
Given an executable file produced by a 9front linker, here is a robust parsing recipe:

1. Read first 4 bytes. If ELF magic -> hand to ELF parser.
2. Otherwise read the 4-byte magic and check whether it matches the plan9-magic formula for a known arch (compare with `*/asm.c` `magic = 4*N*N+7` expressions). Remember to check the high-bit which signals `dlm`.
3. If Plan 9 native:
   - Read the next words according to the writer's ordering (text,datsize,bsssize,symsize,entry_lo,aux,lcsize). If the arch's writer appends a 64-bit entry do a llget/read-8 at the end if needed.
   - Compute offsets: text starts at HEADR; data at HEADR+textsize (or rounded per INITRND); syms at HEADR+textsize+datsize; lc at syms+symsize.
   - Read `symsize` bytes and parse symbols per `putsymb()` rules (read optional high word if the writer wrote it, then low word, then type byte, then name). Pay special attention to 'z'/'Z' names.
   - Read the `lcsize` bytes and decode PC/line using the LC decoder.
   - If magic high-bit (dlm) is set, read relocation tables as emitted by `asmdyn()` for the arch.

12) Example decoders & test cases
- I can produce a small, single-file C parser that implements the recipe above for arm64 (7l) or amd64 (9l). That tool will:
  - open a file, detect header type, dump text/data sizes, parse & print symbol table entries and their addresses and types, and decode the LC stream to list pc→line mappings.
  - for dlm files attempt to parse relocation items written by `asmdyn()` (arm64/amd64 variants differ) and print a relocation list.

If you'd like that, tell me which architecture to prioritize (arm64 was your earlier priority) and I'll add the tool under `tools/ldexe-<arch>.c`, compile it in the workspace and run it against a sample `.out`/executable from your repo to demonstrate the output.

References (code locations used to expand this doc)
- `sys/src/cmd/9l/asm.c` — amd64 writer and ELF output branch; magic `4*27*27+7`, writes `llput(entry)` for 64-bit entry.
- `sys/src/cmd/7l/asm.c` — arm64 writer; magic `4*28*28+7`, writer sets fat bit and appends `llput(vl)` for the full 64-bit entry. Contains `datblk`/`zput`/`dynreloc` calls and the `asmsym`/`asmlc` implementations.
- `sys/src/cmd/6l/asm.c`, `sys/src/cmd/8l/asm.c`, `sys/src/cmd/5l/asm.c`, `sys/src/cmd/2l/asm.c` — other arch writers: they illustrate different uses of `lput` vs `lputl`, ELF branches, and different values used to compute the magic.
- `sys/src/cmd/*l/obj.c` — complementary code that consumes headers and symbol blobs; useful for cross-checking how loaders expect values like `entry` and symbol addresses to be encoded.

Appendix: small annotated examples
- Symbol entry bytes (example): assume HEADTYPE==2 and writer uses little-endian `lputl` style for low words (the example below uses the canonical conceptual order):

  [0x00..0x03] : high-word (v>>32)
  [0x04..0x07] : low-word (v & 0xffffffff)
  [0x08]      : type_byte = (byte) (('T') + 0x80)
  [0x09..0x??] : 's' 'y' 'm' 'N' 'A' 'M' 'E' 0x00

- LC sequence example (MINLC=4): to describe two instruction steps where pc advances by 8 bytes and line increases by +3 and then +1:
  - emit (8 / MINLC == 2) -> token 130 (0x82)
  - emit small positive delta 3 -> token 3
  - emit small positive delta 1 -> token 1

  Decoded: 0x82 -> pc += 2*4 = 8; 0x03 -> line += 3; 0x01 -> line += 1

Closing notes
- This expanded document should let you write a robust parser for 9front native executables and understand how linkers place symbols, line tables and relocation metadata on disk. If you'd like, I will now:
  - implement an arm64 (7l) standalone parser that prints header/symbols/LC entries, or
  - add exact magic numeric examples (expanded for each arch) extracted from the arch `asm.c` files and produce a small test vector with a trimmed synthetic executable.

Tell me which of the two follow-ups you want (parser or per-arch numeric table + test vectors) and I'll implement it next.

Goals
- Explain the final executable layout (text/data/bss/symbols/line tables).
- Document the header variants selectable with `-H`/`HEADTYPE` and the fields that matter to linkers and loaders.
- Describe the on-disk symbol table encoding produced by `asmsym()` / `putsymb()`.
- Describe the line-number table encoding produced by `asmlc()` (the "lc" table).
- Explain how dynamic modules / dlm change header magic and runtime relocation emission.
- Provide exact, implementable guidance for building or parsing a final 9front executable.

Important writer entry points (where to look in code)
- asmb(): main assembly / final file write. This function lays out text, writes text/data, seeks to symbol area and calls `asmsym()` / `asmlc()` / `asmdyn()` and finally writes the executable header. (See `sys/src/cmd/*l/asm.c` — each arch implements its own writer.)
- asmsym(): iterate symbol table and call `putsymb()` for each symbol (writing the 'symbol blob' that a 9front loader expects).
- asmlc(): produce the line-number "lc" table, a compact variable-length encoding used by the toolchain.
- asmdyn(): helper that writes dynamic module-specific structures (used for `dlm` option).
- datblk()/datfill(): helpers that produce the on-disk data bytes (including endianness / nuxi ordering) and handle relocations when writing data contents.

Global sequence performed by asmb()
-------------------------------
1. Reserve space for header by seeking to HEADR (typically 32–40 bytes, arch-dependent) and writing program text at the logical INITTEXT address.
2. Emit instructions/data for all `Prog` nodes (the assembler output). The code emits instructions in order and increments pc accordingly.
3. Write remaining text-segment strings (if any) and then relocate to write data at HEADR + textsize (or at an architecture-specific padded offset using INITRND).
4. Emit the data segment using `datblk`/`datfill`, which also apply relocations (and call `dynreloc` when building dlm modules).
5. Seek to symbol area and call `asmsym()` which writes the symbol table blob via `putsymb()`. Then call `asmlc()` to write the line table and possibly other tables (`asmlc()` writes a compact stream of line deltas). `asmdyn()` is called when building dynamic modules to add runtime relocation metadata.
6. Seek back to the file start and write the executable header for the selected `HEADTYPE`. The header contains the magic, text/data/bss sizes, symbol-table size, entry point, and other arch-specific fields.

Byte order and sizes
- Files are written in the byte order and integer sizes expected by the target architecture as implemented in each `asm.c`. The canonical Plan 9 writers use big-endian or little-endian helpers (`lput`, `llput`, `lputl`, `llputl`, `cput`) to place values into the buffered output. In practice, the object and executable formats are written little-endian on the common modern architectures in this tree; refer to `asm.c` for `lput` vs `lputl` usage.

HEADTYPE: header variants and what the header contains
---------------------------------------------------
The linkers support multiple header types selectable with `-H` or via `HEADTYPE`. The meaning of the numerical `HEADTYPE` is architecture dependent but the writers implement the same conceptual types. The commonly used values and their semantics are:

- HEADTYPE 0: "no header" / very small compatibility header. The linkers often write only a minimal stub or treat the file as a flat binary. Often used for boot images.

- HEADTYPE 1: an older q.out / COFF / a.out–like header (varies by arch). Writers include fields like textsize, datsize, bsssize, symsize and an entry value. Use when building for old systems that expect this layout.

- HEADTYPE 2: Plan 9 native header. This is the common case for 9front native executables. The header layout (code excerpts collected from multiple `asm.c` implementations) is:

  - 4 byte magic: typically computed as 4 * ARCHID * ARCHID + 7, OR'd with 0x00008000 in some cases to indicate a "fat" header; the top bit (0x80000000) is set when `dlm` (dynamic module) is true. The exact numeric value depends on the architecture (see examples below).
  - 4 byte textsize (bytes in text segment)
  - 4 byte datsize (bytes in data segment)
  - 4 byte bsssize (bytes in BSS)
  - 4 byte symsize (number of bytes in symbol table blob)
  - 4 byte entry (virtual address of entry point, often masked or adjusted)
  - 4 byte aux (often 0)
  - 4 byte lcsize (line number table size)
  - 8 byte entry (full entry value repeated for convenience on some arches)

  The exact order and whether the entry appears twice (32/64-bit) depends on the writer; check the architecture's `asm.c` file for precise layout. Example: in `9l/asm.c` (amd64) the writer sets a magic with `4*27*27+7` and writes an extra 64-bit entry at the end with `llput(vl)`.

- HEADTYPE 3..7: various special legacy or ELF headers. Writers include cases for ELF (they write an ELF e_ident, e_type, e_machine, entry, program headers etc.), NetBSD/boot, DOS MZ, or architecture- or OS-specific boot formats. When an ELF header is written the writer emits a full ELF header and program headers describing the loadable PT_LOAD segments (text/data) and a symbol table descriptor in a PT_NULL/notes-like phdr.

Common header writer pattern (what to parse)
- The writers follow a reproducible pattern when creating the file; a parser can reconstruct it by implementing the reverse steps:
  1. Read the first 4 or 8 bytes: if they match known ELF magic (0x7f 'E' 'L' 'F') switch to ELF parsing.
  2. Otherwise interpret the first 4 bytes as a plan9-style magic. If the value matches formula 4*N*N+7 (plus optional 0x8000/0x80000000 flags) treat as a Plan 9 native header. The top bit (0x80000000) commonly flags a dlm (dynamic) file.
  3. From the header read textsize/datsize/bsssize/symsize and entry. Use these to find sections in the file: the text segment is at HEADR..HEADR+textsize (or at HEADR rounded/padded according to INITRND), data segment follows, then symsize bytes of symbol table, then the line table.

Examples (code references)
- The code computes a magic value with a small formula that depends on architecture. Examples extracted directly from the writers:
  - amd64 (`sys/src/cmd/9l/asm.c`): magic = 4*27*27+7 (then magic |= 0x00008000). The code writes Plan 9 header fields, then `llput(vl)` to write the full entry 64-bit value.
  - arm64 (`sys/src/cmd/7l/asm.c`): magic = 4*28*28+7 (writer ORs 0x00008000 for fat header and sets the top bit when dlm is used). See `7l/asm.c` for the exact sequence that writes the header and llput of entry.
  - other arches: the writers are consistent in shape but vary in their numeric magic constant (see `*/asm.c`).

  Per-architecture magic and header helpers (extracted)
  --------------------------------------------------

  Below are the concrete values and writer helpers found in the per-architecture `*/asm.c` files I inspected. For each linker directory I list: the magic expression (as it appears in source), the evaluated decimal value, whether fat-header or dlm bits are OR'd, which helper the writer used to emit the header fields (this determines on-disk byte order), and whether the writer appends a full 64-bit entry with `llput`.

  - `9l` (sys/src/cmd/9l/asm.c)
    - magic expr: 4*27*27+7 = 2923
    - fat header: yes (magic |= 0x00008000)
    - dlm top-bit: yes (magic |= 0x80000000 when dlm)
    - header helper: `lput` (macro LPUT; writes bytes via cbp[0..3] = (c)>>24 .. (c))
    - appends 64-bit entry: yes (`llput(vl)`)

  - `7l` (sys/src/cmd/7l/asm.c)
    - magic expr: 4*28*28+7 = 3143
    - fat header: yes (magic |= 0x00008000)
    - dlm top-bit: yes (magic |= 0x80000000 when dlm)
    - header helper: `lput` (big-endian 4-byte writer in that file)
    - appends 64-bit entry: yes (`llput(vl)`)

  - `6l` (sys/src/cmd/6l/asm.c)
    - magic expr: 4*26*26+7 = 2711
    - fat header: yes (magic |= 0x00008000)
    - dlm top-bit: yes (magic |= 0x80000000 when dlm)
    - header helper: `lput` (big-endian style via cput >>24..)
    - appends 64-bit entry: yes (`llput(vl)`)

  - `8l` (sys/src/cmd/8l/asm.c)
    - magic expr: 4*11*11+7 = 491
    - fat header / dlm: file sets `magic |= 0x80000000` when dlm
    - header helper: `lput` / `lputl` both appear in file (ELF / other header branches); the Plan 9 path uses `lput` in this implementation
    - appends 64-bit entry: no explicit `llput` in the Plan 9 header path

  - `vl` (sys/src/cmd/vl/asm.c)
    - magic expr: `((((4*t)+0)*t)+7)` where `t = 24` if `little` else `t = 16`
      - little: 4*24*24+7 = 2311
      - big:    4*16*16+7 = 1031
    - fat header / dlm: handled via other header branches; this file dispatches endianness at runtime (`little` flag)
    - header helper: `LPUT` macro which chooses `LLEPUT` (little) or `LBEPUT` (big)
    - appends 64-bit entry: no `llput` observed in the Plan 9 header path

  - `ql` (sys/src/cmd/ql/asm.c)
    - magic expr: 4*21*21+7 = 1771 (sometimes OR'd with 0x80000000 for dlm)
    - fat header: some branches OR 0x00008000 (see file variants)
    - header helper: `lput` (big-endian style macro in the file)
    - appends 64-bit entry: no `llput` observed in Plan 9 header path

  - `tl` (sys/src/cmd/tl/asm.c)
    - magic expr: 0x647 (hex) = 1607 decimal (Plan 9 path), sometimes OR'd with 0x80000000 when dlm
    - header helper: `lput` (this file also defines `lputl` for little-endian ELF/a.out branches)
    - appends 64-bit entry: no `llput` in Plan 9 path

  - `5l` (sys/src/cmd/5l/asm.c)
    - magic expr (Plan 9 path): 0x647 (same as `tl`) or different header cases; many boot/ELF cases in file
    - header helper: `lput` / `lputl` used in different branches (ARM/other boot formats)
    - appends 64-bit entry: no `llput` in the Plan 9 path

  - `1l`, `2l` (sys/src/cmd/1l/asm.c, sys/src/cmd/2l/asm.c)
    - magic expr (Plan 9 path): `0407` (octal) = 263 decimal (these older/32-bit writers use `0407` in the plan9 header path)
    - header helper: `lput` (big-endian byte emission via cput >>24..)
    - appends 64-bit entry: no `llput`

  Notes:
  - The numeric values above are evaluated from the expressions in the source; many writers then OR the value with additional bits for "fat header" (0x00008000) and `dlm` (0x80000000). When parsing, strip those flag bits to identify the base magic.
  - `lput` vs `lputl` vs `LPUT` matters: it determines on-disk endianness for 4-byte fields. Several files include both helpers and select one depending on `HEADTYPE` (ELF vs Plan 9) or a runtime `little` flag (as in `vl`). Always consult the target arch's `asm.c` when decoding a particular binary.
  - `llput(vl)` appends a 64-bit entry (two consecutive 32-bit `lput` words) in some 64-bit writers (notably `9l`, `7l`, `6l`). If present, the header contains an extra 8-byte full-entry after the usual 32-bit `entry` word and the `lcsize` field; a parser should read it when the arch's writer uses `llput`.


Symbol table blob (the `asmsym()` / `putsymb()` encoding)
-------------------------------------------------
The linkers do not write a classic ELF symbol table for Plan 9 native executables; they write a compact symbol blob consumed by Plan 9 tools. The encoding implemented by `putsymb()` is used across the architectures' writers with only small differences (value size may be 4 or 8 bytes depending on `HEADTYPE`). The algorithm is:

1. For each symbol to export, write a value field:
   - If `HEADTYPE` requires a 64-bit entry (Plan 9 on 64-bit archs) the writer writes an extra high-word in some cases; otherwise just a 32-bit value. Concretely `putsymb` calls `lput()` for the low 32-bit word, and when `HEADTYPE == 2` some writers also write the high 32 bits before the low 32-bit value.

2. Write the symbol type/name byte: `CPUT(t + 0x80)`. The `+0x80` is a canonical flag and the type letter indicates the symbol kind: e.g. 'T' (text), 'D' (data), 'B' (bss), 'f' (file name entry), 'z'/'Z' (wide file name encodes), 'L' (local text), 'm' (frame), 'a' (auto), 'p' (param), etc. If `ver` is non-zero the code converts uppercase to lowercase to indicate versioning.

3. Write the textual name that follows the type byte.
   - Normal names: a plain NUL-terminated ASCII string.
   - 'z' and 'Z' entries (file path entries) use a narrow-pair encoding: `putsymb()` writes `CPUT(s[0])` then iterates the encoded path emitting two bytes at a time until it sees two zero bytes; this encodes path components in a 2-byte-per-component scheme used by the histfrog/history symbol handling. When decoding, watch for the `z`/`Z` type and parse until the terminating double-zero.

4. In `putsymb()` the writer maintains `symsize` by adding the bytes emitted. When HEADTYPE == 2 (Plan 9 64-bit variant) an extra 4 bytes are added per symbol for the upper 32-bit of the value.

Notes on symbol values and relocations
- Symbol values written are the runtime virtual addresses (for STEXT/SDATA/SBSS) — often `s->value + INITDAT` for data/bss. For textual symbols, `s->value` is the offset in text, which is the link-time `pc` value; the loader expects these as runtime addresses or as offsets depending on HEADTYPE.
- During data emission the writers call `dynreloc()` for dynamic module builds and may also call `ckoff()` when encountering SUNDEF references; datablk/datfill contain the logic by which constant fields that reference symbols get patched or have relocation metadata emitted.

Line-number table (asmlc) encoding
---------------------------------
The `asmlc()` function writes a compact stream of line-number information used by debugging and tools. The encoding is a simple delta encoding with two variable-length components: PC increments and line-number increments.

Algorithm (encoder side, see `asmlc()`):
 - Maintain `oldpc` and `oldlc` (last PC and last line).
 - For a new instruction `p` with line != oldlc, compute pc-gap/ MINLC = usually 4 and let v = (p->pc - oldpc) / MINLC. Emit one or more bytes of value (129..255) to indicate repeated PC increments: the encoder emits bytes `s+128` for s in 1..127 to add s * MINLC to pc (repeat until v is zero). Each such byte adds one to lcsize.
 - Then compute s = p->line - oldlc. If s in [-63..64] the encoder emits a single byte: for positive s it emits 1..64; for negative s it emits 65..128 (encoded as 64 - s). If s is outside that range emit a 0 byte followed by a 4-byte signed big-endian delta s. Each emitted token increases `lcsize` appropriately.
 - At the end the encoder pads the `lc` stream to an even length by emitting bytes of value 129 until `lcsize` is even.

Decoder guidance
- To decode the `lc` stream:
  1. Read bytes until you reach the stream length `lcsize` recorded in the header.
  2. Maintain `pc` and `line`. For each byte b:
     - If b >= 129: increment PC by (b - 128) * MINLC.
     - Else if b == 0: read the next 4 bytes as a signed 32-bit delta and add to `line`.
     - Else if 1 <= b <= 64: add (b) to `line` (small positive delta).
     - Else if 65 <= b <= 128: add (64 - b) (a small negative delta) to `line`.

ELF output cases
----------------
Some `asm.c` writers can produce ELF output (HEADTYPE 5/6 in several files). When the writer emits ELF it writes a standard ELF header followed by program headers describing PT_LOAD segments for text and data and a separate program header for symbol table/line table sizes. The writers set class (32/64), endianness, machine number and use `llput()`/`lput()` helpers to write the canonical Elf64/Elf32 fields. If you need to parse these files, prefer a standard ELF parser — the linker writers produce well-formed ELF headers (though not every optional field is populated).

Dynamic modules and `dlm`
-------------------------
- When linkers are asked to produce a dynamically-loadable module (`-u` or `dlm` = 1), they set a flag and the writers set the top bit on the Plan 9 magic (OR 0x80000000). They also call `asmdyn()` after symbols and LC tables to emit dynamic-relocation metadata. The data emission code calls `dynreloc()` for references that must be resolved at load time.

Special cases and loader expectations
- Entry value: the writers compute the entry via `entryvalue()` which either accepts a numeric expression (`-E 0x...`) or looks up a named symbol in the symbol table (default `_main` or `INITENTRY`). For plan9-style headers the entry may be written twice (lower word, high-word) or masked by `PADDR()` depending on architecture.
- HEADR rounding / INITRND: the writer may pad text/data segments to align with large boundaries (e.g., 0x10000 or 4096) using INITRND; file offsets are computed with rnd(HEADR+textsize, INITRND) in many writers.
- Symbol name special encodings: `putsymb()` uses a two-byte pair format for 'z'/'Z' name entries — pay attention when reconstructing paths from the symbol blob.

Quick checklist for a parser that wants to reliably read 9front executables
-----------------------------------------------------------------------
1. Read the first 4 bytes. If they equal 0x7f 'E' 'L' 'F' parse as ELF. Otherwise treat as an integer magic.
2. If the high bit (0x80000000) is set it is a dlm. Strip that bit for architecture identification if you need to.
3. If magic equals the arch Plan 9 magic, read the Plan 9 header (text,datsize,bsssize,symsize,entry,aux,lcsize, maybe an llput of entry). Consult the target arch `asm.c` for exact ordering — the writers differ only in whether they write an extra high-word and in the magic constant.
4. With text and data sizes in hand compute file offsets for the symbol blob and lc table. Read `symsize` bytes and parse symbol entries using the exact `putsymb()` rules:
   - read (optionally) an upper 32-bit value if the header wrote it
   - read a low 32-bit value
   - read a type byte b (the writer writes `t + 0x80`)
   - parse the name: if type is `z` or `Z` parse the 2-byte pair encoding until double-zero, otherwise read a NUL-terminated ASCII string
5. Read the lc table length `lcsize` and decode the variable-length opcodes described above to recover pc → source line mappings.

References (source locations used to write this document)
- `sys/src/cmd/9l/asm.c` — amd64 writer: header magic computation, symbol & LC writing, ELF branch.
- `sys/src/cmd/7l/asm.c` — arm64 writer: Plan 9 header variant for arm64 and llput(entry) usage.
- `sys/src/cmd/6l/asm.c`, `sys/src/cmd/8l/asm.c`, `sys/src/cmd/5l/asm.c`, and other `*/asm.c` files — demonstrate the common writer pattern and per-arch differences. See also each arch's `asmsym()` and `asmlc()` implementations in these files.
- `sys/src/cmd/*l/obj.c` — object readers contain the reverse logic for many of the structures (e.g., how ldobj expects text/data layout, `objfile()` archive handling and how `zaddr()` expects symbol indices). These files show how the header fields are consumed by downstream tools.

Appendix: examples and real snippets
- Plan 9 header (example canonical layout, words are 4-byte little-endian unless otherwise noted):

  offset 0x00: magic (4 bytes)         -- e.g. 4*ARCH*ARCH + 7, possibly OR with 0x00008000 and 0x80000000 for dlm
  offset 0x04: textsize (4 bytes)
  offset 0x08: datsize  (4 bytes)
  offset 0x0C: bsssize  (4 bytes)
  offset 0x10: symsize  (4 bytes)
  offset 0x14: entry_lo (4 bytes)      -- sometimes masked with PADDR()
  offset 0x18: aux/zero  (4 bytes)
  offset 0x1C: lcsize   (4 bytes)
  offset 0x20: entry_hi (8 bytes)      -- on some 64-bit writers the full 64-bit entry value is appended with llput

- Symbol blob entry (as written by `putsymb()`):
  - optional high-word (4 bytes) when HEADTYPE indicates 64-bit value
  - low 32-bit value (4 bytes)
  - one byte: `type + 0x80`
  - name bytes: NUL-terminated ASCII or special `z`/`Z` pair-encoded sequence terminated by two NUL bytes

- LC table encoding summary (decoder pseudocode):

  pc = INITTEXT
  line = initial_line
  while bytes remain:
    b = read_byte()
    if b >= 129:
      pc += (b - 128) * MINLC
      continue
    if b == 0:
      delta = read_s32()
      line += delta
      continue
    if 1 <= b <= 64:
      line += (b)
      continue
    if 65 <= b <= 128:
      line += (64 - b)

Follow-ups / next improvements
- I can produce a small C parser utility (like the object-stream parser earlier) that reads a Plan 9 header, dumps text/data sizes, parses the symbol blob and LC stream, and prints a human-readable listing. If you want that, tell me which architecture to prioritize (arm64 was your stated priority earlier) and I will implement it.

This document was produced by inspecting the `asmb()` family across architectures in `sys/src/cmd/*l/asm.c` and the symbol/line helpers in the same directories. If you want, I can augment this page with more per-architecture exact magic values and a binary example excerpt of a real executable from your tree.
