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

/* Address encoding flags - from l.h */
#define T_TYPE    (1<<0)
#define T_INDEX   (1<<1)
#define T_OFFSET  (1<<2)
#define T_FCONST  (1<<3)
#define T_SYM     (1<<4)
#define T_SCONST  (1<<5)
#define T_64      (1<<6)

/* Symbol table limits - from *.out.h */
#define NSYM 50     /* Local symbol table size */
#define NSNAME 8    /* String constant size */

/* Plan 9 object file private data */
struct plan9_obj_tdata {
    asymbol **symbols;
    unsigned int symbol_count;
    asection *text_section;
    asection *data_section;
    asection *bss_section;
    bfd_size_type text_size;
    bfd_size_type data_size;
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
        /* This looks like a Plan 9 object file */
        bfd_set_format (abfd, bfd_object);
        /* Default to aarch64 in this workspace; safe fallback if unknown. */
        if (!bfd_set_arch_mach (abfd, bfd_arch_aarch64, bfd_mach_aarch64))
            bfd_set_arch_mach (abfd, bfd_arch_unknown, 0);
        /* Build sections + symbols now so tools can query them. */
        if (!plan9obj_mkobject (abfd))
            return NULL;
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

    /* Parse now to size sections and collect symbols. */
    if (!parse_plan9_object (abfd))
        return false;
    return true;
}

/* Parse address encoding - implements zaddr() logic from 9front */
static unsigned int
parse_plan9_address (bfd *abfd, unsigned char *data, unsigned int offset,
                     asymbol **local_syms, struct plan9_address *addr)
{
    unsigned char t = data[offset];
    unsigned int pos = offset + 1;
    
    memset (addr, 0, sizeof (*addr));
    addr->type_flags = t;
    
    /* Parse fields in order per 9front zaddr() */
    if (t & T_INDEX) {
        if (pos + 1 >= bfd_get_size (abfd)) return 0;
        addr->index = data[pos];
        addr->scale = data[pos + 1];
        pos += 2;
    }
    
    if (t & T_OFFSET) {
        if (pos + 3 >= bfd_get_size (abfd)) return 0;
        addr->offset = (data[pos]) | (data[pos+1] << 8) | 
                      (data[pos+2] << 16) | (data[pos+3] << 24);
        pos += 4;
        
        if (t & T_64) {
            if (pos + 3 >= bfd_get_size (abfd)) return 0;
            uint32_t high = (data[pos]) | (data[pos+1] << 8) | 
                           (data[pos+2] << 16) | (data[pos+3] << 24);
            addr->offset |= ((uint64_t)high << 32);
            pos += 4;
        }
    }
    
    if (t & T_SYM) {
        if (pos >= bfd_get_size (abfd)) return 0;
        uint8_t sym_index = data[pos];
        if (sym_index < NSYM && local_syms[sym_index])
            addr->symbol = local_syms[sym_index];
        pos++;
    }
    
    if (t & T_FCONST) {
        if (pos + 7 >= bfd_get_size (abfd)) return 0;
        memcpy (addr->fconst, data + pos, 8);
        pos += 8;
    }
    
    if (t & T_SCONST) {
        if (pos + NSNAME - 1 >= bfd_get_size (abfd)) return 0;
        memcpy (addr->sconst, data + pos, NSNAME);
        pos += NSNAME;
    }
    
    if (t & T_TYPE) {
        if (pos >= bfd_get_size (abfd)) return 0;
        addr->addr_type = data[pos];
        pos++;
    }
    
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
    unsigned int symbol_count = 0;
    
    /* Read entire file into memory */
    size = bfd_get_size (abfd);
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
    
    /* Parse opcode stream */
    bool in_name_prefix = true; /* ANAME/ASIGNAME records usually come first */
    while (pos + 1 < size) {
        unsigned int opcode_pos = pos;
        uint16_t opcode = data[pos] | (data[pos+1] << 8);
        pos += 2;

        /* Heuristic: try to parse ANAME/ASIGNAME without knowing opcode numbers. */
        if (in_name_prefix) {
            bool parsed_name = false;
            unsigned int save = pos;

            /* Try ANAME layout: [v][o][name\0] */
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
                                symbol_count++;
                                pos = nend + 1;
                                parsed_name = true;
                            }
                        }
                    }
                }
            }

            if (!parsed_name) {
                /* Try ASIGNAME layout: [sig32][v][o][name\0] */
                pos = save; /* reset to after opcode */
                if (pos + 6 < size) {
                    /* uint32_t sig = ... unused for now */
                    unsigned int nstart = pos + 6;
                    uint8_t v = data[pos + 4];
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
                                    symbol_count++;
                                    pos = nend + 1;
                                    parsed_name = true;
                                }
                            }
                        }
                    }
                }
            }

            if (parsed_name)
                continue; /* Handle next record */

            /* First non-name record – resume opcode at saved point */
            in_name_prefix = false;
            pos = save; /* let normal parser consume the rest of this record */
        }

        /* Normal instruction path and section sizing */
        if (opcode == AEND) {
            break; /* End of object */
        } else {
            /* Regular instruction - parse line number and operands */
            if (pos + 3 >= size) break;
            
            uint32_t line = data[pos] | (data[pos+1] << 8) | 
                           (data[pos+2] << 16) | (data[pos+3] << 24);
            pos += 4;
            
            /* Parse 'from' operand */
            struct plan9_address from_addr;
            unsigned int consumed = parse_plan9_address (abfd, data, pos, 
                                                        local_syms, &from_addr);
            if (consumed == 0) break;
            pos += consumed;
            
            /* Parse 'to' operand */
            struct plan9_address to_addr;
            consumed = parse_plan9_address (abfd, data, pos, 
                                           local_syms, &to_addr);
            if (consumed == 0) break;
            pos += consumed;
            
            /* Process instruction based on opcode */
            if (opcode == ATEXT) {
                /* Text section symbol */
                if (to_addr.symbol) {
                    /* Use current synthesized text size as the symbol's value (section-relative). */
                    to_addr.symbol->section = plan9_obj_tdata(abfd)->text_section;
                    to_addr.symbol->value = plan9_obj_tdata(abfd)->text_size;
                    to_addr.symbol->flags = BSF_FUNCTION | BSF_GLOBAL;
                }
                /* Crude heuristic: treat each ATEXT instruction as 4 bytes. */
                plan9_obj_tdata(abfd)->text_size += 4;
            } else if (opcode == ADATA) {
                /* Initialized data goes to .data */
                if (to_addr.symbol) {
                    to_addr.symbol->section = plan9_obj_tdata(abfd)->data_section;
                    to_addr.symbol->value = plan9_obj_tdata(abfd)->data_size;
                    to_addr.symbol->flags = BSF_OBJECT | BSF_GLOBAL;
                }
                /* Approximate each ADATA as 8 bytes of data. */
                plan9_obj_tdata(abfd)->data_size += 8;
            } else if (opcode == AGLOBL) {
                /* Uninitialized globals are typically BSS; default there. */
                if (to_addr.symbol) {
                    to_addr.symbol->section = plan9_obj_tdata(abfd)->bss_section;
                    to_addr.symbol->value = 0; /* BSS offset unknown without size parsing */
                    to_addr.symbol->flags = BSF_OBJECT | BSF_GLOBAL;
                }
            }
        }
    }
    
    /* Store symbol table */
    if (symbol_count > 0) {
        plan9_obj_tdata(abfd)->symbol_count = symbol_count;
        plan9_obj_tdata(abfd)->symbols = (asymbol **) bfd_alloc (abfd, 
                                         symbol_count * sizeof (asymbol *));
        if (plan9_obj_tdata(abfd)->symbols) {
            unsigned int i, j = 0;
            for (i = 0; i < NSYM && j < symbol_count; i++) {
                if (local_syms[i]) {
                    plan9_obj_tdata(abfd)->symbols[j++] = local_syms[i];
                }
            }
        }
    }
    
    /* Apply synthesized sizes to sections so higher layers see something. */
    if (plan9_obj_tdata(abfd)->text_section)
        plan9_obj_tdata(abfd)->text_section->size = plan9_obj_tdata(abfd)->text_size;
    if (plan9_obj_tdata(abfd)->data_section)
        plan9_obj_tdata(abfd)->data_section->size = plan9_obj_tdata(abfd)->data_size;

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
            break;
    }
}

/* Expose parse helper via a post-create hook if needed later. */

/* Plan 9 object file target vector is defined in bfd-plan9.c to avoid duplication. */