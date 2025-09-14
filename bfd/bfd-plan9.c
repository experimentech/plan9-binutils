/* Plan 9 BFD backend - Object format support for GNU binutils
 * Copyright (C) 2025 Free Software Foundation, Inc.
 * 
 * This file is part of BFD, the Binary File Descriptor library.
 * Based on analysis of 9front source code at sys/src/libmach/
 */

#include "sysdep.h"
#include "bfd.h"
#include "libbfd.h"
#include "bfdlink.h"
#include <stdint.h>
#include <stdio.h>
#if defined(HAVE_CONFIG_H)
#include "config.h"
#endif
/* For minimal builds (e.g. stripped Plan 9 only linker) we don't require the
   separate plan9obj.c object stream recognizer. Instead we provide a very
   small fallback object recognizer here. If plan9obj.c is present it becomes
   unused but harmless. Optionally enable plan9obj integration by defining
   ENABLE_PLAN9OBJ at build time and providing plan9obj.c. */
/* Always include plan9obj.h for prototypes when wiring relocation jump table
    and to use its object recognizer. */
#include "plan9obj.h"


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
static void plan9_print_symbol (bfd *, void *, asymbol *, bfd_print_symbol_type);
static bool plan9_set_arch_mach (bfd *, enum bfd_architecture, unsigned long);

/* BFD target vector functions */

/* (Removed unused generic plan9_object_p helper)
     Provide a local minimalist object recognizer for the generic Plan 9
     object stream target vector.  This only validates the two-byte opcode
     prefix pattern used by 9front compilers (borrowed from earlier
     plan9obj.c logic) and marks the bfd as an object file. */
static bfd_cleanup
plan9_fallback_object_p (bfd *abfd)
{
    unsigned char buf[5];
    if (bfd_seek (abfd, 0, SEEK_SET) != 0)
        return NULL;
    if (bfd_read (buf, 5, abfd) != 5)
        return NULL;
    /* Check for the two magic byte arrangements identifying a Plan 9 object. */
    if ((buf[2] == 1 && buf[3] == '<') || (buf[3] == 1 && buf[4] == '<'))
    {
        bfd_set_arch_mach (abfd, bfd_arch_unknown, 0);
        return _bfd_no_cleanup;
    }
    return NULL;
}

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
            
            /* Report success; format is already set by framework. */
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
    /* Symbol table emission */
    asymbol **outs = bfd_get_outsymbols (abfd);
    unsigned int outcount = abfd->symcount;
    bool have_outsyms = (outs != NULL && outcount > 0);
    /* Track presence of common linker-defined boundary symbols. */
    bool outs_has_etext = false, outs_has_edata = false, outs_has_end = false;
    bfd_size_type symsize = 0;
    bool is64 = false;
    unsigned int i;
    file_ptr sym_filepos;
    
    if (!hdr)
        return false;
    
    /* Find sections */
    text_sec = bfd_get_section_by_name (abfd, ".text");
    data_sec = bfd_get_section_by_name (abfd, ".data");  
    bss_sec = bfd_get_section_by_name (abfd, ".bss");
    
        /* Determine if we should emit 64-bit symbol values based on magic. */
        {
                uint32_t magic = hdr->magic;
                if (magic == R_MAGIC || magic == S_MAGIC || magic == T_MAGIC || magic == U_MAGIC)
                        is64 = true;
        }

                 /* Compute symbol table size from outsymbols, if any.  We only
                        emit a subset: defined symbols in .text/.data/.bss. */
                         if (have_outsyms)
                     {
                         for (i = 0; i < outcount; i++)
                             {
                                 asymbol *s = outs[i];
                                 if (!s || !s->name || !s->section)
                                     continue;
                                         /* Mark boundary names if present. */
                                         if (strcmp (s->name, "_etext") == 0) outs_has_etext = true;
                                         else if (strcmp (s->name, "_edata") == 0) outs_has_edata = true;
                                         else if (strcmp (s->name, "_end") == 0) outs_has_end = true;

                                         /* Skip undefined/debug/common. Allow ABS only for special names
                                                (_etext/_edata/_end) which we map below. */
                                         if (s->section == bfd_und_section_ptr
                                                 || s->section == bfd_com_section_ptr)
                                             continue;

                                 /* Map to a Plan 9 type letter; skip if not one we handle. */
                                 char t = 0;
                                 flagword secf = s->section->flags;
                                         if (s->section == bfd_abs_section_ptr)
                                             {
                                                 /* Map ABS boundary names to their types. */
                                                 if (strcmp (s->name, "_etext") == 0)
                                                     t = 'T';
                                                 else if (strcmp (s->name, "_edata") == 0)
                                                     t = 'D';
                                                 else if (strcmp (s->name, "_end") == 0)
                                                     t = 'B';
                                                 else
                                                     t = 0;
                                             }
                                         else if (secf & SEC_CODE)
                                     t = (s->flags & BSF_GLOBAL) ? 'T' : 'L';
                                 else if ((secf & SEC_DATA) || (secf & SEC_HAS_CONTENTS))
                                     t = 'D';
                                 else if ((secf & SEC_ALLOC) && !(secf & SEC_HAS_CONTENTS))
                                     t = 'B';
                                 if (t == 0)
                                     continue;

                                 /* value + type byte + NUL-terminated name */
                                 symsize += (is64 ? 8 : 4); /* value */
                                 symsize += 1;              /* type */
                                 symsize += (bfd_size_type) (strlen (s->name) + 1);
                             }
                     }
                         /* Add synthetic boundary symbols if not already present. */
                         {
                             bfd_vma text_vma = text_sec ? text_sec->vma : 0;
                             bfd_size_type textsz = text_sec ? text_sec->size : 0;
                             bfd_vma data_vma = data_sec ? data_sec->vma : (text_vma + textsz);
                             bfd_size_type datasz = data_sec ? data_sec->size : 0;
                             bfd_size_type bsssz  = bss_sec ? bss_sec->size : 0;

                             if (!outs_has_etext && text_sec)
                                 symsize += (is64 ? 8 : 4) + 1 + (bfd_size_type) (strlen ("_etext") + 1);
                             if (!outs_has_edata)
                                 symsize += (is64 ? 8 : 4) + 1 + (bfd_size_type) (strlen ("_edata") + 1);
                             if (!outs_has_end)
                                 symsize += (is64 ? 8 : 4) + 1 + (bfd_size_type) (strlen ("_end") + 1);
                             (void) data_vma; (void) datasz; (void) bsssz; /* values computed later */
                         }
                 /* If the linker didn't populate outsymbols (or none qualified),
                        synthesize a minimal table with just a global text symbol
                        "main" at the entry point. */
                 if (symsize == 0 && abfd->start_address != 0)
                     {
                         const char *fallback = "main";
                         symsize = (is64 ? 8 : 4) + 1 + (bfd_size_type) (strlen (fallback) + 1);
                     }

        /* Populate header */
    memset (&disk_hdr, 0, sizeof (disk_hdr));
    bfd_putb32 (hdr->magic, &disk_hdr.magic);
    bfd_putb32 (text_sec ? text_sec->size : 0, &disk_hdr.text);
    bfd_putb32 (data_sec ? data_sec->size : 0, &disk_hdr.data);
    bfd_putb32 (bss_sec ? bss_sec->size : 0, &disk_hdr.bss);
        bfd_putb32 (symsize, &disk_hdr.syms);
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
    
        /* Now write the symbol table (if any), immediately following data. */
        sym_filepos = PLAN9_EXEC_HDR_SIZE
                                + (file_ptr) (text_sec ? text_sec->size : 0)
                                + (file_ptr) (data_sec ? data_sec->size : 0);

                        if (symsize > 0)
                        {
                                if (bfd_seek (abfd, sym_filepos, SEEK_SET) != 0)
                                        return false;

                                                if (have_outsyms)
                                    {
                                        for (i = 0; i < outcount; i++)
                                            {
                                                asymbol *s = outs[i];
                                                if (!s || !s->name || !s->section)
                                                    continue;
                                                                if (s->section == bfd_und_section_ptr
                                                                        || s->section == bfd_com_section_ptr)
                                                    continue;

                                                char t = 0;
                                                flagword secf = s->section->flags;
                                                                if (s->section == bfd_abs_section_ptr)
                                                                    {
                                                                        if (strcmp (s->name, "_etext") == 0)
                                                                            t = 'T';
                                                                        else if (strcmp (s->name, "_edata") == 0)
                                                                            t = 'D';
                                                                        else if (strcmp (s->name, "_end") == 0)
                                                                            t = 'B';
                                                                        else
                                                                            t = 0;
                                                                    }
                                                                else if (secf & SEC_CODE)
                                                    t = (s->flags & BSF_GLOBAL) ? 'T' : 'L';
                                                else if ((secf & SEC_DATA) || (secf & SEC_HAS_CONTENTS))
                                                    t = 'D';
                                                else if ((secf & SEC_ALLOC) && !(secf & SEC_HAS_CONTENTS))
                                                    t = 'B';
                                                if (t == 0)
                                                    continue;

                                                /* Compute absolute value: section VMA + section-relative value. */
                                                                bfd_vma aval = bfd_asymbol_value (s);
                                                if (is64)
                                                    {
                                                        uint32_t hi = (uint32_t) (aval >> 32);
                                                        uint32_t lo = (uint32_t) (aval & 0xffffffffu);
                                                        bfd_byte w[8];
                                                        bfd_putb32 (hi, w);
                                                        bfd_putb32 (lo, w + 4);
                                                        if (bfd_write (w, 8, abfd) != 8)
                                                            return false;
                                                    }
                                                else
                                                    {
                                                        uint32_t lo = (uint32_t) (aval & 0xffffffffu);
                                                        bfd_byte w[4];
                                                        bfd_putb32 (lo, w);
                                                        if (bfd_write (w, 4, abfd) != 4)
                                                            return false;
                                                    }

                                                /* Type byte is ASCII letter + 0x80. */
                                                {
                                                    uint8_t type_plus = (uint8_t) (t + 0x80);
                                                    if (bfd_write (&type_plus, 1, abfd) != 1)
                                                        return false;
                                                }

                                                /* Name including trailing NUL. */
                                                {
                                                    size_t nlen = strlen (s->name) + 1;
                                                    if (bfd_write (s->name, nlen, abfd) != (bfd_size_type) nlen)
                                                        return false;
                                                }
                                            }
                                                        }
                                else
                                    {
                                        /* Fallback: single T main at start address. */
                                        const char *name = "main";
                                        bfd_vma aval = abfd->start_address;
                                        if (is64)
                                            {
                                                uint32_t hi = (uint32_t) (aval >> 32);
                                                uint32_t lo = (uint32_t) (aval & 0xffffffffu);
                                                bfd_byte w[8];
                                                bfd_putb32 (hi, w);
                                                bfd_putb32 (lo, w + 4);
                                                if (bfd_write (w, 8, abfd) != 8)
                                                    return false;
                                            }
                                        else
                                            {
                                                uint32_t lo = (uint32_t) (aval & 0xffffffffu);
                                                bfd_byte w[4];
                                                bfd_putb32 (lo, w);
                                                if (bfd_write (w, 4, abfd) != 4)
                                                    return false;
                                            }
                                        uint8_t type_plus = (uint8_t) ('T' + 0x80);
                                        if (bfd_write (&type_plus, 1, abfd) != 1)
                                            return false;
                                        {
                                            size_t nlen = strlen (name) + 1;
                                            if (bfd_write (name, nlen, abfd) != (bfd_size_type) nlen)
                                                return false;
                                        }
                                    }
                        }

        /* Note: BSS section is not written (zero-initialized at runtime). */
    
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
plan9_canonicalize_symtab (bfd *abfd, asymbol **syms)
{
    /* Parse Plan 9 executable symbol table as written by putsymb():
       - syms area begins after header + text + data (no padding assumed here)
       - per entry:
         [optional] high 32-bit word (when 64-bit writers emit llput)
         low 32-bit word (value)
         1 byte: type = t + 0x80 (t is ASCII letter: 'T','D','B','L','f','z','Z', ...)
         name: NUL-terminated string, except for 'z'/'Z' which use a two-byte-pair
               encoding terminated by two consecutive zero bytes.
       - repeat until syms bytes consumed. */
    struct plan9_exec_hdr *hdr = (struct plan9_exec_hdr *) abfd->tdata.any;
    asection *text_sec = bfd_get_section_by_name (abfd, ".text");
    asection *data_sec = bfd_get_section_by_name (abfd, ".data");
    asection *bss_sec = bfd_get_section_by_name (abfd, ".bss");
    file_ptr off;
    bfd_size_type symsz;
    bfd_size_type read_left;
    bfd_size_type n = 0;
    file_ptr save_pos;
    uint32_t magic = 0;
    bool is64 = false;
    /* file alignment not applied to file offsets here */

    if (!hdr)
        return 0;

    symsz = bfd_getb32 (&hdr->syms);
    if (symsz == 0)
        return 0;

    /* Compute file offset of symbol table: header + text + data.
       Note: for these 9front samples the writers do not pad file offsets
       to INITRND between text and data. */
    magic = bfd_getb32 (&hdr->magic);
    off = PLAN9_EXEC_HDR_SIZE + (file_ptr) bfd_getb32 (&hdr->text)
        + (file_ptr) bfd_getb32 (&hdr->data);

    /* Decide whether values are 64-bit (writers emit an extra high word).
       Use header magic to detect 64-bit arches. */
    if (magic == R_MAGIC || magic == S_MAGIC || magic == T_MAGIC || magic == U_MAGIC)
        is64 = true;

    save_pos = bfd_tell (abfd);
    if (bfd_seek (abfd, off, SEEK_SET) != 0)
        return 0;

    read_left = symsz;
    while (read_left > 0) {
        /* value: 4 or 8 bytes (two 4B words) */
        uint32_t hi = 0, lo = 0;
        bfd_vma val = 0;
        uint8_t type_plus;
        char namebuf[1024];

        if (is64) {
            if (read_left < 8) break;
            if (bfd_read (&hi, 4, abfd) != 4) break;
            if (bfd_read (&lo, 4, abfd) != 4) break;
            read_left -= 8;
            val = (((bfd_vma) bfd_getb32 (&hi)) << 32) | ((bfd_vma) bfd_getb32 (&lo));
        } else {
            if (read_left < 4) break;
            if (bfd_read (&lo, 4, abfd) != 4) break;
            read_left -= 4;
            val = (bfd_vma) bfd_getb32 (&lo);
        }


        /* type byte */
        if (read_left < 1) break;
        if (bfd_read (&type_plus, 1, abfd) != 1) break;
        read_left -= 1;
        char t = (char)(type_plus - 0x80);

        /* name string: normal NUL-terminated, except z/Z which are special. */
        bfd_size_type nb = 0;
        if (t == 'z' || t == 'Z') {
            /* Two-byte pair encoding terminated by two zero bytes. We don't
               reconstruct the path here; skip bytes until 0x00 0x00. */
            int prev_zero = 0;
            while (read_left > 0) {
                unsigned char b;
                if (bfd_read (&b, 1, abfd) != 1) { read_left = 0; break; }
                read_left -= 1;
                if (b == 0) {
                    if (prev_zero) break; /* end */
                    prev_zero = 1;
                } else {
                    prev_zero = 0;
                }
                        }
            /* Skip history/file entries; they are not real symbols for nm. */
            continue;
        } else {
            /* Regular NUL-terminated name */
            while (nb + 1 < (bfd_size_type) sizeof (namebuf)) {
                unsigned char b;
                if (read_left == 0) break;
                if (bfd_read (&b, 1, abfd) != 1) { read_left = 0; break; }
                read_left -= 1;
                namebuf[nb++] = (char) b;
                if (b == '\0') break;
            }
            if (nb == 0) break; /* malformed */
        }

        /* Skip non-symbol informational records ('f' file, 'm' frame, 'a' auto, 'p' param). */
        if (t == 'f' || t == 'm' || t == 'a' || t == 'p') {
            continue;
        }

        /* Create symbol for program-relevant entries only */
        if (!(t == 'T' || t == 'L' || t == 'D' || t == 'B'))
            continue;
        {
            asymbol *sym = bfd_zalloc (abfd, sizeof (asymbol));
            if (!sym) break;
            sym->the_bfd = abfd;
            /* Ensure name is set (even if empty) */
            if (nb > 0) {
                sym->name = bfd_alloc (abfd, nb);
                if (!sym->name) break;
                memcpy ((char*)sym->name, namebuf, nb);
            } else {
                sym->name = "";
            }

            /* Classify by type letter when possible; fallback to ranges. */
            switch (t) {
                case 'T': /* text symbol */
                case 'L': /* local text */
                    sym->section = text_sec ? text_sec : bfd_abs_section_ptr;
                    sym->flags = BSF_FUNCTION | (t == 'L' ? 0 : BSF_GLOBAL);
                    if (text_sec)
                        sym->value = val - text_sec->vma;
                    else
                        sym->value = val;
                    break;
                case 'D':
                    sym->section = data_sec ? data_sec : bfd_abs_section_ptr;
                    sym->flags = BSF_OBJECT | BSF_GLOBAL;
                    if (data_sec)
                        sym->value = val - data_sec->vma;
                    else
                        sym->value = val;
                    break;
                case 'B':
                    sym->section = bss_sec ? bss_sec : bfd_abs_section_ptr;
                    sym->flags = BSF_OBJECT | BSF_GLOBAL;
                    if (bss_sec)
                        sym->value = val - bss_sec->vma;
                    else
                        sym->value = val;
                    break;
                default:
                    break;
            }
            syms[n++] = sym;
        }
    }

    /* Null-terminate the list as expected. */
    syms[n] = NULL;

    /* Restore file position */
    {
        int _ = bfd_seek (abfd, save_pos, SEEK_SET);
        (void) _;
    }

    return (long) n;
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
        BFD_JUMP_TABLE_LINK (plan9), \
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
            fprintf (file, " %s", symbol->name);
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

/* Linker jump table: map Plan 9 backends to generic, non-erroring hooks. */
/* These are used by both the Plan 9 executable targets (PLAN9_TARGET) and
    our plan9-object backend. Keeping them generic avoids setting
    bfd_error_invalid_operation during early ld setup. */
#define plan9_sizeof_headers                 _bfd_nolink_sizeof_headers
#define plan9_bfd_get_relocated_section_contents bfd_generic_get_relocated_section_contents
#define plan9_bfd_relax_section              bfd_generic_relax_section
#define plan9_bfd_link_hash_table_create     _bfd_generic_link_hash_table_create
#define plan9_bfd_link_add_symbols           _bfd_generic_link_add_symbols
#define plan9_bfd_link_just_syms             _bfd_generic_link_just_syms
#define plan9_bfd_copy_link_hash_symbol_type _bfd_generic_copy_link_hash_symbol_type
/* Custom final link: after generic link writes sections, append a minimal
     Plan 9 symbol table (from outsymbols if available, else a fallback 'main')
     and update the Exec header syms size. */
static bool
plan9_bfd_final_link (bfd *abfd, struct bfd_link_info *info)
{
    bool ok = _bfd_generic_final_link (abfd, info);
    if (!ok)
        return false;

    /* Determine 64-bit mode. Prefer header magic if present, else arch width. */
    bool is64 = false;
    struct plan9_exec_hdr *hdr = (struct plan9_exec_hdr *) abfd->tdata.any;
    if (hdr)
        {
            uint32_t magic = hdr->magic;
            if (magic == R_MAGIC || magic == S_MAGIC || magic == T_MAGIC || magic == U_MAGIC)
                is64 = true;
        }
    else if (abfd->arch_info && abfd->arch_info->bits_per_address == 64)
        is64 = true;

    /* Compute section sizes and position of symbol table: header + text + data. */
    asection *text_sec = bfd_get_section_by_name (abfd, ".text");
    asection *data_sec = bfd_get_section_by_name (abfd, ".data");
    bfd_size_type textsz = text_sec ? bfd_get_section_limit_octets (abfd, text_sec) : 0;
    bfd_size_type datasz = data_sec ? bfd_get_section_limit_octets (abfd, data_sec) : 0;
    file_ptr sym_filepos = PLAN9_EXEC_HDR_SIZE + (file_ptr) textsz + (file_ptr) datasz;

    /* Gather output symbols if the linker populated them. */
    asymbol **outs = bfd_get_outsymbols (abfd);
    unsigned int outcount = abfd->symcount;
    bfd_size_type symsize = 0;
    if (outs && outcount > 0)
        {
            for (unsigned int i = 0; i < outcount; i++)
                {
                    asymbol *s = outs[i];
                    if (!s || !s->name || !s->section)
                        continue;
                    if (s->section == bfd_und_section_ptr
                            || s->section == bfd_abs_section_ptr
                            || s->section == bfd_com_section_ptr)
                        continue;
                    /* Map section to Plan 9 type letter. */
                    char t = 0;
                    flagword secf = s->section->flags;
                    if (secf & SEC_CODE)
                        t = (s->flags & BSF_GLOBAL) ? 'T' : 'L';
                    else if ((secf & SEC_DATA) || (secf & SEC_HAS_CONTENTS))
                        t = 'D';
                    else if ((secf & SEC_ALLOC) && !(secf & SEC_HAS_CONTENTS))
                        t = 'B';
                    if (t == 0)
                        continue;
                    symsize += (is64 ? 8 : 4) + 1 + (bfd_size_type) (strlen (s->name) + 1);
                }
        }
    if (symsize == 0 && abfd->start_address != 0)
        symsize = (is64 ? 8 : 4) + 1 + (bfd_size_type) (strlen ("main") + 1);

    if (symsize == 0)
        return true; /* Nothing to emit. */

    /* Append symbols. */
    if (bfd_seek (abfd, sym_filepos, SEEK_SET) != 0)
        return false;

    if (outs && outcount > 0)
        {
            for (unsigned int i = 0; i < outcount; i++)
                {
                    asymbol *s = outs[i];
                    if (!s || !s->name || !s->section)
                        continue;
                    if (s->section == bfd_und_section_ptr
                            || s->section == bfd_abs_section_ptr
                            || s->section == bfd_com_section_ptr)
                        continue;
                    char t = 0;
                    flagword secf = s->section->flags;
                    if (secf & SEC_CODE)
                        t = (s->flags & BSF_GLOBAL) ? 'T' : 'L';
                    else if ((secf & SEC_DATA) || (secf & SEC_HAS_CONTENTS))
                        t = 'D';
                    else if ((secf & SEC_ALLOC) && !(secf & SEC_HAS_CONTENTS))
                        t = 'B';
                    if (t == 0)
                        continue;
                    bfd_vma aval = bfd_asymbol_value (s);
                    if (is64)
                        {
                            uint32_t hi = (uint32_t) (aval >> 32);
                            uint32_t lo = (uint32_t) (aval & 0xffffffffu);
                            bfd_byte w[8];
                            bfd_putb32 (hi, w);
                            bfd_putb32 (lo, w + 4);
                            if (bfd_write (w, 8, abfd) != 8)
                                return false;
                        }
                    else
                        {
                            uint32_t lo = (uint32_t) (aval & 0xffffffffu);
                            bfd_byte w[4];
                            bfd_putb32 (lo, w);
                            if (bfd_write (w, 4, abfd) != 4)
                                return false;
                        }
                    uint8_t type_plus = (uint8_t) (t + 0x80);
                    if (bfd_write (&type_plus, 1, abfd) != 1)
                        return false;
                    size_t nlen = strlen (s->name) + 1;
                    if (bfd_write (s->name, nlen, abfd) != (bfd_size_type) nlen)
                        return false;
                }
        }
    else
        {
            /* Fallback single symbol: T main at start address. */
            bfd_vma aval = abfd->start_address;
            if (is64)
                {
                    uint32_t hi = (uint32_t) (aval >> 32);
                    uint32_t lo = (uint32_t) (aval & 0xffffffffu);
                    bfd_byte w[8];
                    bfd_putb32 (hi, w);
                    bfd_putb32 (lo, w + 4);
                    if (bfd_write (w, 8, abfd) != 8)
                        return false;
                }
            else
                {
                    uint32_t lo = (uint32_t) (aval & 0xffffffffu);
                    bfd_byte w[4];
                    bfd_putb32 (lo, w);
                    if (bfd_write (w, 4, abfd) != 4)
                        return false;
                }
            uint8_t type_plus = (uint8_t) ('T' + 0x80);
            if (bfd_write (&type_plus, 1, abfd) != 1)
                return false;
            const char *name = "main";
            size_t nlen = strlen (name) + 1;
            if (bfd_write (name, nlen, abfd) != (bfd_size_type) nlen)
                return false;
        }

    /* Update header syms size and rewrite header. */
    {
        struct plan9_exec_hdr disk_hdr;
        memset (&disk_hdr, 0, sizeof (disk_hdr));
        uint32_t magic = hdr ? hdr->magic : R_MAGIC; /* default to 64-bit Plan 9 */
        bfd_putb32 (magic, &disk_hdr.magic);
        bfd_putb32 (textsz, &disk_hdr.text);
        bfd_putb32 (datasz, &disk_hdr.data);
        /* bss left as zero for now. */
        bfd_putb32 (0, &disk_hdr.bss);
        bfd_putb32 (symsize, &disk_hdr.syms);
        bfd_putb32 (abfd->start_address, &disk_hdr.entry);
        bfd_putb32 (0, &disk_hdr.spsz);
        bfd_putb32 (0, &disk_hdr.pcsz);
        if (bfd_seek (abfd, 0, SEEK_SET) != 0)
            return false;
        if (bfd_write (&disk_hdr, PLAN9_EXEC_HDR_SIZE, abfd) != PLAN9_EXEC_HDR_SIZE)
            return false;
    }

    return true;
}
#define plan9_bfd_link_split_section         _bfd_generic_link_split_section
#define plan9_bfd_link_check_relocs          _bfd_generic_link_check_relocs
#define plan9_bfd_gc_sections                bfd_generic_gc_sections
#define plan9_bfd_lookup_section_flags       bfd_generic_lookup_section_flags
#define plan9_bfd_merge_sections             bfd_generic_merge_sections
#define plan9_bfd_is_group_section           bfd_generic_is_group_section
#define plan9_bfd_group_name                 bfd_generic_group_name
#define plan9_bfd_discard_group              bfd_generic_discard_group
#define plan9_section_already_linked         _bfd_generic_section_already_linked
#define plan9_bfd_define_common_symbol       bfd_generic_define_common_symbol
#define plan9_bfd_link_hide_symbol           _bfd_generic_link_hide_symbol
#define plan9_bfd_define_start_stop          bfd_generic_define_start_stop

/* For the plan9-object backend we use a separate NAME to keep macros clear. */
#define plan9obj_sizeof_headers                 _bfd_nolink_sizeof_headers
#define plan9obj_bfd_get_relocated_section_contents bfd_generic_get_relocated_section_contents
#define plan9obj_bfd_relax_section              bfd_generic_relax_section
#define plan9obj_bfd_link_hash_table_create     _bfd_generic_link_hash_table_create
#define plan9obj_bfd_link_add_symbols           _bfd_generic_link_add_symbols
#define plan9obj_bfd_link_just_syms             _bfd_generic_link_just_syms
#define plan9obj_bfd_copy_link_hash_symbol_type _bfd_generic_copy_link_hash_symbol_type
#define plan9obj_bfd_final_link                 _bfd_generic_final_link
#define plan9obj_bfd_link_split_section         _bfd_generic_link_split_section
#define plan9obj_bfd_link_check_relocs          _bfd_generic_link_check_relocs
#define plan9obj_bfd_gc_sections                bfd_generic_gc_sections
#define plan9obj_bfd_lookup_section_flags       bfd_generic_lookup_section_flags
#define plan9obj_bfd_merge_sections             bfd_generic_merge_sections
#define plan9obj_bfd_is_group_section           bfd_generic_is_group_section
#define plan9obj_bfd_group_name                 bfd_generic_group_name
#define plan9obj_bfd_discard_group              bfd_generic_discard_group
#define plan9obj_section_already_linked         _bfd_generic_section_already_linked
#define plan9obj_bfd_define_common_symbol       bfd_generic_define_common_symbol
#define plan9obj_bfd_link_hide_symbol           _bfd_generic_link_hide_symbol
#define plan9obj_bfd_define_start_stop          bfd_generic_define_start_stop

/* plan9obj symbol-table helpers are provided by plan9obj.c; prototypes
   come from plan9obj.h which we include above. */
#define plan9obj_get_symbol_version_string _bfd_nosymbols_get_symbol_version_string
#define plan9obj_bfd_is_local_label_name  bfd_generic_is_local_label_name
#define plan9obj_bfd_is_target_special_symbol _bfd_bool_bfd_asymbol_false
#define plan9obj_get_lineno               _bfd_nosymbols_get_lineno
#define plan9obj_find_nearest_line        _bfd_nosymbols_find_nearest_line
#define plan9obj_find_nearest_line_with_alt _bfd_nosymbols_find_nearest_line_with_alt
#define plan9obj_find_line                _bfd_nosymbols_find_line
#define plan9obj_find_inliner_info        _bfd_nosymbols_find_inliner_info
#define plan9obj_bfd_make_debug_symbol    _bfd_nosymbols_bfd_make_debug_symbol
#define plan9obj_read_minisymbols         _bfd_generic_read_minisymbols
#define plan9obj_minisymbol_to_symbol     _bfd_generic_minisymbol_to_symbol
/* Relocation jump table wiring for plan9-object backend */
#define plan9obj_get_reloc_upper_bound     plan9obj_get_reloc_upper_bound
#define plan9obj_canonicalize_reloc        plan9obj_canonicalize_reloc
#define plan9obj_bfd_reloc_type_lookup     plan9obj_bfd_reloc_type_lookup
#define plan9obj_bfd_reloc_name_lookup     plan9obj_bfd_reloc_name_lookup
#define plan9obj_set_reloc                  _bfd_generic_set_reloc
/* Define targets for other architectures */
PLAN9_TARGET(amd64, S_MAGIC, bfd_arch_i386, bfd_mach_x86_64);
PLAN9_TARGET(386, I_MAGIC, bfd_arch_i386, bfd_mach_i386_i386);
PLAN9_TARGET(arm, E_MAGIC, bfd_arch_arm, bfd_mach_arm_unknown);
PLAN9_TARGET(arm64, R_MAGIC, bfd_arch_aarch64, bfd_mach_aarch64);
PLAN9_TARGET(power, Q_MAGIC, bfd_arch_powerpc, bfd_mach_ppc);
PLAN9_TARGET(power64, T_MAGIC, bfd_arch_powerpc, bfd_mach_ppc64);

/* Define the plan9-object backend which recognizes raw Plan 9 object streams.
   Phase 1: symbol + section sizing only (no relocations). */
/* Select the recognizer: prefer plan9obj.c when enabled, otherwise use
   the tiny local fallback that only checks the opcode prefix. */
#define PLAN9OBJ_OBJECT_P_FN plan9_object_p
const bfd_target plan9_object_vec = {
    "plan9-object",                 /* Name */
    bfd_target_unknown_flavour,
    BFD_ENDIAN_LITTLE,
    BFD_ENDIAN_LITTLE,
    (HAS_SYMS | HAS_LOCALS),          /* We expose symbols but no relocs yet. */
    (SEC_CODE | SEC_DATA | SEC_HAS_CONTENTS | SEC_ALLOC | SEC_LOAD),
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
     /* Recognize only object files here. Do not advertise archive support,
         or BFD may misclassify raw .7 objects as archives. */
     { _bfd_dummy_target, PLAN9OBJ_OBJECT_P_FN, _bfd_dummy_target, _bfd_dummy_target },
    { _bfd_bool_bfd_false_error, _bfd_bool_bfd_false_error, _bfd_generic_mkarchive, _bfd_bool_bfd_false_error },
    { _bfd_bool_bfd_false_error, _bfd_bool_bfd_false_error, _bfd_write_archive_contents, _bfd_bool_bfd_false_error },
        BFD_JUMP_TABLE_GENERIC (_bfd_generic), /* close/free stays generic */
        BFD_JUMP_TABLE_COPY (_bfd_generic),
    BFD_JUMP_TABLE_CORE (_bfd_nocore),
        BFD_JUMP_TABLE_ARCHIVE (_bfd_archive_bsd),
        BFD_JUMP_TABLE_SYMBOLS (plan9obj),
    BFD_JUMP_TABLE_RELOCS (plan9obj),
        BFD_JUMP_TABLE_WRITE (_bfd_generic), /* set_section_contents ok */
    BFD_JUMP_TABLE_LINK (plan9obj),
    BFD_JUMP_TABLE_DYNAMIC (_bfd_nodynamic),
    NULL,
    NULL
};