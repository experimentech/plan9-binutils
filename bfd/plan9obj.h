/* Plan 9 Object File BFD backend header
 * Copyright (C) 2025 Free Software Foundation, Inc.
 * Based on authoritative 9front source documentation
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

#endif /* _PLAN9OBJ_H */