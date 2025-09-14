/* Plan 9 Object File BFD backend header
 * Copyright (C) 2025 Free Software Foundation, Inc.
 * Based on authoritative 9front source documentation
 *
 * Phase 1 (minimal) interface for integrating raw Plan 9 object streams
 * into a reduced linker: we expose a small set of functions that build
 * synthetic sections and a symbol table. Relocations are not yet
 * synthesized – the backend advertises no relocation support so the
 * linker can at least place sections and collect symbols.
 */

#ifndef _PLAN9OBJ_H
#define _PLAN9OBJ_H

#include "bfd.h"

/* Mirror minimal constants needed from 7.out.h */
#ifndef NSNAME
#define NSNAME 8
#endif

/* Plan 9 address structure for arm64 (.7) - mirrors 9front libmach 7obj.c */
struct plan9_address {
    uint8_t type;        /* D_* address type */
    uint8_t reg;         /* register (ignored for now) */
    uint8_t sym_index;   /* local symbol table index */
    uint8_t name;        /* name/type classification (D_EXTERN, D_STATIC, etc) */
    int64_t offset;      /* 32-bit offset (sign-extended), or 64-bit for D_DCONST */
    asymbol *symbol;     /* Resolved pointer to local symbol if any */
    uint8_t fconst[8];   /* IEEE float constant (when type==D_FCONST) */
    uint8_t sconst[NSNAME]; /* Short string constant (when type==D_SCONST) */
};

/* External declarations */
extern const bfd_target plan9_object_vec;

/* Object-file recognizer used by the plan9-object backend. */
bfd_cleanup plan9_object_p (bfd *abfd);

/* Symbol table hooks (object backend specific). */
long plan9obj_get_symtab_upper_bound (bfd *abfd);
long plan9obj_canonicalize_symtab (bfd *abfd, asymbol **syms);
asymbol *plan9obj_make_empty_symbol (bfd *abfd);
void plan9obj_get_symbol_info (bfd *abfd, asymbol *symbol, symbol_info *ret);
void plan9obj_print_symbol (bfd *abfd, void *filep, asymbol *sym, bfd_print_symbol_type how);

/* Object creation and writing for plan9-object backend. */
bool plan9obj_mkobject (bfd *abfd);
bool plan9obj_write_object_contents (bfd *abfd);

/* Relocation interfaces for the plan9-object backend. */
long plan9obj_get_reloc_upper_bound (bfd *abfd, asection *sec);
long plan9obj_canonicalize_reloc (bfd *abfd, asection *sec, arelent **relpp, asymbol **symbols);
reloc_howto_type *plan9obj_bfd_reloc_type_lookup (bfd *abfd, bfd_reloc_code_real_type code);
reloc_howto_type *plan9obj_bfd_reloc_name_lookup (bfd *abfd, const char *name);


#endif /* _PLAN9OBJ_H */