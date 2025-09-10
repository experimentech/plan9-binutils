/* obj-plan9.h, Plan 9 object file format for gas, the assembler.
   Copyright (C) 2023 Free Software Foundation, Inc.

   This file is part of GAS, the GNU Assembler.

   GAS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 3,
   or (at your option) any later version.

   GAS is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
   the GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GAS; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

/* Tag to validate Plan 9 object file format processing */
#define OBJ_PLAN9 1

#include "targ-cpu.h"

/* Define the target format for Plan 9 based on target architecture */
#ifdef TARGET_ARCH
# if defined (TC_AARCH64)
#  define TARGET_FORMAT "plan9-arm64"
# elif defined (TC_I386)
#  define TARGET_FORMAT "plan9-386" 
# elif defined (TC_M68K)
#  define TARGET_FORMAT "plan9-m68k"
# elif defined (TC_ARM)
#  define TARGET_FORMAT "plan9-arm"
# elif defined (TC_PPC)
#  define TARGET_FORMAT "plan9-power"
# elif defined (TC_PPC64)
#  define TARGET_FORMAT "plan9-power64"
# else
#  define TARGET_FORMAT "plan9-amd64"
# endif
#else
# define TARGET_FORMAT "plan9-amd64"
#endif

/* Plan 9 uses unknown flavour in BFD */
#define OUTPUT_FLAVOR bfd_target_unknown_flavour

/* Use generic functions where possible */
extern const pseudo_typeS plan9_pseudo_table[];
extern const struct format_ops plan9_format_ops;

#ifndef obj_pop_insert
#define obj_pop_insert() pop_insert (plan9_pseudo_table)
#endif

/* Symbol table entry data type - use generic BFD asymbol */
typedef asymbol obj_symbol_type;

/* Simple symbol table macros - Plan 9 format doesn't use complex attributes */
#define S_SET_OTHER(S,V)    do { /* Plan 9 doesn't use other field */ } while (0)
#define S_SET_TYPE(S,T)     do { /* Plan 9 doesn't use type field */ } while (0)
#define S_SET_DESC(S,D)     do { /* Plan 9 doesn't use desc field */ } while (0)
#define S_GET_OTHER(S)      (0)
#define S_GET_TYPE(S)       (0)
#define S_GET_DESC(S)       (0)

/* Section references */
extern asection *text_section, *data_section, *bss_section;

/* Minimal object format handling - use generic where possible */
#define obj_frob_symbol(S,PUNT) do { /* No special symbol processing needed */ } while (0)
#define obj_frob_file_before_fix() do { /* No special file processing needed */ } while (0)

#define obj_sec_sym_ok_for_reloc(SEC)  1

#define obj_read_begin_hook()    do { /* Nothing needed */ } while (0)
#define obj_symbol_new_hook(s)   do { /* Nothing needed */ } while (0)

#define EMIT_SECTION_SYMBOLS     0

/* Don't use STABS for Plan 9 */
#undef AOUT_STABS