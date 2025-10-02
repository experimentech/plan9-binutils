/* Plan 9 Object File BFD backend - implements 9front opcode stream format
 * Copyright (C) 2025 Free Software Foundation, Inc.
 * Based on authoritative 9front source documentation
 *
 * Phase 1: minimal integration for GNU ld. We synthesize approximate
 * section sizes and a symbol table from the opcode stream so the linker
 * can at least enumerate and place code/data. No relocation records yet.
 */

#include "sysdep.h"
#include "bfd.h"
#include "libbfd.h"
#include "plan9obj.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Debugging helper: guard plan9obj internal diagnostics behind an
   environment-controlled function so developers can re-enable them when
   needed without editing source. The helper checks PLAN9OBJ_DEBUG and
   BFD_VERBOSE once at runtime to avoid repeated getenv cost. */
static int plan9obj_debug_enabled = -1;
static void
plan9obj_dbg (bfd *abfd, const char *fmt, ...)
{
    va_list ap;
    if (plan9obj_debug_enabled < 0)
    {
        const char *e1 = getenv ("PLAN9OBJ_DEBUG");
        const char *e2 = getenv ("BFD_VERBOSE");
        plan9obj_debug_enabled = ((e1 && *e1) || (e2 && *e2)) ? 1 : 0;
    }
    if (!plan9obj_debug_enabled)
        return;

    va_start (ap, fmt);
    /* Format the incoming message into a local buffer, then pass it to
       BFD's central error handler so callers (objdump, nm, ld, etc.) can
       route backend diagnostics through their registered callbacks. We
       prefix with the bfd pointer using %pB when available to give
       context similar to other backends. */
    char msgbuf[2048];
    vsnprintf (msgbuf, sizeof (msgbuf), fmt, ap);
    va_end (ap);

    if (abfd)
    {
        /* Use BFD's error handler which understands %pB and routing. */
        _bfd_error_handler ("%pB: %s", abfd, msgbuf);
    }
    else
    {
        _bfd_error_handler ("%s", msgbuf);
    }
}

/* Convenience macro to preserve call site brevity; callers must have 'abfd' in scope. */
#define PLAN9OBJ_DBG(...) plan9obj_dbg (abfd, __VA_ARGS__)

/* Event emission: enable by exporting PLAN9OBJ_EVENTS=1. Events are emitted as
   compact JSON lines prefixed with 'PLAN9OBJ-EVENT: ' on stderr for machine
   consumption by the regression tools. */
static int plan9obj_events_enabled = -1;
static void plan9obj_events_init (void)
{
    const char *e = getenv ("PLAN9OBJ_EVENTS");
    plan9obj_events_enabled = (e && *e) ? 1 : 0;
}

static char *plan9obj_json_escape (const char *s, char *buf, size_t buflen)
{
    if (!s || buflen == 0) { if (buflen) buf[0] = '\0'; return buf; }
    char *dst = buf;
    size_t remaining = buflen;
    while (*s && remaining > 1) {
        unsigned char c = (unsigned char) *s++;
        if (c == '"' || c == '\\') {
            if (remaining <= 2) break;
            *dst++ = '\\';
            *dst++ = (char) c;
            remaining -= 2;
        } else if (c >= 0x20 && c != '\n' && c != '\r' && c != '\t') {
            *dst++ = (char) c;
            remaining--;
        } else {
            if (remaining <= 6) break;
            int written = snprintf (dst, remaining, "\\u%04x", (int) c);
            if (written <= 0) break;
            dst += written;
            remaining -= (size_t) written;
        }
    }
    *dst = '\0';
    return buf;
}

static void plan9obj_emit_event (unsigned int pass, unsigned int opcode_pos, unsigned int idx,
                                 const char *rec, const char *action,
                                 const char *oldn, const char *newn)
{
    if (plan9obj_events_enabled < 0)
        plan9obj_events_init ();
    if (!plan9obj_events_enabled) {
        /* If explicit event emission not enabled, allow emission when debug
           diagnostics are enabled (so verbose runs will still produce events).
           Ensure plan9obj_debug_enabled is initialized similarly to plan9obj_dbg. */
        if (plan9obj_debug_enabled < 0) {
            const char *e1 = getenv ("PLAN9OBJ_DEBUG");
            const char *e2 = getenv ("BFD_VERBOSE");
            plan9obj_debug_enabled = ((e1 && *e1) || (e2 && *e2)) ? 1 : 0;
        }
        if (!plan9obj_debug_enabled)
            return;
    }

    char oldbuf[256];
    char newbuf[256];
    plan9obj_json_escape (oldn ? oldn : "", oldbuf, sizeof oldbuf);
    plan9obj_json_escape (newn ? newn : "", newbuf, sizeof newbuf);
    fprintf (stderr, "PLAN9OBJ-EVENT: {\"pass\":%u,\"pos\":%u,\"idx\":%u,\"rec\":\"%s\",\"action\":\"%s\",\"old\":\"%s\",\"new\":\"%s\"}\n",
             pass, opcode_pos, idx, rec ? rec : "", action ? action : "", oldbuf, newbuf);
}

/* Minimal AArch64 relocation howtos for Plan 9 object usage. We only need
     a couple to link simple programs: absolute 64-bit and branch/call 26-bit. */
static reloc_howto_type plan9obj_aarch64_abs64_howto =
    HOWTO (BFD_RELOC_64,        /* type */
                 0,                   /* rightshift */
                 3,                   /* size (3==8 bytes) */
                 64,                  /* bitsize */
                 false,               /* pc_relative */
                 0,                   /* bitpos */
                 complain_overflow_bitfield,
                 NULL, /* special_function */
                 "ABS64",            /* name */
                 false,               /* partial_inplace */
                 0,                   /* src_mask */
                 0,                   /* dst_mask */
                 false);              /* pcrel_offset */

static reloc_howto_type plan9obj_aarch64_call26_howto =
    HOWTO (BFD_RELOC_AARCH64_CALL26,
                 2,                   /* rightshift: low 2 bits not encoded */
                 2,
                 26,
                 true,                /* pc-relative */
                 0,
                 complain_overflow_signed,
         NULL,
                 "CALL26",
                 false,
                 0,
                 0,
                 true);

/* Plan 9 Object File Detection - from 9front isobjfile() */
#define PLAN9_OBJ_MAGIC1_OFFSET 2
#define PLAN9_OBJ_MAGIC2_OFFSET 3  
#define PLAN9_OBJ_MAGIC3_OFFSET 4
#define PLAN9_OBJ_MAGIC_BYTE 1
#define PLAN9_OBJ_MAGIC_LT '<'

/* Opcodes for arm64 from 9front sys/src/cmd/7c/7.out.h */
/* Verified via a small dump program in this workspace. */
#define ATEXT    309    /* Text section start */
#define ADATA    310    /* Data initialization */
#define AGLOBL   311    /* Global symbol */
#define ANAME    313    /* Symbol name record */
#define ASIGNAME 320    /* Symbol name with signature */
#define AEND     323    /* End of object */

/* Address encoding is fixed-format for arm64; no T_* flags needed here. */

/* Symbol table limits - from *.out.h */
#define NSYM 256    /* Local symbol table size (expanded to handle real-world .7 files) */
#define NSNAME 8    /* String constant size */

/* Minimal subset of D_* constants for arm64 from 7.out.h (values verified) */
#define D_GOK    0
#define D_NONE   1
#define D_EXTERN 2
#define D_STATIC 3
#define D_AUTO   4
#define D_PARAM  5
#define D_BRANCH 6
#define D_OREG   7
#define D_XPRE   8
#define D_XPOST  9
#define D_CONST  10
#define D_DCONST 11
#define D_FCONST 12
#define D_SCONST 13
#define D_REG    14
#define D_SP     15
#define D_FREG   16
#define D_VREG   17
#define D_SPR    18
#define D_SHIFT  22
#define D_EXTREG 27
#define D_ROFF   28
#define D_COND   29

/* Plan 9 object file private data */
struct plan9_obj_tdata {
    asymbol **symbols;           /* exported symbol vector for BFD */
    unsigned int symbol_count;   /* number of exported symbols */
    asection *text_section;
    asection *data_section;
    asection *bss_section;
    bfd_size_type text_size;
    bfd_size_type data_size;
    /* Relocations synthesized from opcode stream. */
    arelent **text_relocs;
    long text_reloc_count;
    arelent **data_relocs;
    long data_reloc_count;
};

#define plan9_obj_tdata(bfd) ((struct plan9_obj_tdata *)(bfd)->tdata.any)

/* Forward declarations */
bfd_cleanup plan9_object_p (bfd *);
bool plan9obj_mkobject (bfd *);
bool plan9obj_write_object_contents (bfd *);
long plan9obj_get_symtab_upper_bound (bfd *);
long plan9obj_canonicalize_symtab (bfd *, asymbol **);
asymbol *plan9obj_make_empty_symbol (bfd *);
void plan9obj_get_symbol_info (bfd *, asymbol *, symbol_info *);
void plan9obj_print_symbol (bfd *abfd, void *filep, asymbol *symbol, bfd_print_symbol_type how);
static bool parse_plan9_object (bfd *abfd);
static reloc_howto_type *plan9obj_aarch64_howto_from_code (bfd_reloc_code_real_type code);
static void plan9_dname_str (int d, char *buf, size_t buflen) __attribute__((unused));
static void plan9_dname_str (int d, char *buf, size_t buflen)
{
    if (!buf || buflen == 0) return;
    switch (d) {
        case D_OREG:    snprintf (buf, buflen, "D_OREG"); break;
        case D_STATIC:  snprintf (buf, buflen, "D_STATIC"); break;
        case D_EXTERN:  snprintf (buf, buflen, "D_EXTERN"); break;
        case D_CONST:   snprintf (buf, buflen, "D_CONST"); break;
        case D_DCONST:  snprintf (buf, buflen, "D_DCONST"); break;
        case D_SCONST:  snprintf (buf, buflen, "D_SCONST"); break;
        case D_REG:     snprintf (buf, buflen, "D_REG"); break;
        case D_SP:      snprintf (buf, buflen, "D_SP"); break;
        case D_FREG:    snprintf (buf, buflen, "D_FREG"); break;
        case D_VREG:    snprintf (buf, buflen, "D_VREG"); break;
        case D_ROFF:    snprintf (buf, buflen, "D_ROFF"); break;
        case D_BRANCH:  snprintf (buf, buflen, "D_BRANCH"); break;
        default:        snprintf (buf, buflen, "D_%d", d); break;
    }
}

static inline bool
plan9_name_is_path_like (const char *name)
{
    if (!name || !*name)
        return false;
    if (name[0] == '<')
        return true;
    if (name[0] == '.' && name[1] == '/')
        return true;
    return strchr (name, '/') != NULL;
}

/* Plan 9 object file private helper: decide if a new symbol name should replace an existing one.
   Heuristic rules (conservative):
   - If no old name, accept new.
   - If old is path-like (< or contains '/') prefer new.
   - If old contains '$' and new does not -> prefer new.
   - If new contains '$' and old does not -> do not replace.
   - If old starts with '.' (like .string) and new does not -> prefer new.
   - Otherwise keep the existing name to preserve stability. */
static inline bool
plan9_is_word (const char *s)
{
    if (!s || s[0] == '\0') return false;
    if (s[0] == '.') return false;
    for (const char *p = s; *p; ++p) {
        char c = *p;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'))
            return false;
    }
    return true;
}

static bool
plan9_should_replace_symname (const char *oldn, const char *newn)
{
    if (!newn) return false;
    if (!oldn) return true;

    bool old_is_path_like = plan9_name_is_path_like (oldn);
    bool new_is_path_like = plan9_name_is_path_like (newn);

    bool old_word = plan9_is_word (oldn);
    bool new_word = plan9_is_word (newn);

    /* Replace if the old name is clearly path-/table-like and new is a tidy word. */
    if ((old_is_path_like || strchr(oldn, '$') || oldn[0] == '.') && new_word && !new_is_path_like)
        return true;

    /* Do not replace a good, word-like existing name with another name that is not strictly better. */
    if (old_word && new_word)
        return false;

    /* If old is not word-like but new is word-like prefer new. */
    if (!old_word && new_word)
        return true;

    /* Otherwise, default to keeping the existing name for stability. */
    return false;
}

static bool
plan9_register_local_symbol (bfd *abfd, asymbol **local_syms,
                             uint8_t sym_index, const char *name,
                             unsigned int pass, unsigned int opcode_pos,
                             const char *opcode_name)
{
    if (!name || !*name)
        return false;

    if (plan9_name_is_path_like (name))
    {
        PLAN9OBJ_DBG ("[plan9obj][%s] pos=0x%08x idx=%u skipped path-like name='%s'\n",
                      opcode_name, opcode_pos, (unsigned int) sym_index, name);
        plan9obj_emit_event (pass, opcode_pos, sym_index,
                             opcode_name, "skipped-path", NULL, name);
        return false;
    }

    asymbol *existing = local_syms[sym_index];
    const char *oldn = existing ? existing->name : NULL;

    if (existing && !plan9_should_replace_symname (oldn, name))
    {
        PLAN9OBJ_DBG ("[plan9obj][%s] pos=0x%08x idx=%u kept old='%s' candidate='%s'\n",
                      opcode_name, opcode_pos, (unsigned int) sym_index,
                      oldn ? oldn : "(null)", name);
        plan9obj_emit_event (pass, opcode_pos, sym_index,
                             opcode_name, "kept",
                             oldn ? oldn : NULL, name);
        return false;
    }

    asymbol *sym = existing ? existing : bfd_make_empty_symbol (abfd);
    if (!sym)
        return false;

    size_t len = strlen (name);
    char *stored = (char *) bfd_alloc (abfd, len + 1);
    if (!stored)
        return false;
    memcpy (stored, name, len + 1);

    sym->name = stored;
    sym->value = 0;
    sym->flags = BSF_LOCAL;
    sym->the_bfd = abfd;
    local_syms[sym_index] = sym;

    const char *action = existing ? "created_or_replaced" : "created";
    PLAN9OBJ_DBG ("[plan9obj][%s] pos=0x%08x idx=%u %s old='%s' new='%s'\n",
                  opcode_name, opcode_pos, (unsigned int) sym_index,
                  action, oldn ? oldn : "(null)", name);
    plan9obj_emit_event (pass, opcode_pos, sym_index,
                         opcode_name, action,
                         oldn ? oldn : NULL, name);
    return true;
}

/* Object file detection - implements 9front isobjfile() logic */
bfd_cleanup
plan9_object_p (bfd *abfd)
{
    unsigned char buf[5];
    
    /* Read first 5 bytes */
    if (bfd_seek (abfd, 0, SEEK_SET) != 0)
        return NULL;
    
    if (bfd_read (buf, 5, abfd) != 5)
        return NULL;
    
    /* Check Plan 9 object file magic pattern */
    if ((buf[2] == PLAN9_OBJ_MAGIC_BYTE && buf[3] == PLAN9_OBJ_MAGIC_LT) ||
        (buf[3] == PLAN9_OBJ_MAGIC_BYTE && buf[4] == PLAN9_OBJ_MAGIC_LT))
    {
          /* This looks like a Plan 9 object file.  Format is already
              preset by the caller (bfd_check_format). */
        /* Default to aarch64 in this workspace; safe fallback if unknown. */
        if (!bfd_set_arch_mach (abfd, bfd_arch_aarch64, bfd_mach_aarch64))
            bfd_set_arch_mach (abfd, bfd_arch_unknown, 0);
        /* Build sections now; parsing is best-effort and non-fatal here. */
        if (!plan9obj_mkobject (abfd))
            return NULL;
        /* Try to parse to size sections and collect symbols, but don't fail
           recognition if heuristics do not fully parse the file. */
        (void) parse_plan9_object (abfd);
        return _bfd_no_cleanup;
    }
    
    return NULL;
}

/* Create Plan 9 object file structure */
bool
plan9obj_mkobject (bfd *abfd)
{
    struct plan9_obj_tdata *tdata;
    size_t amt = sizeof (struct plan9_obj_tdata);
    
    tdata = (struct plan9_obj_tdata *) bfd_zalloc (abfd, amt);
    if (tdata == NULL)
        return false;
    
    abfd->tdata.any = tdata;
    
    /* Create standard sections */
    tdata->text_section = bfd_make_section_with_flags (abfd, ".text",
        SEC_CODE | SEC_LOAD | SEC_ALLOC | SEC_HAS_CONTENTS | SEC_RELOC);

    tdata->data_section = bfd_make_section_with_flags (abfd, ".data", 
        SEC_DATA | SEC_LOAD | SEC_ALLOC | SEC_HAS_CONTENTS | SEC_RELOC);
    
    tdata->bss_section = bfd_make_section_with_flags (abfd, ".bss",
        SEC_ALLOC);
    
    if (!tdata->text_section || !tdata->data_section || !tdata->bss_section)
        return false;
    
    /* Mark that this object will expose symbols (tools like nm/objdump key off this). */
    abfd->flags |= HAS_SYMS | HAS_LOCALS;

    /* Try to parse now to size sections and collect symbols.
       If heuristics fail, keep the object recognized with empty sections. */
    (void) parse_plan9_object (abfd);
    return true;
}

/* Forward declare plan9_address defined in header to satisfy prototypes here. */
struct plan9_address;

/* Parse address encoding for arm64 .7 objects (based on 9front libmach 7obj.c) */
static unsigned int
parse_plan9_address (bfd *abfd, unsigned char *data, unsigned int offset,
                     size_t size, asymbol **local_syms, struct plan9_address *addr)
{
    unsigned int pos = offset;

    if (pos >= size) return 0;
    memset (addr, 0, sizeof (*addr));
    addr->type = data[pos++];
    if (pos >= size) return 0;
    addr->reg = data[pos++];
    if (pos >= size) return 0;
    addr->sym_index = data[pos++];
    if (pos >= size) return 0;
    addr->name = data[pos++];
    addr->symbol = NULL;

    switch (addr->type) {
        default:
            /* No extra payload */
            break;
        case D_OREG:
        case D_XPRE:
        case D_XPOST:
        case D_CONST:
        case D_BRANCH:
        case D_SHIFT:
        case D_EXTREG:
        case D_ROFF:
        case D_SPR: {
            if (pos + 3 >= size) return 0;
            int32_t off = (int32_t)(data[pos] | (data[pos+1] << 8) | (data[pos+2] << 16) | (data[pos+3] << 24));
            addr->offset = (int64_t) off;
            pos += 4;
            break;
        }
        case D_DCONST: {
            if (pos + 7 >= size) return 0;
            /* little-endian 64-bit */
            uint64_t lo = (uint32_t)(data[pos] | (data[pos+1] << 8) | (data[pos+2] << 16) | (data[pos+3] << 24));
            uint64_t hi = (uint32_t)(data[pos+4] | (data[pos+5] << 8) | (data[pos+6] << 16) | (data[pos+7] << 24));
            addr->offset = (int64_t)(lo | (hi << 32));
            pos += 8;
            break;
        }
        case D_SCONST: {
            if (pos + NSNAME - 1 >= size) return 0;
            memcpy (addr->sconst, data + pos, NSNAME);
            pos += NSNAME;
            break;
        }
        case D_FCONST: {
            if (pos + 7 >= size) return 0;
            memcpy (addr->fconst, data + pos, 8);
            pos += 8;
            break;
        }
    }

    /* sym_index is an 8-bit index (0..255); NSYM is 256 so the explicit
       bound check is redundant and triggers -Wtype-limits. Rely on the
       resolved local_syms[] lookup (which is sized NSYM) for safety. */
    (void) abfd; /* parameter unused in this parser helper */
    if (local_syms[addr->sym_index])
        addr->symbol = local_syms[addr->sym_index];

    return pos - offset;
}

/* Parse Plan 9 object file - implements ldobj() logic from 9front (subset) */
static bool
parse_plan9_object (bfd *abfd)
{
    /* Parse opcode stream */
    asymbol *local_syms[NSYM] = {0};
    /* NOTE: counters, local_types[], and collected[] were removed to
       silence warnings while they are not used. They may be reintroduced
       later when heuristics/collection kick in. */

    /* Read entire file into memory */
    size_t size = bfd_get_file_size (abfd);
    if (size == 0 || size > 0x10000000) /* Sanity check: 256MB max */
        return false;
    
    unsigned char *data = (unsigned char *) bfd_malloc (size);
    if (!data)
        return false;
    
    if (bfd_seek (abfd, 0, SEEK_SET) != 0 ||
        bfd_read (data, size, abfd) != size) {
        free (data);
        return false;
    }
    
    /* Prepare reloc arrays (grow on demand in a simple doubling scheme). */
    long text_rel_cap = 8, data_rel_cap = 8;
    plan9_obj_tdata(abfd)->text_relocs = (arelent **) bfd_zalloc (abfd, text_rel_cap * sizeof (arelent *));
    plan9_obj_tdata(abfd)->data_relocs = (arelent **) bfd_zalloc (abfd, data_rel_cap * sizeof (arelent *));
    plan9_obj_tdata(abfd)->text_reloc_count = 0;
    plan9_obj_tdata(abfd)->data_reloc_count = 0;

    /* State shared between passes */
    bool in_prefix = true;        /* used while scanning initial name-prefix area */
    bool in_name_prefix2 = false; /* set true at start of pass2 */
    /* Runtime toggle to control ATEXT strictness: if 0 (via env var), be permissive. */
    int atext_strict = 1;
    {
        const char *ev = getenv("PLAN9OBJ_ATEXT_STRICT");
        if (ev && strcmp(ev, "0") == 0) {
            atext_strict = 0;
            /* Emit an event so verbose runs show the toggle. */
            if (plan9obj_events_enabled <= 0) {
                /* ensure event subsystem initialised so we can emit this early */
                plan9obj_events_init ();
            }
            plan9obj_emit_event (0, 0, 0, "ATEXT", "strictness", NULL, atext_strict ? "strict" : "loose");
        }
    }
    (void) atext_strict; /* currently advisory only; keep quiet when unused */

    /* Parse opcode stream: two-pass approach
       - Pass 1: scan for ANAME/ASIGNAME to populate local_syms/local_types so
         forward references are resolved.
       - Pass 2: full parse which sizes sections, collects symbols, and
         synthesizes relocations. */

    /* --- PASS 1: collect ANAME/ASIGNAME entries --- */
    {
        unsigned int p = 0;
        /* in_prefix already declared in function scope; ensure it's set for pass1 */
        in_prefix = true;
        while (p + 1 < size) {
            unsigned int opcode_pos = p;
            uint16_t opcode = data[p] | (data[p+1] << 8);
            p += 2;
            if (opcode == ANAME || opcode == ASIGNAME) {
                bool parsed_name = false;
                unsigned int save = p;
                if (opcode == ANAME) {
                    if (p + 2 < size) {
                        uint8_t v = data[p];
                        uint8_t idx = data[p + 1];
                        unsigned int nstart = p + 2;
                        unsigned int nend = nstart;
                        while (nend < size && (nend - nstart) < 512 && data[nend] != 0)
                            nend++;
                        if (nend < size && v < 0x80 && nend > nstart && data[nend] == 0) {
                            bool printable = true;
                            for (unsigned int i = nstart; i < nend; i++) {
                                unsigned char ch = data[i];
                                if (ch < 0x20 || ch > 0x7e) { printable = false; break; }
                            }
                            if (printable) {
                                const char *nm = (const char *) (data + nstart);
                                plan9_register_local_symbol (abfd, local_syms, idx, nm,
                                                             1, opcode_pos, "ANAME");
                                p = nend + 1;
                                parsed_name = true;
                            }
                        }
                    }
                } else { /* ASIGNAME */
                    p = save;
                    if (p + 6 < size) {
                        uint8_t v = data[p + 4];
                        uint8_t idx = data[p + 5];
                        unsigned int nstart = p + 6;
                        unsigned int nend = nstart;
                        while (nend < size && (nend - nstart) < 512 && data[nend] != 0)
                            nend++;
                        if (nend < size && v < 0x80 && nend > nstart && data[nend] == 0) {
                            bool printable = true;
                            for (unsigned int i = nstart; i < nend; i++) {
                                unsigned char ch = data[i];
                                if (ch < 0x20 || ch > 0x7e) { printable = false; break; }
                            }
                            if (printable) {
                                const char *nm = (const char *) (data + nstart);
                                plan9_register_local_symbol (abfd, local_syms, idx, nm,
                                                             1, opcode_pos, "ASIGNAME");
                                p = nend + 1;
                                parsed_name = true;
                            }
                        }
                    }
                }
                if (parsed_name)
                    continue;
                p = save;
                 /* Non-name opcode encountered; leave payload handling to the
                    existing branch below.  We'll clear the "in_prefix" flag
                    once at the end of the loop so both failed-parses and
                    non-name opcodes behave identically without duplicated
                    assignments. */
             } else {
                 /* Skip the record payload without performing any symbol/reloc work.
                    Use the same address-parsing helper to advance over operands. */
                 if (opcode == AEND)
                     break;
                 if (p >= size) break;
                 uint8_t rflag = data[p++];
                 if (p + 3 >= size) break;
                 p += 4; /* skip 4-byte line */
                 struct plan9_address tmpa;
                 unsigned int consumed = parse_plan9_address (abfd, data, p, size, local_syms, &tmpa);
                 if (consumed == 0) break;
                 p += consumed;
                 if (rflag & 0x40) {
                     consumed = parse_plan9_address (abfd, data, p, size, local_syms, &tmpa);
                     if (consumed == 0) break;
                     p += consumed;
                 }
                 consumed = parse_plan9_address (abfd, data, p, size, local_syms, &tmpa);
                 if (consumed == 0) break;
                 p += consumed;
                 /* Continue to next record */
             }
         }
         /* At end-of-iteration: if we fell through (i.e. not a parsed name)
            clear the prefix flag so subsequent non-name records don't
            repeatedly consider we are still in the name-prefix area. This
            centralizes the assignment for clarity. */
         if (in_prefix) in_prefix = false;
         /* After finishing the pass1 scan, continue... */
     }

    /* --- PASS 2: full parse, now that local_syms[] is populated --- */
    {
        unsigned int p = 0;
        /* per-pass counters removed until needed */
        /* reuse function-scoped flag for pass2 */
        in_name_prefix2 = true;
        /* Temporary recorded relocation table: we can't finalize arelent->sym_ptr_ptr
           until we've allocated the exported symbol vector, so record reloc targets
           by their original ANAME index here. */
        struct recorded_reloc {
            unsigned int addr;       /* offset inside section */
            int sym_index;           /* target symbol ANAME index */
            int dest_sym_index;      /* symbol owning the relocation slot (for data) */
            unsigned int width;      /* width in bytes of the relocation field */
            int64_t addend;          /* relocation addend */
            int is_text;             /* 0 => data reloc (ABS64), 1 => text (CALL26) */
        };
                struct recorded_reloc *rec_relocs = NULL;
                long rec_reloc_cap = 0, rec_reloc_count = 0;
                bfd_size_type data_extent = 0;
                while (p + 1 < size) {
            unsigned int opcode_pos = p;
            uint16_t opcode = data[p] | (data[p+1] << 8);
            p += 2;

            /* Handle ANAME/ASIGNAME records anywhere in the stream. */
            if (opcode == ANAME || opcode == ASIGNAME) {
                bool parsed_name = false;
                unsigned int save = p;

                if (opcode == ANAME) {
                    if (p + 2 < size) {
                        uint8_t v = data[p];
                        uint8_t idx = data[p + 1];
                        unsigned int nstart = p + 2;
                        unsigned int nend = nstart;
                        while (nend < size && (nend - nstart) < 512 && data[nend] != 0)
                            nend++;
                        if (nend < size && v < 0x80 && nend > nstart && data[nend] == 0) {
                            bool printable = true;
                            for (unsigned int i = nstart; i < nend; i++) {
                                unsigned char ch = data[i];
                                if (ch < 0x20 || ch > 0x7e) { printable = false; break; }
                            }
                            if (printable) {
                                const char *nm = (const char *) (data + nstart);
                                plan9_register_local_symbol (abfd, local_syms, idx, nm,
                                                             2, opcode_pos, "ANAME");
                                p = nend + 1;
                                parsed_name = true;
                            }
                        }
                    }
                } else { /* ASIGNAME */
                    p = save;
                    if (p + 6 < size) {
                        uint8_t v = data[p + 4];
                        uint8_t idx = data[p + 5];
                        unsigned int nstart = p + 6;
                        unsigned int nend = nstart;
                        while (nend < size && (nend - nstart) < 512 && data[nend] != 0)
                            nend++;
                        if (nend < size && v < 0x80 && nend > nstart && data[nend] == 0) {
                            bool printable = true;
                            for (unsigned int i = nstart; i < nend; i++) {
                                unsigned char ch = data[i];
                                if (ch < 0x20 || ch > 0x7e) { printable = false; break; }
                            }
                            if (printable) {
                                const char *nm = (const char *) (data + nstart);
                                plan9_register_local_symbol (abfd, local_syms, idx, nm,
                                                             2, opcode_pos, "ASIGNAME");
                                p = nend + 1;
                                parsed_name = true;
                            }
                        }
                    }
                }
                if (parsed_name)
                    continue;
                p = save;
                 /* Non-name opcode encountered; leave payload handling to the
                    existing branch below.  We'll clear the "in_prefix" flag
                    once at the end of the loop so both failed-parses and
                    non-name opcodes behave identically without duplicated
                    assignments. */
             } else {
                 if (opcode == AEND)
                     break;
                 if (p >= size)
                     break;

                 uint8_t attr = data[p++];
                 uint8_t regbits = attr & 0x3f;
                 bool has_from3 = (attr & 0x40) != 0;

                 if (p + 3 >= size)
                     break;
                 p += 4; /* skip line number */

                 struct plan9_address addr_from;
                 struct plan9_address addr_from3;
                 struct plan9_address addr_to;
                 memset (&addr_from, 0, sizeof addr_from);
                 memset (&addr_from3, 0, sizeof addr_from3);
                 memset (&addr_to, 0, sizeof addr_to);

                 unsigned int consumed = parse_plan9_address (abfd, data, p, size, local_syms, &addr_from);
                 if (consumed == 0)
                     break;
                 p += consumed;

                 if (has_from3) {
                     consumed = parse_plan9_address (abfd, data, p, size, local_syms, &addr_from3);
                     if (consumed == 0)
                         break;
                     p += consumed;
                 }

                 consumed = parse_plan9_address (abfd, data, p, size, local_syms, &addr_to);
                 if (consumed == 0)
                     break;
                 p += consumed;

                 if (opcode == ADATA) {
                     PLAN9OBJ_DBG ("[plan9obj][ADATA parse] pos=0x%08x len=%u dest.type=%u dest.sym=%u dest.off=%lld src.type=%u src.sym=%u src.off=%lld\n",
                                   opcode_pos,
                                   (unsigned int) regbits,
                                   (unsigned int) addr_from.type,
                                   (unsigned int) addr_from.sym_index,
                                   (long long) addr_from.offset,
                                   (unsigned int) addr_to.type,
                                   (unsigned int) addr_to.sym_index,
                                   (long long) addr_to.offset);

                     if (regbits && addr_from.offset >= 0) {
                         bfd_size_type potential = (bfd_size_type) addr_from.offset + regbits;
                         if (potential > data_extent)
                             data_extent = potential;
                     }

                     int target_idx = -1;
                     unsigned int target_addr = (unsigned int) (addr_from.offset >= 0 ? addr_from.offset : 0);
                     int64_t target_addend = 0;

                     if (addr_to.symbol)
                     {
                         target_idx = (int) addr_to.sym_index;
                         target_addend = addr_to.offset;
                     }
                     else if (addr_from.symbol && !addr_to.symbol && addr_from.type == D_CONST)
                     {
                         target_idx = (int) addr_from.sym_index;
                         target_addend = addr_from.offset;
                     }

                     if (target_idx >= 0)
                     {
                         int dest_local_idx = addr_from.symbol ? (int) addr_from.sym_index : -1;
                         unsigned int slot_width = regbits ? regbits : 8;
                         bool dup = false;
                         for (long _ri = 0; _ri < rec_reloc_count; ++_ri)
                         {
                             if (rec_relocs[_ri].addr == target_addr
                                 && rec_relocs[_ri].sym_index == target_idx
                                 && rec_relocs[_ri].addend == target_addend
                                 && rec_relocs[_ri].dest_sym_index == dest_local_idx
                                 && rec_relocs[_ri].width == slot_width
                                 && rec_relocs[_ri].is_text == 0)
                             {
                                 dup = true;
                                 break;
                             }
                         }
                         if (!dup)
                         {
                             if (rec_reloc_count + 1 > rec_reloc_cap)
                             {
                                 long newcap = rec_reloc_cap ? rec_reloc_cap * 2 : 16;
                                 rec_relocs = (struct recorded_reloc *) bfd_realloc (rec_relocs, newcap * sizeof (*rec_relocs));
                                 rec_reloc_cap = newcap;
                             }
                             rec_relocs[rec_reloc_count].addr = target_addr;
                             rec_relocs[rec_reloc_count].sym_index = target_idx;
                             rec_relocs[rec_reloc_count].dest_sym_index = dest_local_idx;
                             rec_relocs[rec_reloc_count].width = slot_width;
                             rec_relocs[rec_reloc_count].addend = target_addend;
                             rec_relocs[rec_reloc_count].is_text = 0;
                             rec_reloc_count++;
                             PLAN9OBJ_DBG ("[plan9obj][ADATA] pos=0x%08x recorded data-reloc addr=0x%x symidx=%d addend=%lld\n",
                                           opcode_pos, target_addr, target_idx, (long long) target_addend);
                             plan9obj_emit_event (2, opcode_pos, target_idx, "ADATA", "recorded-reloc", NULL, NULL);
                         }
                         else
                         {
                             PLAN9OBJ_DBG ("[plan9obj][ADATA] pos=0x%08x duplicate data-reloc skipped addr=0x%x symidx=%d addend=%lld\n",
                                           opcode_pos, target_addr, target_idx, (long long) target_addend);
                             plan9obj_emit_event (2, opcode_pos, target_idx, "ADATA", "duplicate-ignored", NULL, NULL);
                         }
                     }

                     continue;
                 }

                 if (opcode == ATEXT) {
                     int target_idx = -1;
                     if (addr_to.symbol)
                         target_idx = addr_to.sym_index;
                     else if (addr_from3.symbol)
                         target_idx = addr_from3.sym_index;
                     else if (addr_from.symbol)
                         target_idx = addr_from.sym_index;

                     if (target_idx >= 0) {
                         bool accept = false;

                         if (addr_from.type == D_BRANCH || addr_from.type == D_CONST || addr_from.type == D_DCONST) {
                             if (addr_from.offset >= 0 && (addr_from.offset % 4) == 0)
                                 accept = true;
                         }

                         if (accept) {
                             asymbol *ts = NULL;
                             if (target_idx >= 0 && target_idx < NSYM)
                                 ts = local_syms[target_idx];
                             if (!ts || !ts->name) {
                                 accept = false;
                                 plan9obj_emit_event (2, opcode_pos, target_idx, "ATEXT", "filter-no-sym", NULL, NULL);
                             } else {
                                 const char *nm = ts->name;
                                 if (nm[0] == '<' || strchr (nm, '/') || (nm[0] == '.' && nm[1] == '/')) {
                                     accept = false;
                                     plan9obj_emit_event (2, opcode_pos, target_idx, "ATEXT", "filter-path-like", nm, NULL);
                                 }
                             }
                         } else {
                             plan9obj_emit_event (2, opcode_pos, target_idx, "ATEXT", "filter-addr", NULL, NULL);
                         }

                         if (accept) {
                             bool dup = false;
                             for (long _ri = 0; _ri < rec_reloc_count; ++_ri) {
                                 if (rec_relocs[_ri].addr == (unsigned int) addr_from.offset
                                     && rec_relocs[_ri].sym_index == target_idx
                                     && rec_relocs[_ri].width == 4
                                     && rec_relocs[_ri].is_text == 1) {
                                     dup = true;
                                     break;
                                 }
                             }
                             if (!dup) {
                                 if (rec_reloc_count + 1 > rec_reloc_cap) {
                                     long newcap = rec_reloc_cap ? rec_reloc_cap * 2 : 16;
                                     rec_relocs = (struct recorded_reloc *) bfd_realloc (rec_relocs, newcap * sizeof (*rec_relocs));
                                     rec_reloc_cap = newcap;
                                 }
                                 rec_relocs[rec_reloc_count].addr = (unsigned int) addr_from.offset;
                                 rec_relocs[rec_reloc_count].sym_index = target_idx;
                                 rec_relocs[rec_reloc_count].dest_sym_index = -1;
                                 rec_relocs[rec_reloc_count].width = 4;
                                 rec_relocs[rec_reloc_count].addend = 0;
                                 rec_relocs[rec_reloc_count].is_text = 1;
                                 rec_reloc_count++;
                                 PLAN9OBJ_DBG ("[plan9obj][ATEXT] pos=0x%08x recorded text-reloc addr=0x%x symidx=%d\n",
                                               opcode_pos, (unsigned int) addr_from.offset, target_idx);
                                 plan9obj_emit_event (2, opcode_pos, target_idx, "ATEXT", "recorded-reloc", NULL, NULL);
                             } else {
                                 PLAN9OBJ_DBG ("[plan9obj][ATEXT] pos=0x%08x duplicate text-reloc skipped addr=0x%x symidx=%d\n",
                                               opcode_pos, (unsigned int) addr_from.offset, target_idx);
                                 plan9obj_emit_event (2, opcode_pos, target_idx, "ATEXT", "duplicate-ignored", NULL, NULL);
                             }
                         }
                     }

                     continue;
                 }
             }
         }
         /* At end-of-iteration: centralize clearing of the name-prefix flag
            for pass2 as well. If we parsed a name we 'continue' earlier and
            the flag remains set; otherwise we clear it here exactly once. */
         if (in_prefix) in_prefix = false;
         /* PASS2 prefix clearing handled here. */
         if (in_name_prefix2) in_name_prefix2 = false;
            /* After finishing the pass2 scan, convert recorded relocation entries
            into real arelent structures and materialize the exported symbol
            vector so canonicalize_* functions can access them. */
        /* Build symbol vector and mapping from original ANAME index -> symbol vector index. */
        int sym_map[NSYM];
        memset (sym_map, -1, sizeof (sym_map));
        /* Count actual symbols found */
        unsigned int sym_count = 0;
        for (unsigned int i = 0; i < NSYM; ++i) {
            if (local_syms[i]) sym_count++;
        }
        struct plan9_obj_tdata *t = plan9_obj_tdata (abfd);
        if (t) {
            t->symbol_count = 0;
            t->symbols = NULL;
        }
        if (sym_count && t) {
            t->symbol_count = sym_count;
            t->symbols = (asymbol **) bfd_zalloc (abfd, (sym_count + 1) * sizeof (asymbol *));
            unsigned int dst = 0;
            for (unsigned int i = 0; i < NSYM; ++i) {
                if (!local_syms[i]) continue;
                sym_map[i] = dst;
                /* Assign a reasonable default section for the symbol.  We will
                   overwrite this below if a relocation indicates the real use.
                   Use .bss initially so that unreferenced locals do not appear
                   as functions. */
                local_syms[i]->section = t->bss_section;
                t->symbols[dst++] = local_syms[i];
            }
            t->symbols[dst] = NULL;

            /* Allocate final reloc arrays sized to recorded counts (grow-on-demand
               remains supported but we can pre-reserve). */
            if (rec_reloc_count) {
                long data_count = 0, text_count = 0;
                for (long ri = 0; ri < rec_reloc_count; ++ri) {
                    if (rec_relocs[ri].is_text) text_count++; else data_count++;
                }
                /* Materialize data relocs (ABS64). */
                if (data_count) {
                    t->data_relocs = (arelent **) bfd_zalloc (abfd, data_count * sizeof (arelent *));
                    t->data_reloc_count = 0;
                    for (long ri = 0; ri < rec_reloc_count; ++ri) {
                        struct recorded_reloc *r = &rec_relocs[ri];
                        if (r->is_text) continue;
                        int mapped = (r->sym_index >= 0 && r->sym_index < NSYM) ? sym_map[r->sym_index] : -1;
                        if (mapped < 0) continue;
                        arelent *rel = (arelent *) bfd_zalloc (abfd, sizeof (arelent));
                        rel->address = (bfd_vma) r->addr;
                        rel->addend = (bfd_signed_vma) r->addend;
                        rel->howto = bfd_reloc_type_lookup (abfd, BFD_RELOC_64);
                        rel->sym_ptr_ptr = &t->symbols[mapped];
                        t->data_relocs[t->data_reloc_count++] = rel;
                        t->symbols[mapped]->section = t->data_section;
                        PLAN9OBJ_DBG ("[plan9obj][finalize] created data rel addr=0x%x -> symvec[%d] name='%s' addend=%lld\n",
                                     r->addr, mapped, t->symbols[mapped]->name ? t->symbols[mapped]->name : "(null)", (long long) r->addend);
                    }
                    bfd_size_type maxoff = 0;
                    for (long ri = 0; ri < rec_reloc_count; ++ri) {
                        if (!rec_relocs[ri].is_text) {
                            unsigned int span = rec_relocs[ri].width ? rec_relocs[ri].width : 8;
                            if (rec_relocs[ri].addr + span > maxoff)
                                maxoff = rec_relocs[ri].addr + span;
                        }
                    }
                    t->data_size = maxoff;
                    if (t->data_section) bfd_set_section_size (t->data_section, t->data_size);
                }

                /* Materialize text relocs (CALL26). */
                if (text_count) {
                    t->text_relocs = (arelent **) bfd_zalloc (abfd, text_count * sizeof (arelent *));
                    t->text_reloc_count = 0;
                    for (long ri = 0; ri < rec_reloc_count; ++ri) {
                        struct recorded_reloc *r = &rec_relocs[ri];
                        if (!r->is_text) continue;
                        int mapped = (r->sym_index >= 0 && r->sym_index < NSYM) ? sym_map[r->sym_index] : -1;
                        if (mapped < 0) continue;
                        arelent *rel = (arelent *) bfd_zalloc (abfd, sizeof (arelent));
                        rel->address = (bfd_vma) r->addr;
                        rel->addend = (bfd_signed_vma) r->addend;
                        rel->howto = bfd_reloc_type_lookup (abfd, BFD_RELOC_AARCH64_CALL26);
                        rel->sym_ptr_ptr = &t->symbols[mapped];
                        t->text_relocs[t->text_reloc_count++] = rel;
                        t->symbols[mapped]->section = t->text_section;
                        PLAN9OBJ_DBG ("[plan9obj][finalize] created text rel addr=0x%x -> symvec[%d] name='%s'\n",
                                     r->addr, mapped, t->symbols[mapped]->name ? t->symbols[mapped]->name : "(null)");
                    }
                    bfd_size_type maxoff = 0;
                    for (long ri = 0; ri < rec_reloc_count; ++ri) {
                        if (rec_relocs[ri].is_text) {
                            unsigned int span = rec_relocs[ri].width ? rec_relocs[ri].width : 4;
                            if (rec_relocs[ri].addr + span > maxoff)
                                maxoff = rec_relocs[ri].addr + span;
                        }
                    }
                    t->text_size = maxoff;
                    if (t->text_section) bfd_set_section_size (t->text_section, t->text_size);
                }
            }
        }
        if (t && data_extent > t->data_size) {
            t->data_size = data_extent;
            if (t->data_section)
                bfd_set_section_size (t->data_section, t->data_size);
        }
        /* Report how many relocation candidates we recorded in pass2. */
        if (rec_relocs) bfd_realloc (rec_relocs, 0); /* free */
     }

    free (data);
    return true;
}

/* Stub implementations for required BFD functions */
bool
plan9obj_write_object_contents (bfd *abfd ATTRIBUTE_UNUSED)
{
    /* TODO: Implement Plan 9 object file writing */
    return false;
}

long
plan9obj_get_symtab_upper_bound (bfd *abfd)
{
    struct plan9_obj_tdata *tdata = plan9_obj_tdata (abfd);
    if (!tdata || tdata->symbol_count == 0)
        return 0;
    return (tdata->symbol_count + 1) * sizeof (asymbol *);
}

long
plan9obj_canonicalize_symtab (bfd *abfd, asymbol **syms)
{
    struct plan9_obj_tdata *tdata = plan9_obj_tdata (abfd);
    unsigned int i;
    
    if (!tdata || !tdata->symbols)
        return 0;
    
    for (i = 0; i < tdata->symbol_count; i++) {
        syms[i] = tdata->symbols[i];
    }
    syms[i] = NULL;
    
    return tdata->symbol_count;
}

asymbol *
plan9obj_make_empty_symbol (bfd *abfd)
{
    size_t amt = sizeof (asymbol);
    asymbol *sym = (asymbol *) bfd_zalloc (abfd, amt);
    if (sym)
        sym->the_bfd = abfd;
    return sym;
}

void
plan9obj_get_symbol_info (bfd *abfd ATTRIBUTE_UNUSED, asymbol *symbol,
                      symbol_info *ret)
{
    bfd_symbol_info (symbol, ret);
}

void
plan9obj_print_symbol (bfd *abfd, void *filep, asymbol *symbol, bfd_print_symbol_type how)
{
    FILE *file = (FILE*)filep;
    switch (how) {
        case bfd_print_symbol_name:
            fprintf (file, "%s", symbol->name);
            break;
        default:
            bfd_print_symbol_vandf (abfd, filep, symbol);
            fprintf (file, " %s", symbol->name ? symbol->name : "");
            break;
    }
}

/* Relocation jump table functions */
long
plan9obj_get_reloc_upper_bound (bfd *abfd, asection *sec)
{
    struct plan9_obj_tdata *t = plan9_obj_tdata (abfd);
    if (!t || !sec) return 0;
    if (sec == t->text_section)
        return (t->text_reloc_count + 1) * (long) sizeof (arelent *);
    if (sec == t->data_section)
        return (t->data_reloc_count + 1) * (long) sizeof (arelent *);
    return 0;
}

long
plan9obj_canonicalize_reloc (bfd *abfd, asection *sec, arelent **relpp, asymbol **symbols ATTRIBUTE_UNUSED)
{
    struct plan9_obj_tdata *t = plan9_obj_tdata (abfd);
    long i;
    if (!t || !sec || !relpp) return 0;
    if (sec == t->text_section) {
        for (i = 0; i < t->text_reloc_count; i++)
            relpp[i] = t->text_relocs[i];
        relpp[i] = NULL;
        return t->text_reloc_count;
    }
    if (sec == t->data_section) {
        for (i = 0; i < t->data_reloc_count; i++)
            relpp[i] = t->data_relocs[i];
        relpp[i] = NULL;
        return t->data_reloc_count;
    }
    return 0;
}

static reloc_howto_type *
plan9obj_aarch64_howto_from_code (bfd_reloc_code_real_type code)
{
    switch (code) {
        case BFD_RELOC_64: return &plan9obj_aarch64_abs64_howto;
        case BFD_RELOC_AARCH64_CALL26: return &plan9obj_aarch64_call26_howto;
        default: return NULL;
    }
}

reloc_howto_type *
plan9obj_bfd_reloc_type_lookup (bfd *abfd ATTRIBUTE_UNUSED, bfd_reloc_code_real_type code)
{
    return plan9obj_aarch64_howto_from_code (code);
}

reloc_howto_type *
plan9obj_bfd_reloc_name_lookup (bfd *abfd ATTRIBUTE_UNUSED, const char *name)
{
    if (!name) return NULL;
    if (strcmp (name, "ABS64") == 0) return &plan9obj_aarch64_abs64_howto;
    if (strcmp (name, "CALL26") == 0 || strcmp (name, "AARCH64_CALL26") == 0)
        return &plan9obj_aarch64_call26_howto;
    return NULL;
}
