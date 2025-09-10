# This shell script emits a C file. -*- C -*-
# It does some substitutions.
fragment <<EOF
/* Plan 9 emulation for ${EMULATION_NAME}
   Copyright (C) 2024 Free Software Foundation, Inc.
   Written by Generic.

   This file is part of the GNU Binutils.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston,
   MA 02110-1301, USA.  */

/* Plan 9 emulation code.  */

#include "sysdep.h"
#include "bfd.h"
#include "bfdlink.h"
#include "ctf-api.h"
#include "ld.h"
#include "ldmain.h"
#include "ldexp.h"
#include "ldlang.h"
#include "ldfile.h"
#include "ldemul.h"
#include "ldgram.h"
#include "ldlex.h"
#include "ldmisc.h"
#include "ldctor.h"

static void gld${EMULATION_NAME}_before_parse (void);
static void gld${EMULATION_NAME}_before_allocation (void);

static void
gld${EMULATION_NAME}_before_parse (void)
{
  ldfile_set_output_arch ("${ARCH}", bfd_arch_unknown);
  input_flags.dynamic = false;
  config.has_shared = false;
  config.magic_demand_paged = false;
}

static void
gld${EMULATION_NAME}_before_allocation (void)
{
  /* Don't do anything special for Plan 9 */
}

static char *
gld${EMULATION_NAME}_get_script (int *isfile)
{
  *isfile = 0;
  return 
"OUTPUT_FORMAT(\"${OUTPUT_FORMAT}\")\n"
"OUTPUT_ARCH(${ARCH})\n"
"${RELOCATING+ENTRY(${ENTRY-_main})}\n"
"SECTIONS\n"
"{\n"
"${RELOCATING+  . = ${TEXT_START_ADDR};}\n"
"  .text :\n"
"  {\n"
"    *(.text .text.* .stub)\n"
"    *(.init)\n"
"    *(.fini)\n"
"${RELOCATING+    _etext = .;}\n"
"  }${RELOCATING+ = ${NOP-0}}\n"
"${RELOCATING+  . = ALIGN(8);}\n"
"  .data :\n"
"  {\n"
"    *(.data .data.*)\n"
"    *(.rodata .rodata.*)\n"
"${RELOCATING+    _edata = .;}\n"
"  }\n"
"  .bss :\n"
"  {\n"
"${RELOCATING+    _bss_start = .;}\n"
"    *(.bss .bss.*)\n"
"    *(COMMON)\n"
"${RELOCATING+    . = ALIGN(8);}\n"
"${RELOCATING+    _end = .;}\n"
"  }\n"
"}\n";
}

struct ld_emulation_xfer_struct ld_${EMULATION_NAME}_emulation =
{
  gld${EMULATION_NAME}_before_parse,
  syslib_default,
  hll_default,
  after_parse_default,
  NULL, /* before_plugin_all_symbols_read */
  after_open_default,
  after_check_relocs_default,
  NULL, /* before_place_orphans */
  after_allocation_default,
  set_output_arch_default,
  ldemul_default_target,
  gld${EMULATION_NAME}_before_allocation,
  gld${EMULATION_NAME}_get_script,
  "${EMULATION_NAME}",
  "${OUTPUT_FORMAT}",
  finish_default,
  NULL, /* create output section statements */
  NULL, /* open dynamic archive */
  NULL, /* place orphan */
  NULL, /* set symbols */
  NULL, /* parse args */
  NULL, /* add_options */
  NULL, /* handle_option */
  NULL, /* unrecognized file */
  NULL, /* list options */
  NULL, /* recognized file */
  NULL, /* find_potential_libraries */
  NULL, /* new_vers_pattern */
  NULL, /* extra_map_file_text */
  NULL, /* emit_ctf_early */
  NULL, /* acquire_strings_for_ctf */
  NULL, /* new_dynsym_for_ctf */
  NULL  /* extra_map_file_text */
};
EOF