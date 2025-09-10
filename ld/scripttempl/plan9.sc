# Copyright (C) 2024 Free Software Foundation, Inc.
#
# Copying and distribution of this file, with or without modification,
# are permitted in any medium without royalty provided the copyright
# notice and this notice are preserved.
#
# Plan 9 executable format linker script template

test -z "${ALIGNMENT}" && ALIGNMENT="8"
test -z "${ENTRY}" && ENTRY="_main"

cat <<EOF
/* Plan 9 executable format linker script */

OUTPUT_FORMAT("${OUTPUT_FORMAT}")
OUTPUT_ARCH(${ARCH})

${RELOCATING+${LIB_SEARCH_DIRS}}
${RELOCATING+ENTRY(${ENTRY})}

SECTIONS
{
  ${RELOCATING+. = ${TEXT_START_ADDR};}
  .text :
  {
    *(.text .text.* .stub)
    *(.init)
    *(.fini)
    ${RELOCATING+_etext = .;}
  } ${RELOCATING+= ${NOP-0}}

  ${RELOCATING+. = ALIGN(${ALIGNMENT});}
  .data :
  {
    *(.data .data.*)
    *(.rodata .rodata.*)
    ${RELOCATING+_edata = .;}
  }

  .bss :
  {
    ${RELOCATING+_bss_start = .;}
    *(.bss .bss.*)
    *(COMMON)
    ${RELOCATING+. = ALIGN(${ALIGNMENT});}
    ${RELOCATING+_end = .;}
  }
}
EOF