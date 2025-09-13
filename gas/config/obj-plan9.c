/* obj-plan9.c, Plan 9 object file format for gas, the assembler.
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

#include "as.h"
#include "obj-plan9.h"

/* Pseudo-op table for Plan 9 - use generic table */
const pseudo_typeS plan9_pseudo_table[] =
{
  /* No special pseudo-ops needed for Plan 9 */
  {NULL, NULL, 0}
};

/* Plan 9 object format doesn't need special handling in gas.
   The real Plan 9 format work is done in the BFD backend.
   This object format handler just provides a minimal interface. */

/* Plan 9 format operations table */
const struct format_ops plan9_format_ops =
{
  bfd_target_unknown_flavour,
  0,	/* dfl_leading_underscore.  */
  0,	/* emit_section_symbols.  */
  0,	/* begin.  */
  0,	/* end.  */
  0,	/* app_file.  */
  0,	/* assign_symbol.  */
  0,	/* frob_symbol.  */
  0,	/* frob_file.  */
  0,	/* frob_file_before_adjust.  */
  0,	/* frob_file_before_fix.  */
  0,	/* frob_file_after_relocs.  */
  0,	/* s_get_size.  */
  0,	/* s_set_size.  */
  0,	/* s_get_align.  */
  0,	/* s_set_align.  */
  0,	/* s_get_other.  */
  0,	/* s_set_other.  */
  0,	/* s_get_desc.  */
  0,	/* s_set_desc.  */
  0,	/* s_get_type.  */
  0,	/* s_set_type.  */
  0,	/* copy_symbol_attributes.  */
  0,	/* process_stab.  */
  0,	/* separate_stab_sections.  */
  0,	/* init_stab_section.  */
  0,	/* sec_sym_ok_for_reloc.  */
  0,	/* pop_insert.  */
  0,	/* ecoff_set_ext.  */
  0,	/* read_begin_hook.  */
  0,	/* symbol_new_hook.  */
  0,	/* symbol_clone_hook.  */
  0,	/* adjust_symtab.  */
};
