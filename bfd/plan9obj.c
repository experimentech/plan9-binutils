/* Plan 9 Object File BFD backend - implements 9front opcode stream format
 * Copyright (C) 2025 Free Software Foundation, Inc.
 * Based on authoritative 9front source documentation
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

/* Opcodes from 9front *.out.h */
#define ANAME 144    /* From 6c/6.out.h - symbol name record */
#define ASIGNAME 145 /* Symbol name with signature */
#define ATEXT 146    /* Text section start */
#define ADATA 147    /* Data initialization */
#define AGLOBL 148   /* Global symbol */
#define AEND 149     /* End of object */

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
};

#define plan9_obj_tdata(bfd) ((struct plan9_obj_tdata *)(bfd)->tdata.any)

/* Forward declarations */
static bfd_cleanup plan9_object_p (bfd *);
static bool plan9_mkobject (bfd *);
static bool plan9_write_object_contents (bfd *);
static long plan9_get_symtab_upper_bound (bfd *);
static long plan9_canonicalize_symtab (bfd *, asymbol **);
static asymbol *plan9_make_empty_symbol (bfd *);
static void plan9_get_symbol_info (bfd *, asymbol *, symbol_info *);

/* Object file detection - implements 9front isobjfile() logic */
static bfd_cleanup
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
        
        /* Set default architecture - we'll determine specific arch during parsing */
        bfd_set_arch_mach (abfd, bfd_arch_unknown, 0);
        
        return _bfd_no_cleanup;
    }
    
    return NULL;
}

/* Create Plan 9 object file structure */
static bool
plan9_mkobject (bfd *abfd)
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

/* Parse Plan 9 object file - implements ldobj() logic from 9front */
static bool
parse_plan9_object (bfd *abfd)
{
    unsigned char *data;
    size_t size;
    unsigned int pos = 0;
    asymbol *local_syms[NSYM] = {0};
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
    while (pos + 1 < size) {
        uint16_t opcode = data[pos] | (data[pos+1] << 8);
        pos += 2;
        
        if (opcode == AEND) {
            break; /* End of object */
        } else if (opcode == ANAME || opcode == ASIGNAME) {
            /* Symbol record */
            uint32_t signature = 0;
            uint8_t sym_type, sym_index;
            char *name;
            
            if (opcode == ASIGNAME) {
                if (pos + 3 >= size) break;
                signature = data[pos] | (data[pos+1] << 8) | 
                           (data[pos+2] << 16) | (data[pos+3] << 24);
                pos += 4;
            }
            
            if (pos + 1 >= size) break;
            sym_type = data[pos++];
            sym_index = data[pos++];
            
            /* Extract null-terminated name */
            name = (char *)(data + pos);
            while (pos < size && data[pos] != 0) pos++;
            if (pos >= size) break;
            pos++; /* Skip null terminator */
            
            /* Create symbol if index is valid */
            if (sym_index < NSYM && symbol_count < 1000) {
                asymbol *sym = bfd_make_empty_symbol (abfd);
                if (sym) {
                    sym->name = bfd_alloc (abfd, strlen(name) + 1);
                    if (sym->name) {
                        strcpy ((char*)sym->name, name);
                        sym->value = 0;
                        sym->flags = BSF_LOCAL;
                        local_syms[sym_index] = sym;
                        symbol_count++;
                    }
                }
            }
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
                    to_addr.symbol->section = plan9_obj_tdata(abfd)->text_section;
                    to_addr.symbol->flags = BSF_FUNCTION | BSF_GLOBAL;
                }
            } else if (opcode == ADATA || opcode == AGLOBL) {
                /* Data section symbol */
                if (to_addr.symbol) {
                    to_addr.symbol->section = plan9_obj_tdata(abfd)->data_section;
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
    
    free (data);
    return true;
}

/* Stub implementations for required BFD functions */
static bool
plan9_write_object_contents (bfd *abfd ATTRIBUTE_UNUSED)
{
    /* TODO: Implement Plan 9 object file writing */
    return false;
}

static long
plan9_get_symtab_upper_bound (bfd *abfd)
{
    struct plan9_obj_tdata *tdata = plan9_obj_tdata (abfd);
    if (!tdata)
        return 0;
    return (tdata->symbol_count + 1) * sizeof (asymbol *);
}

static long
plan9_canonicalize_symtab (bfd *abfd, asymbol **syms)
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

static asymbol *
plan9_make_empty_symbol (bfd *abfd)
{
    size_t amt = sizeof (asymbol);
    asymbol *sym = (asymbol *) bfd_zalloc (abfd, amt);
    if (sym)
        sym->the_bfd = abfd;
    return sym;
}

static void
plan9_get_symbol_info (bfd *abfd ATTRIBUTE_UNUSED, asymbol *symbol,
                      symbol_info *ret)
{
    bfd_symbol_info (symbol, ret);
}

/* Plan 9 object file target definition */
const bfd_target plan9_object_vec = {
    "plan9-object",                     /* name */
    bfd_target_unknown_flavour,         /* flavour */
    BFD_ENDIAN_LITTLE,                  /* byteorder */
    BFD_ENDIAN_LITTLE,                  /* header_byteorder */
    (HAS_RELOC | EXEC_P | HAS_LINENO |  /* object_flags */
     HAS_DEBUG | HAS_SYMS | HAS_LOCALS | WP_TEXT | D_PAGED),
    (SEC_CODE | SEC_DATA | SEC_ROM | SEC_HAS_CONTENTS | SEC_ALLOC | SEC_LOAD |
     SEC_RELOC),                        /* section_flags */
    0,                                  /* symbol_leading_char */
    ' ',                                /* ar_pad_char */
    16,                                 /* ar_max_namelen */
    0,                                  /* match_priority */
    TARGET_KEEP_UNUSED_SECTION_SYMBOLS,
    bfd_getl64, bfd_getl_signed_64, bfd_putl64, /* data */
    bfd_getl32, bfd_getl_signed_32, bfd_putl32,
    bfd_getl16, bfd_getl_signed_16, bfd_putl16,
    bfd_getl64, bfd_getl_signed_64, bfd_putl64, /* hdrs */
    bfd_getl32, bfd_getl_signed_32, bfd_putl32,
    bfd_getl16, bfd_getl_signed_16, bfd_putl16,
    
    { plan9_object_p,                   /* bfd_check_format */
      bfd_generic_archive_p,
      _bfd_dummy_target,
      _bfd_dummy_target },
      
    { _bfd_bool_bfd_false_error,        /* bfd_set_format */
      _bfd_generic_mkarchive,
      _bfd_bool_bfd_false_error,
      _bfd_bool_bfd_false_error },
      
    { _bfd_bool_bfd_false_error,        /* bfd_write_contents */
      _bfd_write_archive_contents,
      _bfd_bool_bfd_false_error,
      _bfd_bool_bfd_false_error },
      
    BFD_JUMP_TABLE_GENERIC (_bfd_generic),
    BFD_JUMP_TABLE_COPY (_bfd_generic),
    BFD_JUMP_TABLE_CORE (_bfd_nocore),
    BFD_JUMP_TABLE_ARCHIVE (_bfd_archive_bsd),
    BFD_JUMP_TABLE_SYMBOLS (_bfd_nosymbols),
    BFD_JUMP_TABLE_RELOCS (_bfd_norelocs),
    BFD_JUMP_TABLE_WRITE (_bfd_generic),
    BFD_JUMP_TABLE_LINK (_bfd_nolink),
    BFD_JUMP_TABLE_DYNAMIC (_bfd_nodynamic),
    
    NULL,
    NULL
};