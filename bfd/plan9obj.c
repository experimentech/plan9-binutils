/* Plan 9 Object File BFD backend - implements 9front opcode stream format
 * Copyright (C) 2025 Free Software Foundation, Inc.
 * Based on authoritative 9front source documentation
 *
 * Phase 1: minimal integration for GNU ld. We synthesize approximate
 * section sizes and a symbol table from the opcode stream so the linker
 * can at least enumerate and place code/data. No relocation records yet.
 */

#include "sysdep.h"
#include "bfd.h"
#include "libbfd.h"
#include "plan9obj.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "libbfd.h"
#include <stdlib.h>

/* Debugging helper: guard plan9obj internal diagnostics behind an
   environment-controlled function so developers can re-enable them when
   needed without editing source. The helper checks PLAN9OBJ_DEBUG and
   BFD_VERBOSE once at runtime to avoid repeated getenv cost. */
static int plan9obj_debug_enabled = -1;
static void
plan9obj_dbg (bfd *abfd, const char *fmt, ...)
{
    va_list ap;
    if (plan9obj_debug_enabled < 0)
    {
        const char *e1 = getenv ("PLAN9OBJ_DEBUG");
        const char *e2 = getenv ("BFD_VERBOSE");
        plan9obj_debug_enabled = ((e1 && *e1) || (e2 && *e2)) ? 1 : 0;
    }
    if (!plan9obj_debug_enabled)
        return;

    va_start (ap, fmt);
    if (abfd && /* if bfd has a printf-like helper, prefer it */ 0) {
        /* Placeholder: prefer bfd-specific printing if available. */
        vfprintf (stderr, fmt, ap);
    } else {
        vfprintf (stderr, fmt, ap);
    }
    va_end (ap);
}

/* Convenience macro to preserve call site brevity; callers must have 'abfd' in scope. */
#define PLAN9OBJ_DBG(...) plan9obj_dbg (abfd, __VA_ARGS__)

/* Minimal AArch64 relocation howtos for Plan 9 object usage. We only need
     a couple to link simple programs: absolute 64-bit and branch/call 26-bit. */
static reloc_howto_type plan9obj_aarch64_abs64_howto =
    HOWTO (BFD_RELOC_64,        /* type */
                 0,                   /* rightshift */
                 3,                   /* size (3==8 bytes) */
                 64,                  /* bitsize */
                 false,               /* pc_relative */
                 0,                   /* bitpos */
                 complain_overflow_bitfield,
                 NULL, /* special_function */
                 "ABS64",            /* name */
                 false,               /* partial_inplace */
                 0,                   /* src_mask */
                 0,                   /* dst_mask */
                 false);              /* pcrel_offset */

static reloc_howto_type plan9obj_aarch64_call26_howto =
    HOWTO (BFD_RELOC_AARCH64_CALL26,
                 2,                   /* rightshift: low 2 bits not encoded */
                 2,
                 26,
                 true,                /* pc-relative */
                 0,
                 complain_overflow_signed,
         NULL,
                 "CALL26",
                 false,
                 0,
                 0,
                 true);

/* Plan 9 Object File Detection - from 9front isobjfile() */
#define PLAN9_OBJ_MAGIC1_OFFSET 2
#define PLAN9_OBJ_MAGIC2_OFFSET 3  
#define PLAN9_OBJ_MAGIC3_OFFSET 4
#define PLAN9_OBJ_MAGIC_BYTE 1
#define PLAN9_OBJ_MAGIC_LT '<'

/* Opcodes for arm64 from 9front sys/src/cmd/7c/7.out.h */
/* Verified via a small dump program in this workspace. */
#define ATEXT    309    /* Text section start */
#define ADATA    310    /* Data initialization */
#define AGLOBL   311    /* Global symbol */
#define ANAME    313    /* Symbol name record */
#define ASIGNAME 320    /* Symbol name with signature */
#define AEND     323    /* End of object */

/* Address encoding is fixed-format for arm64; no T_* flags needed here. */

/* Symbol table limits - from *.out.h */
#define NSYM 50     /* Local symbol table size */
#define NSNAME 8    /* String constant size */

/* Minimal subset of D_* constants for arm64 from 7.out.h (values verified) */
#define D_GOK    0
#define D_NONE   1
#define D_EXTERN 2
#define D_STATIC 3
#define D_AUTO   4
#define D_PARAM  5
#define D_BRANCH 6
#define D_OREG   7
#define D_XPRE   8
#define D_XPOST  9
#define D_CONST  10
#define D_DCONST 11
#define D_FCONST 12
#define D_SCONST 13
#define D_REG    14
#define D_SP     15
#define D_FREG   16
#define D_VREG   17
#define D_SPR    18
#define D_SHIFT  22
#define D_EXTREG 27
#define D_ROFF   28
#define D_COND   29

/* Plan 9 object file private data */
struct plan9_obj_tdata {
    asymbol **symbols;           /* exported symbol vector for BFD */
    unsigned int symbol_count;   /* number of exported symbols */
    asection *text_section;
    asection *data_section;
    asection *bss_section;
    bfd_size_type text_size;
    bfd_size_type data_size;
    /* Relocations synthesized from opcode stream. */
    arelent **text_relocs;
    long text_reloc_count;
    arelent **data_relocs;
    long data_reloc_count;
};

#define plan9_obj_tdata(bfd) ((struct plan9_obj_tdata *)(bfd)->tdata.any)

/* Forward declarations */
bfd_cleanup plan9_object_p (bfd *);
bool plan9obj_mkobject (bfd *);
bool plan9obj_write_object_contents (bfd *);
long plan9obj_get_symtab_upper_bound (bfd *);
long plan9obj_canonicalize_symtab (bfd *, asymbol **);
asymbol *plan9obj_make_empty_symbol (bfd *);
void plan9obj_get_symbol_info (bfd *, asymbol *, symbol_info *);
void plan9obj_print_symbol (bfd *abfd, void *filep, asymbol *symbol, bfd_print_symbol_type how);
static bool parse_plan9_object (bfd *abfd);
static reloc_howto_type *plan9obj_aarch64_howto_from_code (bfd_reloc_code_real_type code);


/* Object file detection - implements 9front isobjfile() logic */
bfd_cleanup
plan9_object_p (bfd *abfd)
{
    unsigned char buf[5];
    
    /* Read first 5 bytes */
    if (bfd_seek (abfd, 0, SEEK_SET) != 0)
        return NULL;
    
    if (bfd_read (buf, 5, abfd) != 5)
        return NULL;
    
    /* Check Plan 9 object file magic pattern */
    if ((buf[2] == PLAN9_OBJ_MAGIC_BYTE && buf[3] == PLAN9_OBJ_MAGIC_LT) ||
        (buf[3] == PLAN9_OBJ_MAGIC_BYTE && buf[4] == PLAN9_OBJ_MAGIC_LT))
    {
          /* This looks like a Plan 9 object file.  Format is already
              preset by the caller (bfd_check_format). */
        /* Default to aarch64 in this workspace; safe fallback if unknown. */
        if (!bfd_set_arch_mach (abfd, bfd_arch_aarch64, bfd_mach_aarch64))
            bfd_set_arch_mach (abfd, bfd_arch_unknown, 0);
        /* Build sections now; parsing is best-effort and non-fatal here. */
        if (!plan9obj_mkobject (abfd))
            return NULL;
        /* Try to parse to size sections and collect symbols, but don't fail
           recognition if heuristics do not fully parse the file. */
        (void) parse_plan9_object (abfd);
        return _bfd_no_cleanup;
    }
    
    return NULL;
}

/* Create Plan 9 object file structure */
bool
plan9obj_mkobject (bfd *abfd)
{
    struct plan9_obj_tdata *tdata;
    size_t amt = sizeof (struct plan9_obj_tdata);
    
    tdata = (struct plan9_obj_tdata *) bfd_zalloc (abfd, amt);
    if (tdata == NULL)
        return false;
    
    abfd->tdata.any = tdata;
    
    /* Create standard sections */
    tdata->text_section = bfd_make_section_with_flags (abfd, ".text",
        SEC_CODE | SEC_LOAD | SEC_ALLOC | SEC_HAS_CONTENTS);
    
    tdata->data_section = bfd_make_section_with_flags (abfd, ".data", 
        SEC_DATA | SEC_LOAD | SEC_ALLOC | SEC_HAS_CONTENTS);
    
    tdata->bss_section = bfd_make_section_with_flags (abfd, ".bss",
        SEC_ALLOC);
    
    if (!tdata->text_section || !tdata->data_section || !tdata->bss_section)
        return false;
    
    /* Mark that this object will expose symbols (tools like nm/objdump key off this). */
    abfd->flags |= HAS_SYMS | HAS_LOCALS;

    /* Try to parse now to size sections and collect symbols.
       If heuristics fail, keep the object recognized with empty sections. */
    (void) parse_plan9_object (abfd);
    return true;
}

/* Forward declare plan9_address defined in header to satisfy prototypes here. */
struct plan9_address;

/* Parse address encoding for arm64 .7 objects (based on 9front libmach 7obj.c) */
static unsigned int
parse_plan9_address (bfd *abfd, unsigned char *data, unsigned int offset,
                     size_t size, asymbol **local_syms, struct plan9_address *addr)
{
    unsigned int pos = offset;

    if (pos >= size) return 0;
    memset (addr, 0, sizeof (*addr));
    addr->type = data[pos++];
    if (pos >= size) return 0;
    addr->reg = data[pos++];
    if (pos >= size) return 0;
    addr->sym_index = data[pos++];
    if (pos >= size) return 0;
    addr->name = data[pos++];
    addr->symbol = NULL;

    switch (addr->type) {
        default:
            /* No extra payload */
            break;
        case D_OREG:
        case D_XPRE:
        case D_XPOST:
        case D_CONST:
        case D_BRANCH:
        case D_SHIFT:
        case D_EXTREG:
        case D_ROFF:
        case D_SPR: {
            if (pos + 3 >= size) return 0;
            int32_t off = (int32_t)(data[pos] | (data[pos+1] << 8) | (data[pos+2] << 16) | (data[pos+3] << 24));
            addr->offset = (int64_t) off;
            pos += 4;
            break;
        }
        case D_DCONST: {
            if (pos + 7 >= size) return 0;
            /* little-endian 64-bit */
            uint64_t lo = (uint32_t)(data[pos] | (data[pos+1] << 8) | (data[pos+2] << 16) | (data[pos+3] << 24));
            uint64_t hi = (uint32_t)(data[pos+4] | (data[pos+5] << 8) | (data[pos+6] << 16) | (data[pos+7] << 24));
            addr->offset = (int64_t)(lo | (hi << 32));
            pos += 8;
            break;
        }
        case D_SCONST: {
            if (pos + NSNAME - 1 >= size) return 0;
            memcpy (addr->sconst, data + pos, NSNAME);
            pos += NSNAME;
            break;
        }
        case D_FCONST: {
            if (pos + 7 >= size) return 0;
            memcpy (addr->fconst, data + pos, 8);
            pos += 8;
            break;
        }
    }

    if (addr->sym_index < NSYM && local_syms[addr->sym_index])
        addr->symbol = local_syms[addr->sym_index];

    return pos - offset;
}

/* Parse Plan 9 object file - implements ldobj() logic from 9front (subset) */
static bool
parse_plan9_object (bfd *abfd)
{
    unsigned char *data;
    size_t size;
    unsigned int pos = 0;
    asymbol *local_syms[NSYM] = {0};
    /* Track the ANAME/ASIGNAME type byte for possible heuristics later. */
    unsigned char local_types[NSYM] = {0};
    /* We'll collect only meaningful symbols (with a section) to expose. */
    asymbol *collected[256];
    unsigned int collected_count = 0;
    
    /* Read entire file into memory */
    size = bfd_get_file_size (abfd);
    if (size == 0 || size > 0x10000000) /* Sanity check: 256MB max */
        return false;
    
    data = (unsigned char *) bfd_malloc (size);
    if (!data)
        return false;
    
    if (bfd_seek (abfd, 0, SEEK_SET) != 0 ||
        bfd_read (data, size, abfd) != size) {
        free (data);
        return false;
    }
    
    /* Prepare reloc arrays (grow on demand in a simple doubling scheme). */
    long text_rel_cap = 8, data_rel_cap = 8;
    plan9_obj_tdata(abfd)->text_relocs = (arelent **) bfd_zalloc (abfd, text_rel_cap * sizeof (arelent *));
    plan9_obj_tdata(abfd)->data_relocs = (arelent **) bfd_zalloc (abfd, data_rel_cap * sizeof (arelent *));
    plan9_obj_tdata(abfd)->text_reloc_count = 0;
    plan9_obj_tdata(abfd)->data_reloc_count = 0;

    /* Parse opcode stream */
    unsigned int count_atext = 0, count_adata = 0, count_aglobl = 0, count_other = 0;
    bool in_name_prefix = true; /* ANAME/ASIGNAME records usually come first, but can appear anywhere */
    while (pos + 1 < size) {
        unsigned int opcode_pos = pos;
        uint16_t opcode = data[pos] | (data[pos+1] << 8);
        pos += 2;

        /* Handle ANAME/ASIGNAME records anywhere in the stream. */
        if (opcode == ANAME || opcode == ASIGNAME) {
            bool parsed_name = false;
            unsigned int save = pos;

            if (opcode == ANAME) {
                /* ANAME layout: [v][o][name\0] */
                if (pos + 2 < size) {
                    uint8_t v = data[pos];
                    uint8_t o = data[pos + 1];
                    unsigned int nstart = pos + 2;
                    unsigned int nend = nstart;
                    /* Find NUL within a reasonable bound */
                    while (nend < size && (nend - nstart) < 512 && data[nend] != 0)
                        nend++;
                    if (nend < size && o < NSYM && v < 0x80 && (nend > nstart)) {
                        /* Quick sanity: bytes printable or path-ish */
                        bool ok = true;
                        for (unsigned int i = nstart; i < nend; i++) {
                            unsigned char ch = data[i];
                            if (ch < 0x20 || ch > 0x7e) { ok = false; break; }
                        }
                        if (ok) {
                            char *name = (char *)(data + nstart);
                            asymbol *sym = bfd_make_empty_symbol (abfd);
                            if (sym) {
                                sym->name = bfd_alloc (abfd, (nend - nstart) + 1);
                                if (sym->name) {
                                    memcpy ((char*)sym->name, name, (nend - nstart) + 1);
                                    sym->value = 0;
                                    sym->flags = BSF_LOCAL;
                                    local_syms[o] = sym;
                                    local_types[o] = v;
                                    /* Note: not exported yet; section assigned later */
                                    pos = nend + 1;
                                    parsed_name = true;
                                }
                            }
                        }
                    }
                }
            } else if (opcode == ASIGNAME) {
                /* ASIGNAME layout: [sig32][v][o][name\0] */
                uint8_t v = data[pos];
                /* Re-read with signature present */
                pos = save;
                if (pos + 6 < size) {
                    unsigned int nstart = pos + 6;
                    v = data[pos + 4];
                    uint8_t o = data[pos + 5];
                    unsigned int nend = nstart;
                    while (nend < size && (nend - nstart) < 512 && data[nend] != 0)
                        nend++;
                    if (nend < size && o < NSYM && v < 0x80 && (nend > nstart)) {
                        bool ok = true;
                        for (unsigned int i = nstart; i < nend; i++) {
                            unsigned char ch = data[i];
                            if (ch < 0x20 || ch > 0x7e) { ok = false; break; }
                        }
                        if (ok) {
                            char *name = (char *)(data + nstart);
                            asymbol *sym = bfd_make_empty_symbol (abfd);
                            if (sym) {
                                sym->name = bfd_alloc (abfd, (nend - nstart) + 1);
                                if (sym->name) {
                                    memcpy ((char*)sym->name, name, (nend - nstart) + 1);
                                    sym->value = 0;
                                    sym->flags = BSF_LOCAL;
                                    local_syms[o] = sym;
                                    local_types[o] = v;
                                    /* Not exported yet; section assigned later */
                                    pos = nend + 1;
                                    parsed_name = true;
                                }
                            }
                        }
                    }
                }
            }
            if (parsed_name) {
                /* If we were still in the initial name prefix, stay or drop depending on content. */
                continue; /* Next record */
            }
            /* Failed to parse as name; rewind and treat regularly. */
            pos = save;
            if (in_name_prefix)
                in_name_prefix = false;
        } else if (in_name_prefix) {
            /* First non-name record ends the initial prefix. */
            in_name_prefix = false;
            /* pos already advanced after opcode; fall through to normal parsing. */
        }

        /* Normal instruction path and section sizing */
        if (opcode == AEND) {
            break; /* End of object */
        } else {
            /* Regular instruction - read reg/flag (1), skip 4-byte line, then parse addresses */
            if (pos >= size) break;
            uint8_t rflag = data[pos++];
            if (pos + 3 >= size) break;
            pos += 4;
            struct plan9_address from_addr;
            unsigned int consumed = parse_plan9_address (abfd, data, pos, size, local_syms, &from_addr);
            if (consumed == 0) break;
            pos += consumed;
            if (rflag & 0x40) {
                struct plan9_address from3;
                consumed = parse_plan9_address (abfd, data, pos, size, local_syms, &from3);
                if (consumed == 0) break;
                pos += consumed;
            }

            /* Parse 'to' operand */
            struct plan9_address to_addr;
            consumed = parse_plan9_address (abfd, data, pos, size, local_syms, &to_addr);
            if (consumed == 0) break;
            pos += consumed;

            /* Process instruction based on opcode */
            if (opcode == ATEXT) {
                /* Text section symbol comes from first address 'a' (from_addr). */
                if (from_addr.symbol && from_addr.type == D_OREG &&
                    (from_addr.name == D_STATIC || from_addr.name == D_EXTERN)) {
                    from_addr.symbol->section = plan9_obj_tdata(abfd)->text_section;
                    from_addr.symbol->value = plan9_obj_tdata(abfd)->text_size;
                    from_addr.symbol->flags = BSF_FUNCTION | BSF_GLOBAL;
                    bool seen = false;
                    for (unsigned int k = 0; k < collected_count; k++) if (collected[k] == from_addr.symbol) { seen = true; break; }
                    if (!seen && collected_count < (unsigned) (sizeof collected / sizeof collected[0]))
                        collected[collected_count++] = from_addr.symbol;
                }
                /* Crude heuristic: treat each ATEXT instruction as 4 bytes. */
                plan9_obj_tdata(abfd)->text_size += 4;
                /* If this ATEXT also encodes a branch target (from_addr with symbol),
                   synthesize a CALL26 relocation at current text offset. */
                if (from_addr.symbol) {
                    if (plan9_obj_tdata(abfd)->text_reloc_count >= text_rel_cap) {
                        long newcap = text_rel_cap * 2;
                        arelent **nr = (arelent **) bfd_zalloc (abfd, newcap * sizeof (arelent *));
                        if (!nr) { free (data); return false; }
                        memcpy (nr, plan9_obj_tdata(abfd)->text_relocs, text_rel_cap * sizeof (arelent *));
                        plan9_obj_tdata(abfd)->text_relocs = nr;
                        text_rel_cap = newcap;
                    }
                    arelent *r = (arelent *) bfd_zalloc (abfd, sizeof (arelent));
                    if (!r) { free (data); return false; }
                    r->howto = &plan9obj_aarch64_call26_howto;
                    r->sym_ptr_ptr = (asymbol **) bfd_zalloc (abfd, sizeof (asymbol *));
                    if (!r->sym_ptr_ptr) { free (data); return false; }
                    *(r->sym_ptr_ptr) = from_addr.symbol;
                    r->address = plan9_obj_tdata(abfd)->text_size - 4; /* relocation at this instruction */
                    r->addend = 0;
                    PLAN9OBJ_DBG ("[plan9obj] added text-reloc sym=%s addr=0x%llx\n", from_addr.symbol?from_addr.symbol->name:"(null)", (unsigned long long) r->address);
                    plan9_obj_tdata(abfd)->text_relocs[plan9_obj_tdata(abfd)->text_reloc_count++] = r;
                }
            } else if (opcode == ADATA) {
                count_adata++;
                /* Initialized data symbol comes from first address. */
                if (from_addr.symbol && from_addr.type == D_OREG &&
                    (from_addr.name == D_STATIC || from_addr.name == D_EXTERN)) {
                    from_addr.symbol->section = plan9_obj_tdata(abfd)->data_section;
                    from_addr.symbol->value = plan9_obj_tdata(abfd)->data_size;
                    from_addr.symbol->flags = BSF_OBJECT | BSF_GLOBAL;
                    bool seen = false;
                    for (unsigned int k = 0; k < collected_count; k++) if (collected[k] == from_addr.symbol) { seen = true; break; }
                    if (!seen && collected_count < (unsigned) (sizeof collected / sizeof collected[0]))
                        collected[collected_count++] = from_addr.symbol;
                }
                /* Diagnostic: print operand details for analysis (temporary) */
                {
                    const char *from_sym = from_addr.symbol ? from_addr.symbol->name : "(null)";
                    const char *to_sym = to_addr.symbol ? to_addr.symbol->name : "(null)";
                    PLAN9OBJ_DBG ("[plan9obj] ADATA@0x%08x from(t=%u n=%u sidx=%u off=%lld sym=%s) to(t=%u n=%u sidx=%u off=%lld sym=%s)\n",
                             opcode_pos,
                             (unsigned int) from_addr.type, (unsigned int) from_addr.name, (unsigned int) from_addr.sym_index, (long long) from_addr.offset, from_sym,
                             (unsigned int) to_addr.type, (unsigned int) to_addr.name, (unsigned int) to_addr.sym_index, (long long) to_addr.offset, to_sym);
                }

                /* Determine whether this ADATA encodes a relocation. Heuristics:
                   - Prefer the 'to' operand as the referenced symbol (it's the value being written);
                   - If 'to' has no symbol but the 'from' operand is not a definition (i.e. not D_STATIC/D_EXTERN),
                     then 'from' likely encodes a symbol reference and should be relocated.
                   - Otherwise no relocation is necessary (literal constants, local data initialization). */
                {
                    asymbol *rel_sym = NULL;
                    long long rel_addend = 0;
                    if (to_addr.symbol) {
                        rel_sym = to_addr.symbol;
                        rel_addend = to_addr.offset;
                    } else if (from_addr.symbol && from_addr.type == D_OREG &&
                               !(from_addr.name == D_STATIC || from_addr.name == D_EXTERN)) {
                        rel_sym = from_addr.symbol;
                        rel_addend = from_addr.offset;
                    }

                    /* Size the datum (assume 8-byte pointers/words for ABS64). */
                    unsigned long long datum_start = plan9_obj_tdata(abfd)->data_size;
                    plan9_obj_tdata(abfd)->data_size += 8;

                    if (rel_sym) {
                        if (plan9_obj_tdata(abfd)->data_reloc_count >= data_rel_cap) {
                            long newcap = data_rel_cap * 2;
                            arelent **nr = (arelent **) bfd_zalloc (abfd, newcap * sizeof (arelent *));
                            if (!nr) { free (data); return false; }
                            memcpy (nr, plan9_obj_tdata(abfd)->data_relocs, data_rel_cap * sizeof (arelent *));
                            plan9_obj_tdata(abfd)->data_relocs = nr;
                            data_rel_cap = newcap;
                        }
                        arelent *r = (arelent *) bfd_zalloc (abfd, sizeof (arelent));
                        if (!r) { free (data); return false; }
                        r->howto = &plan9obj_aarch64_abs64_howto;
                        r->sym_ptr_ptr = (asymbol **) bfd_zalloc (abfd, sizeof (asymbol *));
                        if (!r->sym_ptr_ptr) { free (data); return false; }
                        *(r->sym_ptr_ptr) = rel_sym;
                        r->address = (bfd_vma) datum_start;
                        r->addend = rel_addend;
                        PLAN9OBJ_DBG ("[plan9obj] added data-reloc sym=%s addr=0x%llx addend=%lld\n",
                                 rel_sym?rel_sym->name:"(null)", (unsigned long long) r->address, (long long) r->addend);
                        plan9_obj_tdata(abfd)->data_relocs[plan9_obj_tdata(abfd)->data_reloc_count++] = r;
                    }
                }
            } else if (opcode == AGLOBL) {
                count_aglobl++;
                /* Uninitialized globals are BSS; based on first address. */
                if (from_addr.symbol && from_addr.type == D_OREG &&
                    (from_addr.name == D_STATIC || from_addr.name == D_EXTERN)) {
                    from_addr.symbol->section = plan9_obj_tdata(abfd)->bss_section;
                    from_addr.symbol->value = 0;
                    from_addr.symbol->flags = BSF_OBJECT | BSF_GLOBAL;
                    bool seen = false;
                    for (unsigned int k = 0; k < collected_count; k++) if (collected[k] == from_addr.symbol) { seen = true; break; }
                    if (!seen && collected_count < (unsigned) (sizeof collected / sizeof collected[0]))
                        collected[collected_count++] = from_addr.symbol;
                }
            } else {
                /* Treat all other opcodes as code instructions. Approx 4 bytes each. */
                plan9_obj_tdata(abfd)->text_size += 4;
                count_other++;
            }
        }
    }
    
    /* Export only the collected symbols (with sections) to BFD */
    if (collected_count > 0) {
        plan9_obj_tdata(abfd)->symbol_count = collected_count;
        plan9_obj_tdata(abfd)->symbols = (asymbol **) bfd_alloc (abfd, collected_count * sizeof (asymbol *));
        if (plan9_obj_tdata(abfd)->symbols) {
            memcpy (plan9_obj_tdata(abfd)->symbols, collected, collected_count * sizeof (asymbol *));
            for (unsigned int i = 0; i < collected_count; i++) {
                const char *nm = plan9_obj_tdata(abfd)->symbols[i]->name;
                asection *s = plan9_obj_tdata(abfd)->symbols[i]->section;
                unsigned long long val = plan9_obj_tdata(abfd)->symbols[i]->value;
                PLAN9OBJ_DBG ("[plan9obj] sym[%u] name='%s' sec=%s val=0x%llx\n", i, nm?nm:"(null)", s?s->name:"(null)", val);
            }
        }
    }
    
    /* Apply synthesized sizes to sections so higher layers see something. */
    if (plan9_obj_tdata(abfd)->text_section)
        plan9_obj_tdata(abfd)->text_section->size = plan9_obj_tdata(abfd)->text_size;
    if (plan9_obj_tdata(abfd)->data_section)
        plan9_obj_tdata(abfd)->data_section->size = plan9_obj_tdata(abfd)->data_size;

    /* Debug: report synthesized sizes and symbol count. */
    PLAN9OBJ_DBG ("[plan9obj] ATEXT=%u ADATA=%u AGLOBL=%u OTHER=%u -> text=%llu data=%llu syms=%u\n",
             count_atext, count_adata, count_aglobl, count_other,
             (unsigned long long) plan9_obj_tdata(abfd)->text_size,
             (unsigned long long) plan9_obj_tdata(abfd)->data_size,
             plan9_obj_tdata(abfd)->symbol_count);
    PLAN9OBJ_DBG ("[plan9obj] relocs: text=%ld data=%ld\n", plan9_obj_tdata(abfd)->text_reloc_count, plan9_obj_tdata(abfd)->data_reloc_count);

    free (data);
    return true;
}

/* Stub implementations for required BFD functions */
bool
plan9obj_write_object_contents (bfd *abfd ATTRIBUTE_UNUSED)
{
    /* TODO: Implement Plan 9 object file writing */
    return false;
}

long
plan9obj_get_symtab_upper_bound (bfd *abfd)
{
    struct plan9_obj_tdata *tdata = plan9_obj_tdata (abfd);
    if (!tdata || tdata->symbol_count == 0)
        return 0;
    return (tdata->symbol_count + 1) * sizeof (asymbol *);
}

long
plan9obj_canonicalize_symtab (bfd *abfd, asymbol **syms)
{
    struct plan9_obj_tdata *tdata = plan9_obj_tdata (abfd);
    unsigned int i;
    
    if (!tdata || !tdata->symbols)
        return 0;
    
    for (i = 0; i < tdata->symbol_count; i++) {
        syms[i] = tdata->symbols[i];
    }
    syms[i] = NULL;
    
    return tdata->symbol_count;
}

asymbol *
plan9obj_make_empty_symbol (bfd *abfd)
{
    size_t amt = sizeof (asymbol);
    asymbol *sym = (asymbol *) bfd_zalloc (abfd, amt);
    if (sym)
        sym->the_bfd = abfd;
    return sym;
}

void
plan9obj_get_symbol_info (bfd *abfd ATTRIBUTE_UNUSED, asymbol *symbol,
                      symbol_info *ret)
{
    bfd_symbol_info (symbol, ret);
}

void
plan9obj_print_symbol (bfd *abfd, void *filep, asymbol *symbol, bfd_print_symbol_type how)
{
    FILE *file = (FILE*)filep;
    switch (how) {
        case bfd_print_symbol_name:
            fprintf (file, "%s", symbol->name);
            break;
        default:
            bfd_print_symbol_vandf (abfd, filep, symbol);
            fprintf (file, " %s", symbol->name ? symbol->name : "");
            break;
    }
}

/* Relocation jump table functions */
long
plan9obj_get_reloc_upper_bound (bfd *abfd, asection *sec)
{
    struct plan9_obj_tdata *t = plan9_obj_tdata (abfd);
    if (!t || !sec) return 0;
    if (sec == t->text_section)
        return (t->text_reloc_count + 1) * (long) sizeof (arelent *);
    if (sec == t->data_section)
        return (t->data_reloc_count + 1) * (long) sizeof (arelent *);
    return 0;
}

long
plan9obj_canonicalize_reloc (bfd *abfd, asection *sec, arelent **relpp, asymbol **symbols ATTRIBUTE_UNUSED)
{
    struct plan9_obj_tdata *t = plan9_obj_tdata (abfd);
    long i;
    if (!t || !sec || !relpp) return 0;
    if (sec == t->text_section) {
        for (i = 0; i < t->text_reloc_count; i++)
            relpp[i] = t->text_relocs[i];
        relpp[i] = NULL;
        return t->text_reloc_count;
    }
    if (sec == t->data_section) {
        for (i = 0; i < t->data_reloc_count; i++)
            relpp[i] = t->data_relocs[i];
        relpp[i] = NULL;
        return t->data_reloc_count;
    }
    return 0;
}

static reloc_howto_type *
plan9obj_aarch64_howto_from_code (bfd_reloc_code_real_type code)
{
    switch (code) {
        case BFD_RELOC_64: return &plan9obj_aarch64_abs64_howto;
        case BFD_RELOC_AARCH64_CALL26: return &plan9obj_aarch64_call26_howto;
        default: return NULL;
    }
}

reloc_howto_type *
plan9obj_bfd_reloc_type_lookup (bfd *abfd ATTRIBUTE_UNUSED, bfd_reloc_code_real_type code)
{
    return plan9obj_aarch64_howto_from_code (code);
}

reloc_howto_type *
plan9obj_bfd_reloc_name_lookup (bfd *abfd ATTRIBUTE_UNUSED, const char *name)
{
    if (!name) return NULL;
    if (strcmp (name, "ABS64") == 0) return &plan9obj_aarch64_abs64_howto;
    if (strcmp (name, "CALL26") == 0 || strcmp (name, "AARCH64_CALL26") == 0)
        return &plan9obj_aarch64_call26_howto;
    return NULL;
}

/* Expose parse helper via a post-create hook if needed later. */

/* Plan 9 object file target vector is defined in bfd-plan9.c to avoid duplication. */