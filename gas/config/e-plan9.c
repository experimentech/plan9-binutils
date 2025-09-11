/* Copyright (C) 2023 Free Software Foundation, Inc.

   This file is part of GAS, the GNU Assembler.

   GAS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   GAS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GAS; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#include "as.h"
#include "emul.h"

static const char *plan9_bfd_name (void);

static const char *
plan9_bfd_name (void)
{
  /* Map assembler target CPU to the corresponding BFD Plan 9 vector name. */
#if defined (TC_AARCH64)
  return "plan9-arm64";
#elif defined (TC_ARM)
  return "plan9-arm";
#elif defined (TC_PPC64)
  return "plan9-power64";
#elif defined (TC_PPC)
  return "plan9-power";
#elif defined (TC_I386)
  return "plan9-386";
#elif defined (TC_X86_64)
  return "plan9-amd64";
#else
  /* Fallback to amd64 Plan 9 format if the target is unknown here. */
  return "plan9-amd64";
#endif
}

#define emul_bfd_name	plan9_bfd_name
#define emul_format	&plan9_format_ops

#define emul_name	"plan9"
#define emul_struct_name plan9
#define emul_default_endian 0
#include "emul-target.h"