/* Plan 9 BFD backend - Object format support for GNU binutils
 * Copyright (C) 2025 Free Software Foundation, Inc.
 * 
 * This file is part of BFD, the Binary File Descriptor library.
 * Based on analysis of 9front source code at sys/src/libmach/
 */

#include "sysdep.h"
#include "bfd.h"
#include "libbfd.h"
#include <stdint.h>
#include <stdio.h>
/* Forward decl from plan9obj.c (object-file recognizer) */
static bfd_cleanup plan9_object_p (bfd *);


/* Plan 9 magic numbers - from 9front/sys/include/a.out.h */
#define HDR_MAGIC    0x00008000
#define _MAGIC(f, b) ((f)|((((4*(b))+0)*(b))+7))

#define A_MAGIC      _MAGIC(0, 8)           /* 68020 */
#define I_MAGIC      _MAGIC(0, 11)          /* Intel 386 */
#define K_MAGIC      _MAGIC(0, 13)          /* SPARC */
#define E_MAGIC      _MAGIC(0, 20)          /* ARM */
#define Q_MAGIC      _MAGIC(0, 21)          /* PowerPC */
#define U_MAGIC      _MAGIC(0, 25)          /* SPARC64 */
#define S_MAGIC      _MAGIC(HDR_MAGIC, 26)  /* AMD64 */
#define T_MAGIC      _MAGIC(HDR_MAGIC, 27)  /* PowerPC64 */
#define R_MAGIC      _MAGIC(HDR_MAGIC, 28)  /* ARM64 */

/* Plan 9 executable header - exactly matches 9front struct Exec */
struct plan9_exec_hdr {
    uint32_t magic;     /* Magic number */
    uint32_t text;      /* Size of text segment */
    uint32_t data;      /* Size of initialized data */
    uint32_t bss;       /* Size of uninitialized data */
    uint32_t syms;      /* Size of symbol table */
    uint32_t entry;     /* Entry point */
    uint32_t spsz;      /* Size of pc/sp offset table */
    uint32_t pcsz;      /* Size of pc/line number table */
};

#define PLAN9_EXEC_HDR_SIZE 32

/* Plan 9 symbol entry - from 9front struct Sym */
struct plan9_symbol {
    uint64_t value;     /* Symbol value */
    uint32_t sig;       /* Signature */
    uint8_t type;       /* Symbol type */
    /* Name follows as null-terminated string */
};

/* Architecture descriptions */
struct plan9_arch_info {
    uint32_t magic;
    const char *name;
    enum bfd_architecture arch;
    unsigned long mach;
    unsigned int page_size;
    const char *obj_ext;
};

static const struct plan9_arch_info plan9_archs[] = {
    { R_MAGIC, "arm64", bfd_arch_aarch64, bfd_mach_aarch64, 0x10000, ".7" },
    { S_MAGIC, "amd64", bfd_arch_i386, bfd_mach_x86_64, 0x1000, ".6" },
    { I_MAGIC, "386", bfd_arch_i386, bfd_mach_i386_i386, 0x1000, ".8" },
    { E_MAGIC, "arm", bfd_arch_arm, bfd_mach_arm_unknown, 0x1000, ".5" },
    { T_MAGIC, "power64", bfd_arch_powerpc, bfd_mach_ppc64, 0x10000, ".9" },
    { Q_MAGIC, "power", bfd_arch_powerpc, bfd_mach_ppc, 0x1000, ".q" },
    { U_MAGIC, "sparc64", bfd_arch_sparc, bfd_mach_sparc_v9, 0x2000, ".u" },
    { 0, NULL, bfd_arch_unknown, 0, 0, NULL }
};

/* Forward declarations */
static bool plan9_mkobject (bfd *);
static bool plan9_write_object_contents (bfd *);
static long plan9_get_symtab_upper_bound (bfd *);
static long plan9_canonicalize_symtab (bfd *abfd ATTRIBUTE_UNUSED, asymbol **syms ATTRIBUTE_UNUSED);
static asymbol *plan9_make_empty_symbol (bfd *);
static void plan9_get_symbol_info (bfd *, asymbol *, symbol_info *);
static bool plan9_set_arch_mach (bfd *, enum bfd_architecture, unsigned long);

/* BFD target vector functions */

/* (Removed unused generic plan9_object_p helper) */

/* Target-specific object recognition functions */
static bfd_cleanup plan9_object_p_magic (bfd *abfd, uint32_t expected_magic)
{
    struct plan9_exec_hdr hdr;
    const struct plan9_arch_info *arch_info;
    uint32_t magic;
    
    if (bfd_seek (abfd, 0, SEEK_SET) != 0)
        return NULL;
    if (bfd_read (&hdr, PLAN9_EXEC_HDR_SIZE, abfd) != PLAN9_EXEC_HDR_SIZE)
        return NULL;
    
    /* Plan 9 uses big-endian format for headers */
    magic = bfd_getb32 (&hdr.magic);
    
    /* Check if this matches the expected magic for this target */
    if (magic != expected_magic)
        return NULL;
    
    /* Find matching architecture */
    for (arch_info = plan9_archs; arch_info->name; arch_info++) {
        if (arch_info->magic == magic) {
            /* Set architecture info */
            bfd_set_arch_mach (abfd, arch_info->arch, arch_info->mach);
            
            /* Set up sections */
            abfd->flags = EXEC_P | HAS_SYMS;
            
            /* Create text section */
            if (bfd_getb32 (&hdr.text) > 0) {
                asection *text_sec = bfd_make_section (abfd, ".text");
                if (!text_sec) return false;
                
                text_sec->flags = SEC_ALLOC | SEC_LOAD | SEC_CODE | SEC_HAS_CONTENTS;
                text_sec->size = bfd_getb32 (&hdr.text);
                text_sec->vma = bfd_getb32 (&hdr.entry);
                text_sec->lma = text_sec->vma;
                text_sec->filepos = PLAN9_EXEC_HDR_SIZE;
            }
            
            /* Create data section */  
            if (bfd_getb32 (&hdr.data) > 0) {
                asection *data_sec = bfd_make_section (abfd, ".data");
                if (!data_sec) return false;
                
                data_sec->flags = SEC_ALLOC | SEC_LOAD | SEC_DATA | SEC_HAS_CONTENTS;
                data_sec->size = bfd_getb32 (&hdr.data);
                data_sec->vma = bfd_getb32 (&hdr.entry) + bfd_getb32 (&hdr.text);
                data_sec->lma = data_sec->vma;
                data_sec->filepos = PLAN9_EXEC_HDR_SIZE + bfd_getb32 (&hdr.text);
            }
            
            /* Create BSS section */
            if (bfd_getb32 (&hdr.bss) > 0) {
                asection *bss_sec = bfd_make_section (abfd, ".bss");
                if (!bss_sec) return false;
                
                bss_sec->flags = SEC_ALLOC;
                bss_sec->size = bfd_getb32 (&hdr.bss);
                bss_sec->vma = bfd_getb32 (&hdr.entry) + bfd_getb32 (&hdr.text) + bfd_getb32 (&hdr.data);
                bss_sec->lma = bss_sec->vma;
            }
            
            /* Store header info for later use */
            abfd->tdata.any = bfd_alloc (abfd, sizeof (struct plan9_exec_hdr));
            if (!abfd->tdata.any) return NULL;
            memcpy (abfd->tdata.any, &hdr, sizeof (struct plan9_exec_hdr));
            
            /* Set the start address */
            abfd->start_address = bfd_getb32 (&hdr.entry);
            
            /* Set format and report success */
            bfd_set_format (abfd, bfd_object);
            return _bfd_no_cleanup;
        }
    }
    
    return NULL;
}

/* Define target-specific object_p functions */
static bfd_cleanup plan9_amd64_object_p (bfd *abfd) { return plan9_object_p_magic (abfd, S_MAGIC); }
static bfd_cleanup plan9_386_object_p (bfd *abfd) { return plan9_object_p_magic (abfd, I_MAGIC); }
static bfd_cleanup plan9_arm_object_p (bfd *abfd) { return plan9_object_p_magic (abfd, E_MAGIC); }
static bfd_cleanup plan9_arm64_object_p (bfd *abfd) { return plan9_object_p_magic (abfd, R_MAGIC); }
static bfd_cleanup plan9_power_object_p (bfd *abfd) { return plan9_object_p_magic (abfd, Q_MAGIC); }
static bfd_cleanup plan9_power64_object_p (bfd *abfd) { return plan9_object_p_magic (abfd, T_MAGIC); }

/* Create a new Plan 9 BFD object */
static bool
plan9_mkobject (bfd *abfd)
{
    abfd->tdata.any = bfd_zalloc (abfd, sizeof (struct plan9_exec_hdr));
    return abfd->tdata.any != NULL;
}

/* Write Plan 9 object file */
static bool
plan9_write_object_contents (bfd *abfd)
{
    struct plan9_exec_hdr *hdr = (struct plan9_exec_hdr *) abfd->tdata.any;
    struct plan9_exec_hdr disk_hdr;
    asection *text_sec, *data_sec, *bss_sec;
    
    if (!hdr)
        return false;
    
    /* Find sections */
    text_sec = bfd_get_section_by_name (abfd, ".text");
    data_sec = bfd_get_section_by_name (abfd, ".data");  
    bss_sec = bfd_get_section_by_name (abfd, ".bss");
    
    /* Populate header */
    memset (&disk_hdr, 0, sizeof (disk_hdr));
    bfd_putb32 (hdr->magic, &disk_hdr.magic);
    bfd_putb32 (text_sec ? text_sec->size : 0, &disk_hdr.text);
    bfd_putb32 (data_sec ? data_sec->size : 0, &disk_hdr.data);
    bfd_putb32 (bss_sec ? bss_sec->size : 0, &disk_hdr.bss);
    bfd_putb32 (abfd->start_address, &disk_hdr.entry);
    /* TODO: Calculate syms, spsz, pcsz */
    
    /* Write header */
    if (bfd_seek (abfd, 0, SEEK_SET) != 0)
        return false;
    if (bfd_write (&disk_hdr, PLAN9_EXEC_HDR_SIZE, abfd) != PLAN9_EXEC_HDR_SIZE)
        return false;
        
    /* Write sections */
    /* Write text section */
    if (text_sec && text_sec->size > 0) {
        if (bfd_seek (abfd, PLAN9_EXEC_HDR_SIZE, SEEK_SET) != 0)
            return false;
        
        if (text_sec->contents) {
            if (bfd_write (text_sec->contents, text_sec->size, abfd) != text_sec->size)
                return false;
        } else {
            /* Write zeros if no content provided */
            unsigned char *zeros = bfd_zalloc (abfd, text_sec->size);
            if (!zeros) return false;
            if (bfd_write (zeros, text_sec->size, abfd) != text_sec->size)
                return false;
        }
    }
    
    /* Write data section */
    if (data_sec && data_sec->size > 0) {
        if (bfd_seek (abfd, PLAN9_EXEC_HDR_SIZE + (text_sec ? text_sec->size : 0), SEEK_SET) != 0)
            return false;
        
        if (data_sec->contents) {
            if (bfd_write (data_sec->contents, data_sec->size, abfd) != data_sec->size)
                return false;
        } else {
            /* Write zeros if no content provided */
            unsigned char *zeros = bfd_zalloc (abfd, data_sec->size);
            if (!zeros) return false;
            if (bfd_write (zeros, data_sec->size, abfd) != data_sec->size)
                return false;
        }
    }
    
    /* Write data section */
    if (data_sec && data_sec->size > 0) {
        file_ptr data_offset = PLAN9_EXEC_HDR_SIZE + (text_sec ? text_sec->size : 0);
        if (bfd_seek (abfd, data_offset, SEEK_SET) != 0)
            return false;
        if (data_sec->contents) {
            if (bfd_write (data_sec->contents, data_sec->size, abfd) != data_sec->size)
                return false;
        }
    }
    
    /* BSS section is not written to file (it's zero-initialized at runtime) */
    
    return true;
}

/* Symbol table functions */
static long
plan9_get_symtab_upper_bound (bfd *abfd)
{
    struct plan9_exec_hdr *hdr = (struct plan9_exec_hdr *) abfd->tdata.any;
    if (!hdr) return -1;
    
    /* Rough estimate - Plan 9 symbols are variable length */
    return (bfd_getb32 (&hdr->syms) / 8 + 1) * sizeof (asymbol *);
}

static long  
plan9_canonicalize_symtab (bfd *abfd ATTRIBUTE_UNUSED, asymbol **syms ATTRIBUTE_UNUSED)
{
    /* TODO: Parse Plan 9 symbol table format */
    /* This is complex - symbols are variable length records */
    return 0;
}

static asymbol *
plan9_make_empty_symbol (bfd *abfd)
{
    asymbol *symbol = bfd_zalloc (abfd, sizeof (asymbol));
    if (symbol)
        symbol->the_bfd = abfd;
    return symbol;
}

static void
plan9_get_symbol_info (bfd *abfd ATTRIBUTE_UNUSED, asymbol *symbol, symbol_info *ret)
{
    bfd_symbol_info (symbol, ret);
}

/* Section contents writing - called by objcopy */
static bool
plan9_set_section_contents (bfd *abfd, sec_ptr section, const void *data,
                           file_ptr offset, bfd_size_type count)
{
    /* Allocate section contents buffer if needed */
    if (!section->contents) {
        section->contents = bfd_zalloc (abfd, section->size);
        if (!section->contents)
            return false;
    }
    
    /* Bounds check */
    if (offset + count > section->size)
        return false;
    
    /* Copy the data into the section */
    memcpy ((char *)section->contents + offset, data, count);
    section->flags |= SEC_IN_MEMORY;
    
    return true;
}
static bool
plan9_get_section_contents (bfd *abfd, sec_ptr section, void *location, 
                           file_ptr offset, bfd_size_type count)
{
    struct plan9_exec_hdr *hdr;
    file_ptr file_offset;
    file_ptr current_pos;
    
    if (count == 0)
        return true;
    
    /* For sections with SEC_IN_MEMORY flag, contents are already loaded */
    if (section->flags & SEC_IN_MEMORY)
        return _bfd_generic_get_section_contents (abfd, section, location, offset, count);
    
    hdr = (struct plan9_exec_hdr *) abfd->tdata.any;
    if (!hdr)
        return false;
    
    /* Save current file position */
    current_pos = bfd_tell (abfd);
    
    /* Calculate file position for this section */
    if (strcmp (section->name, ".text") == 0) {
        file_offset = PLAN9_EXEC_HDR_SIZE + offset;
    } else if (strcmp (section->name, ".data") == 0) {
        file_offset = PLAN9_EXEC_HDR_SIZE + bfd_getb32 (&hdr->text) + offset;
    } else {
        /* BSS section has no file representation */
        if (strcmp (section->name, ".bss") == 0) {
            memset (location, 0, count);
            return true;
        }
        return false;
    }
    
    /* Seek to the correct position */
    if (bfd_seek (abfd, file_offset, SEEK_SET) != 0)
        return false;
    
    /* Read the data */
    bfd_size_type bytes_read = bfd_read (location, count, abfd);
    
    /* Restore file position */
    {
        int seek_rv = bfd_seek (abfd, current_pos, SEEK_SET);
        (void) seek_rv;
    }
    
    return bytes_read == count;
}

static bool
plan9_set_arch_mach (bfd *abfd, enum bfd_architecture arch, unsigned long mach)
{
    const struct plan9_arch_info *arch_info;
    struct plan9_exec_hdr *hdr;
    
    /* Find matching Plan 9 architecture */
    for (arch_info = plan9_archs; arch_info->name; arch_info++) {
        if (arch_info->arch == arch && arch_info->mach == mach) {
            if (!abfd->tdata.any) {
                abfd->tdata.any = bfd_zalloc (abfd, sizeof (struct plan9_exec_hdr));
                if (!abfd->tdata.any) return false;
            }
            
            hdr = (struct plan9_exec_hdr *) abfd->tdata.any;
            hdr->magic = arch_info->magic;
            
            return bfd_default_set_arch_mach (abfd, arch, mach);
        }
    }
    
    return false;
}

/* Macro to define Plan 9 targets for different architectures */
#define PLAN9_TARGET(arch_name, magic_val, arch_enum, mach_val) \
const bfd_target plan9_##arch_name##_vec = { \
  "plan9-" #arch_name,			/* Name.  */ \
  bfd_target_unknown_flavour, \
  BFD_ENDIAN_BIG, \
  BFD_ENDIAN_BIG, \
  (HAS_SYMS | WP_TEXT),			/* Object flags.  */ \
  (SEC_CODE | SEC_DATA | SEC_HAS_CONTENTS), /* Section flags.  */ \
  0,					/* Leading underscore.  */ \
  ' ',					/* AR_pad_char.  */ \
  255,					/* AR_max_namelen.  */ \
  0,					/* Match priority.  */ \
  TARGET_KEEP_UNUSED_SECTION_SYMBOLS, /* keep unused section symbols.  */ \
  bfd_getb64, bfd_getb_signed_64, bfd_putb64, \
  bfd_getb32, bfd_getb_signed_32, bfd_putb32, \
  bfd_getb16, bfd_getb_signed_16, bfd_putb16, \
  bfd_getb64, bfd_getb_signed_64, bfd_putb64, \
  bfd_getb32, bfd_getb_signed_32, bfd_putb32, \
  bfd_getb16, bfd_getb_signed_16, bfd_putb16, \
  { \
    _bfd_dummy_target, \
    plan9_##arch_name##_object_p,		/* bfd_check_format.  */ \
    _bfd_dummy_target, \
    _bfd_dummy_target, \
  }, \
  { \
    _bfd_bool_bfd_false_error, \
    plan9_mkobject, \
    _bfd_generic_mkarchive, \
    _bfd_bool_bfd_false_error, \
  }, \
  {					/* bfd_write_contents.  */ \
    _bfd_bool_bfd_false_error, \
    plan9_write_object_contents, \
    _bfd_write_archive_contents, \
    _bfd_bool_bfd_false_error, \
  }, \
  BFD_JUMP_TABLE_GENERIC (plan9), \
  BFD_JUMP_TABLE_COPY (_bfd_generic), \
  BFD_JUMP_TABLE_CORE (_bfd_nocore), \
  BFD_JUMP_TABLE_ARCHIVE (_bfd_noarchive), \
  BFD_JUMP_TABLE_SYMBOLS (plan9), \
  BFD_JUMP_TABLE_RELOCS (_bfd_norelocs), \
  BFD_JUMP_TABLE_WRITE (plan9), \
  BFD_JUMP_TABLE_LINK (_bfd_nolink), \
  BFD_JUMP_TABLE_DYNAMIC (_bfd_nodynamic), \
  NULL, \
  NULL \
}

/* Print a Plan 9 symbol */
static void
plan9_print_symbol (bfd *abfd,
                    void *filep,
                    asymbol *symbol,
                    bfd_print_symbol_type how)
{
  FILE *file = (FILE *) filep;

  switch (how)
    {
    case bfd_print_symbol_name:
      fprintf (file, "%s", symbol->name);
      break;
    default:
      bfd_print_symbol_vandf (abfd, filep, symbol);
      break;
    }
}

/* Symbol table jump table */
#define plan9_get_symtab_upper_bound plan9_get_symtab_upper_bound
#define plan9_canonicalize_symtab plan9_canonicalize_symtab  
#define plan9_make_empty_symbol plan9_make_empty_symbol
#define plan9_print_symbol plan9_print_symbol
#define plan9_get_symbol_info plan9_get_symbol_info
#define plan9_get_symbol_version_string _bfd_nosymbols_get_symbol_version_string
#define plan9_bfd_is_local_label_name bfd_generic_is_local_label_name
#define plan9_bfd_is_target_special_symbol _bfd_bool_bfd_asymbol_false
#define plan9_get_lineno _bfd_nosymbols_get_lineno
#define plan9_find_nearest_line _bfd_nosymbols_find_nearest_line
#define plan9_find_nearest_line_with_alt _bfd_nosymbols_find_nearest_line_with_alt
#define plan9_find_line _bfd_nosymbols_find_line  
#define plan9_find_inliner_info _bfd_nosymbols_find_inliner_info
#define plan9_bfd_make_debug_symbol _bfd_nosymbols_bfd_make_debug_symbol
#define plan9_read_minisymbols _bfd_generic_read_minisymbols
#define plan9_minisymbol_to_symbol _bfd_generic_minisymbol_to_symbol

/* Write table jump table */
#define plan9_set_arch_mach plan9_set_arch_mach
#define plan9_set_section_contents plan9_set_section_contents

/* Generic table jump table */  
#define plan9_close_and_cleanup _bfd_generic_close_and_cleanup
#define plan9_bfd_free_cached_info _bfd_generic_bfd_free_cached_info
#define plan9_new_section_hook _bfd_generic_new_section_hook
#define plan9_get_section_contents plan9_get_section_contents
#define plan9_get_section_contents_in_window _bfd_generic_get_section_contents_in_window

/* Define targets for other architectures */
PLAN9_TARGET(amd64, S_MAGIC, bfd_arch_i386, bfd_mach_x86_64);
PLAN9_TARGET(386, I_MAGIC, bfd_arch_i386, bfd_mach_i386_i386);
PLAN9_TARGET(arm, E_MAGIC, bfd_arch_arm, bfd_mach_arm_unknown);
PLAN9_TARGET(arm64, R_MAGIC, bfd_arch_aarch64, bfd_mach_aarch64);
PLAN9_TARGET(power, Q_MAGIC, bfd_arch_powerpc, bfd_mach_ppc);
PLAN9_TARGET(power64, T_MAGIC, bfd_arch_powerpc, bfd_mach_ppc64);

/* Define the plan9-object backend which recognizes raw Plan 9 object streams. */
const bfd_target plan9_object_vec = {
    "plan9-object",                 /* Name */
    bfd_target_unknown_flavour,
    BFD_ENDIAN_LITTLE,
    BFD_ENDIAN_LITTLE,
    (HAS_RELOC | HAS_SYMS | HAS_LOCALS),
    (SEC_CODE | SEC_DATA | SEC_HAS_CONTENTS | SEC_ALLOC | SEC_LOAD | SEC_RELOC),
    0,
    ' ',
    16,
    0,
    TARGET_KEEP_UNUSED_SECTION_SYMBOLS,
    bfd_getl64, bfd_getl_signed_64, bfd_putl64,
    bfd_getl32, bfd_getl_signed_32, bfd_putl32,
    bfd_getl16, bfd_getl_signed_16, bfd_putl16,
    bfd_getl64, bfd_getl_signed_64, bfd_putl64,
    bfd_getl32, bfd_getl_signed_32, bfd_putl32,
    bfd_getl16, bfd_getl_signed_16, bfd_putl16,
    { plan9_object_p, bfd_generic_archive_p, _bfd_dummy_target, _bfd_dummy_target },
    { _bfd_bool_bfd_false_error, _bfd_generic_mkarchive, _bfd_bool_bfd_false_error, _bfd_bool_bfd_false_error },
    { _bfd_bool_bfd_false_error, _bfd_write_archive_contents, _bfd_bool_bfd_false_error, _bfd_bool_bfd_false_error },
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