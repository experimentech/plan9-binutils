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
#include <stdint.h>

#define PLAN9_EXEC_HDR_SIZE 32
#define PLAN9_HDR_MAGIC 0x00008000u

static void gld${EMULATION_NAME}_before_parse (void);
static void gld${EMULATION_NAME}_before_allocation (void);
static void gld${EMULATION_NAME}_finish (void);

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

/* Append a minimal Plan 9 symbol table so nm has something to show.
   This writes a single global text symbol 'main' at the output's
   start address and updates the header's syms field.  */
static void
gld${EMULATION_NAME}_finish (void)
{
  bfd *obfd = link_info.output_bfd;
  if (!obfd)
    return;

  /* Inspect the existing header to determine fat-header sizing. */
  bfd_byte hdr_buf[PLAN9_EXEC_HDR_SIZE];
  bfd_byte extra_buf[8];
  bfd_size_type header_size = PLAN9_EXEC_HDR_SIZE;
  uint32_t magic = 0;
  bool is64 = false;

  if (bfd_seek (obfd, 0, SEEK_SET) != 0)
    return;
  if (bfd_read (hdr_buf, PLAN9_EXEC_HDR_SIZE, obfd) != PLAN9_EXEC_HDR_SIZE)
    return;
  magic = bfd_getb32 (hdr_buf);
  is64 = (magic & PLAN9_HDR_MAGIC) != 0;
  if (is64)
    {
      if (bfd_read (extra_buf, sizeof (extra_buf), obfd) != (bfd_size_type) sizeof (extra_buf))
        return;
      header_size += sizeof (extra_buf);
    }

  /* Determine sizes and where to place the symbol table: header + text + data. */
  asection *text = bfd_get_section_by_name (obfd, ".text");
  asection *data = bfd_get_section_by_name (obfd, ".data");
  bfd_size_type textsz = text ? bfd_get_section_limit_octets (obfd, text) : 0;
  bfd_size_type datasz = data ? bfd_get_section_limit_octets (obfd, data) : 0;
  file_ptr sympos = (file_ptr) header_size + (file_ptr) textsz + (file_ptr) datasz;

  bfd_vma aval = bfd_get_start_address (obfd);

  /* Build and write a single symbol record: value (8B BE), type (1B), name "main\0". */
  const char *name = "main";
  const size_t nlen = 5; /* m a i n \0 */
  const bfd_size_type symsz = (is64 ? 8 : 4) + 1 + nlen;

  if (bfd_seek (obfd, sympos, SEEK_SET) != 0)
    return;

  if (is64)
    {
      uint32_t hi = (uint32_t) (aval >> 32);
      uint32_t lo = (uint32_t) (aval & 0xffffffffu);
      bfd_byte w[8];
      bfd_putb32 (hi, w);
      bfd_putb32 (lo, w + 4);
      if (bfd_write (w, 8, obfd) != 8)
        return;
    }
  else
    {
      uint32_t lo = (uint32_t) (aval & 0xffffffffu);
      bfd_byte w[4];
      bfd_putb32 (lo, w);
      if (bfd_write (w, 4, obfd) != 4)
        return;
    }

  {
    uint8_t type_plus = (uint8_t) ('T' + 0x80);
    if (bfd_write (&type_plus, 1, obfd) != 1)
      return;
  }

  if (bfd_write (name, nlen, obfd) != (bfd_size_type) nlen)
    return;

  /* Rewrite header with updated syms size. */
  {
    /* Overwrite syms (offset 16..19) and restore file pos to 0 to rewrite. */
    bfd_putb32 ((uint32_t) symsz, hdr_buf + 16);
    if (bfd_seek (obfd, 0, SEEK_SET) != 0)
      return;
    if (bfd_write (hdr_buf, PLAN9_EXEC_HDR_SIZE, obfd) != PLAN9_EXEC_HDR_SIZE)
      return;
  }
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
  before_place_orphans_default,
  after_allocation_default,
  set_output_arch_default,
  ldemul_default_target,
  gld${EMULATION_NAME}_before_allocation,
  gld${EMULATION_NAME}_get_script,
  "${EMULATION_NAME}",
  "${OUTPUT_FORMAT}",
  gld${EMULATION_NAME}_finish,
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