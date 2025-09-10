/* Stub implementation of sframe functions to allow building without libsframe */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

/* Stub types */
typedef void* sframe_decoder_ctx;
typedef void* sframe_encoder_ctx;

/* Decoder stubs */
void* sframe_decode(void* data, size_t size, int* errp) {
    if (errp) *errp = 0;
    return NULL;
}

void sframe_decoder_free(void* ctx) {
    /* no-op */
}

int sframe_decoder_get_abi_arch(void* ctx) {
    return 0;
}

int sframe_decoder_get_fixed_fp_offset(void* ctx) {
    return 0;
}

int sframe_decoder_get_fixed_ra_offset(void* ctx) {
    return 0;
}

int sframe_decoder_get_flags(void* ctx) {
    return 0;
}

void* sframe_decoder_get_fre(void* ctx, int idx) {
    return NULL;
}

void* sframe_decoder_get_funcdesc_v2(void* ctx, int idx) {
    return NULL;
}

int sframe_decoder_get_hdr_size(void* ctx) {
    return 0;
}

int sframe_decoder_get_num_fidx(void* ctx) {
    return 0;
}

int sframe_decoder_get_offsetof_fde_start_addr(void* ctx) {
    return 0;
}

int sframe_decoder_get_version(void* ctx) {
    return 1;
}

/* Encoder stubs */
void* sframe_encode(void* ctx) {
    return NULL;
}

void sframe_encoder_free(void* ctx) {
    /* no-op */
}

int sframe_encoder_get_abi_arch(void* ctx) {
    return 0;
}

int sframe_encoder_get_flags(void* ctx) {
    return 0;
}

int sframe_encoder_get_num_fidx(void* ctx) {
    return 0;
}

int sframe_encoder_get_offsetof_fde_start_addr(void* ctx) {
    return 0;
}

int sframe_encoder_get_version(void* ctx) {
    return 1;
}

int sframe_encoder_add_fre(void* ctx, void* fre) {
    return 0;
}

int sframe_encoder_add_funcdesc_v2(void* ctx, void* funcdesc) {
    return 0;
}

int sframe_encoder_write(void* ctx, void* buffer, size_t size) {
    return 0;
}

/* Error message stub */
const char* sframe_errmsg(int err) {
    return "sframe not supported";
}

/* Dump stub */
void dump_sframe(void* ctx, void* file) {
    /* no-op */
}