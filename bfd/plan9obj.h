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

/* Plan 9 address structure - represents parsed operand */
struct plan9_address {
    uint8_t type_flags;      /* T_* flags */
    uint8_t index;           /* Register/index number */
    uint8_t scale;           /* Scale factor */
    uint8_t addr_type;       /* D_* address type */
    uint64_t offset;         /* Offset value (32 or 64-bit) */
    asymbol *symbol;         /* Symbol reference */
    uint8_t fconst[8];       /* IEEE float constant */
    uint8_t sconst[8];       /* String constant */
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


#endif /* _PLAN9OBJ_H */