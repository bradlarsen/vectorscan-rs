/*
 * Copyright (c) 2015-2020, Intel Corporation
 * Copyright (c) 2024, VectorCamp PC
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of Intel Corporation nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/** \file
 * \brief Teddy literal matcher: SSSE3 engine runtime.
 */

#include "fdr_internal.h"
#include "flood_runtime.h"
#include "teddy.h"
#include "teddy_internal.h"
#include "teddy_runtime_common.h"
#include "util/arch.h"
#include "util/simd_utils.h"


#ifdef ARCH_64_BIT
static really_inline
hwlm_error_t conf_chunk_64(u64a chunk, u8 bucket, u8 offset,
                           CautionReason reason, const u8 *pt,
                           const u32* confBase,
                           const struct FDR_Runtime_Args *a,
                           hwlm_group_t *control,
                           u32 *last_match) {
    if (unlikely(chunk != ones_u64a)) {
        chunk = ~chunk;
        do_confWithBit_teddy(&chunk, bucket, offset, confBase, reason, a, pt,
                control, last_match);
        // adapted from CHECK_HWLM_TERMINATE_MATCHING
        if (unlikely(*control == HWLM_TERMINATE_MATCHING)) {
            return HWLM_TERMINATED;
        }

    }
    return HWLM_SUCCESS;
}

#define CONF_CHUNK_64(chunk, bucket, off, reason, pt, confBase, a, control, last_match) \
 if(conf_chunk_64(chunk, bucket, off, reason, pt, confBase, a, control, last_match) == HWLM_TERMINATED)return HWLM_TERMINATED;

#else // 32/64

static really_inline
hwlm_error_t conf_chunk_32(u32 chunk, u8 bucket, u8 offset,
                           CautionReason reason, const u8 *pt,
                           const u32* confBase,
                           const struct FDR_Runtime_Args *a,
                           hwlm_group_t *control,
                           u32 *last_match) {
    if (unlikely(chunk != ones_u32)) {
        chunk = ~chunk;
        do_confWithBit_teddy(&chunk, bucket, offset, confBase, reason, a, pt,
                control, last_match);
        // adapted from CHECK_HWLM_TERMINATE_MATCHING
        if (unlikely(*control == HWLM_TERMINATE_MATCHING)) {
            return HWLM_TERMINATED;
        }
    }
    return HWLM_SUCCESS;
}

#define CONF_CHUNK_32(chunk, bucket, off, reason, pt, confBase, a, control, last_match) \
 if(conf_chunk_32(chunk, bucket, off, reason, pt, confBase, a, control, last_match) == HWLM_TERMINATED)return HWLM_TERMINATED;

#endif

#if defined(HAVE_AVX512VBMI) || defined(HAVE_AVX512) // common to both 512b's

static really_inline
const m512 *getDupMaskBase(const struct Teddy *teddy, u8 numMask) {
    return (const m512 *)((const u8 *)teddy + ROUNDUP_CL(sizeof(struct Teddy))
                          + ROUNDUP_CL(2 * numMask * sizeof(m256)));
}


#ifdef ARCH_64_BIT

static really_inline
hwlm_error_t confirm_teddy_64_512(m512 var, u8 bucket, u8 offset,
                                  CautionReason reason, const u8 *ptr,
                                  const struct FDR_Runtime_Args *a,
                                  const u32* confBase, hwlm_group_t *control,
                                  u32 *last_match) {
    if (unlikely(diff512(var, ones512()))) {
        m128 p128_0 = extract128from512(var, 0);
        m128 p128_1 = extract128from512(var, 1);
        m128 p128_2 = extract128from512(var, 2);
        m128 p128_3 = extract128from512(var, 3);
        u64a part1 = movq(p128_0);
        u64a part2 = movq(rshiftbyte_m128(p128_0, 8));
        u64a part3 = movq(p128_1);
        u64a part4 = movq(rshiftbyte_m128(p128_1, 8));
        u64a part5 = movq(p128_2);
        u64a part6 = movq(rshiftbyte_m128(p128_2, 8));
        u64a part7 = movq(p128_3);
        u64a part8 = movq(rshiftbyte_m128(p128_3, 8));
        CONF_CHUNK_64(part1, bucket, offset, reason, ptr, confBase, a, control, last_match);
        CONF_CHUNK_64(part2, bucket, offset + 8, reason, ptr, confBase, a, control, last_match);
        CONF_CHUNK_64(part3, bucket, offset + 16, reason, ptr, confBase, a, control, last_match);
        CONF_CHUNK_64(part4, bucket, offset + 24, reason, ptr, confBase, a, control, last_match);
        CONF_CHUNK_64(part5, bucket, offset + 32, reason, ptr, confBase, a, control, last_match);
        CONF_CHUNK_64(part6, bucket, offset + 40, reason, ptr, confBase, a, control, last_match);
        CONF_CHUNK_64(part7, bucket, offset + 48, reason, ptr, confBase, a, control, last_match);
        CONF_CHUNK_64(part8, bucket, offset + 56, reason, ptr, confBase, a, control, last_match);
    }
    return HWLM_SUCCESS;
}

#define confirm_teddy_512_f confirm_teddy_64_512

#else // 32/64

static really_inline
hwlm_error_t confirm_teddy_32_512(m512 var, u8 bucket, u8 offset,
                                  CautionReason reason, const u8 *ptr,
                                  const struct FDR_Runtime_Args *a,
                                  const u32* confBase, hwlm_group_t *control,
                                  u32 *last_match) {
    if (unlikely(diff512(var, ones512()))) {
        m128 p128_0 = extract128from512(var, 0);
        m128 p128_1 = extract128from512(var, 1);
        m128 p128_2 = extract128from512(var, 2);
        m128 p128_3 = extract128from512(var, 3);
        u32 part1 = movd(p128_0);
        u32 part2 = movd(rshiftbyte_m128(p128_0, 4));
        u32 part3 = movd(rshiftbyte_m128(p128_0, 8));
        u32 part4 = movd(rshiftbyte_m128(p128_0, 12));
        u32 part5 = movd(p128_1);
        u32 part6 = movd(rshiftbyte_m128(p128_1, 4));
        u32 part7 = movd(rshiftbyte_m128(p128_1, 8));
        u32 part8 = movd(rshiftbyte_m128(p128_1, 12));
        u32 part9 = movd(p128_2);
        u32 part10 = movd(rshiftbyte_m128(p128_2, 4));
        u32 part11 = movd(rshiftbyte_m128(p128_2, 8));
        u32 part12 = movd(rshiftbyte_m128(p128_2, 12));
        u32 part13 = movd(p128_3);
        u32 part14 = movd(rshiftbyte_m128(p128_3, 4));
        u32 part15 = movd(rshiftbyte_m128(p128_3, 8));
        u32 part16 = movd(rshiftbyte_m128(p128_3, 12));
        CONF_CHUNK_32(part1, bucket, offset, reason, ptr, confBase, a, control, last_match);
        CONF_CHUNK_32(part2, bucket, offset + 4, reason, ptr, confBase, a, control, last_match);
        CONF_CHUNK_32(part3, bucket, offset + 8, reason, ptr, confBase, a, control, last_match);
        CONF_CHUNK_32(part4, bucket, offset + 12, reason, ptr, confBase, a, control, last_match);
        CONF_CHUNK_32(part5, bucket, offset + 16, reason, ptr, confBase, a, control, last_match);
        CONF_CHUNK_32(part6, bucket, offset + 20, reason, ptr, confBase, a, control, last_match);
        CONF_CHUNK_32(part7, bucket, offset + 24, reason, ptr, confBase, a, control, last_match);
        CONF_CHUNK_32(part8, bucket, offset + 28, reason, ptr, confBase, a, control, last_match);
        CONF_CHUNK_32(part9, bucket, offset + 32, reason, ptr, confBase, a, control, last_match);
        CONF_CHUNK_32(part10, bucket, offset + 36, reason, ptr, confBase, a, control, last_match);
        CONF_CHUNK_32(part11, bucket, offset + 40, reason, ptr, confBase, a, control, last_match);
        CONF_CHUNK_32(part12, bucket, offset + 44, reason, ptr, confBase, a, control, last_match);
        CONF_CHUNK_32(part13, bucket, offset + 48, reason, ptr, confBase, a, control, last_match);
        CONF_CHUNK_32(part14, bucket, offset + 52, reason, ptr, confBase, a, control, last_match);
        CONF_CHUNK_32(part15, bucket, offset + 56, reason, ptr, confBase, a, control, last_match);
        CONF_CHUNK_32(part16, bucket, offset + 60, reason, ptr, confBase, a, control, last_match);
    }
    return HWLM_SUCCESS;
}

#define confirm_teddy_512_f confirm_teddy_32_512


#endif // 32/64

#define CONFIRM_TEDDY_512(...) if(confirm_teddy_512_f(__VA_ARGS__, a, confBase, &control, &last_match) == HWLM_TERMINATED)return HWLM_TERMINATED;

#endif // AVX512VBMI or AVX512


#if defined(HAVE_AVX512VBMI) // VBMI strong teddy

#define TEDDY_VBMI_SL1_MASK   0xfffffffffffffffeULL
#define TEDDY_VBMI_SL2_MASK   0xfffffffffffffffcULL
#define TEDDY_VBMI_SL3_MASK   0xfffffffffffffff8ULL

template<int NMSK>
static really_inline
m512 prep_conf_teddy_512vbmi_templ(const m512 *lo_mask, const m512 *dup_mask,
                                   const m512 *sl_msk, const m512 val) {
    m512 lo = and512(val, *lo_mask);
    m512 hi = and512(rshift64_m512(val, 4), *lo_mask);
    m512 shuf_or_b0 = or512(pshufb_m512(dup_mask[0], lo),
                            pshufb_m512(dup_mask[1], hi));

    if constexpr (NMSK == 1) return shuf_or_b0;
    m512 shuf_or_b1 = or512(pshufb_m512(dup_mask[2], lo),
                            pshufb_m512(dup_mask[3], hi));
    m512 sl1 = maskz_vpermb512(TEDDY_VBMI_SL1_MASK, sl_msk[0], shuf_or_b1);
    if constexpr (NMSK == 2) return (or512(sl1, shuf_or_b0));
    m512 shuf_or_b2 = or512(pshufb_m512(dup_mask[4], lo),
                            pshufb_m512(dup_mask[5], hi));
    m512 sl2 = maskz_vpermb512(TEDDY_VBMI_SL2_MASK, sl_msk[1], shuf_or_b2);
    if constexpr (NMSK == 3) return (or512(sl2, or512(sl1, shuf_or_b0)));
    m512 shuf_or_b3 = or512(pshufb_m512(dup_mask[6], lo),
                            pshufb_m512(dup_mask[7], hi));
    m512 sl3 = maskz_vpermb512(TEDDY_VBMI_SL3_MASK, sl_msk[2], shuf_or_b3);
    return (or512(sl3, or512(sl2, or512(sl1, shuf_or_b0))));
}


#define TEDDY_VBMI_SL1_POS    15
#define TEDDY_VBMI_SL2_POS    14
#define TEDDY_VBMI_SL3_POS    13

#define TEDDY_VBMI_CONF_MASK_HEAD   (0xffffffffffffffffULL >> n_sh)
#define TEDDY_VBMI_CONF_MASK_FULL   (0xffffffffffffffffULL << n_sh)
#define TEDDY_VBMI_CONF_MASK_VAR(n) (0xffffffffffffffffULL >> (64 - n) << overlap)
#define TEDDY_VBMI_LOAD_MASK_PATCH  (0xffffffffffffffffULL >> (64 - n_sh))

template<int NMSK>
hwlm_error_t fdr_exec_teddy_512vbmi_templ(const struct FDR *fdr,
                                      const struct FDR_Runtime_Args *a,
                                      hwlm_group_t control) {
    const u8 *buf_end = a->buf + a->len;
    const u8 *ptr = a->buf + a->start_offset;
    u32 floodBackoff = FLOOD_BACKOFF_START;
    const u8 *tryFloodDetect = a->firstFloodDetect;
    u32 last_match = ones_u32;
    const struct Teddy *teddy = (const struct Teddy *)fdr;
    const size_t iterBytes = 64;
    u32 n_sh = NMSK - 1;
    const size_t loopBytes = 64 - n_sh;
    DEBUG_PRINTF("params: buf %p len %zu start_offset %zu\n",
                 a->buf, a->len, a->start_offset);

    const m128 *maskBase = getMaskBase(teddy);

    m512 lo_mask = set1_64x8(0xf);
    m512 dup_mask[NMSK * 2];
    m512 sl_msk[NMSK - 1];
    dup_mask[0] = set1_4x128(maskBase[0]);
    dup_mask[1] = set1_4x128(maskBase[1]);
    if constexpr (NMSK > 1){
    dup_mask[2] = set1_4x128(maskBase[2]);
    dup_mask[3] = set1_4x128(maskBase[3]);
    sl_msk[0] = loadu512(p_sh_mask_arr + TEDDY_VBMI_SL1_POS);
    }
    if constexpr (NMSK > 2){
    dup_mask[4] = set1_4x128(maskBase[4]);
    dup_mask[5] = set1_4x128(maskBase[5]);
    sl_msk[1] = loadu512(p_sh_mask_arr + TEDDY_VBMI_SL2_POS);
    }
    if constexpr (NMSK > 3){
    dup_mask[6] = set1_4x128(maskBase[6]);
    dup_mask[7] = set1_4x128(maskBase[7]);
    sl_msk[2] = loadu512(p_sh_mask_arr + TEDDY_VBMI_SL3_POS);
    }
    const u32 *confBase = getConfBase(teddy);

    u64a k = TEDDY_VBMI_CONF_MASK_FULL;
    m512 p_mask = set_mask_m512(~k);
    u32 overlap = 0;
    u64a patch = 0;
    if (likely(ptr + loopBytes <= buf_end)) {
        m512 p_mask0 = set_mask_m512(~TEDDY_VBMI_CONF_MASK_HEAD);
        m512 r_0 = prep_conf_teddy_512vbmi_templ<NMSK>(&lo_mask, dup_mask, sl_msk, loadu512(ptr));
        r_0 = or512(r_0, p_mask0);
        CONFIRM_TEDDY_512(r_0, 8, 0, VECTORING, ptr);
        ptr += loopBytes;
        overlap = n_sh;
        patch = TEDDY_VBMI_LOAD_MASK_PATCH;
    }

    for (; ptr + loopBytes <= buf_end; ptr += loopBytes) {
        __builtin_prefetch(ptr - n_sh + (64 * 2));
        CHECK_FLOOD;
        m512 r_0 = prep_conf_teddy_512vbmi_templ<NMSK>(&lo_mask, dup_mask, sl_msk, loadu512(ptr - n_sh));
        r_0 = or512(r_0, p_mask);
        CONFIRM_TEDDY_512(r_0, 8, 0, NOT_CAUTIOUS, ptr - n_sh);
    }

    assert(ptr + loopBytes > buf_end);
    if (ptr < buf_end) {
        u32 left = (u32)(buf_end - ptr);
        u64a k1 = TEDDY_VBMI_CONF_MASK_VAR(left);
        m512 p_mask1 = set_mask_m512(~k1);
        m512 val_0 = loadu_maskz_m512(k1 | patch, ptr - overlap);
        m512 r_0 = prep_conf_teddy_512vbmi_templ<NMSK>(&lo_mask, dup_mask, sl_msk, val_0);
        r_0 = or512(r_0, p_mask1);
        CONFIRM_TEDDY_512(r_0, 8, 0, VECTORING, ptr - overlap);
    }

    return HWLM_SUCCESS;
}

#define FDR_EXEC_TEDDY_FN fdr_exec_teddy_512vbmi_templ

#elif defined(HAVE_AVX512) // AVX512 reinforced teddy

/* both 512b versions use the same confirm teddy */

template <int NMSK>
static inline
m512 shift_or_512_templ(const m512 *dup_mask, m512 lo, m512 hi) {
    return or512(lshift128_m512(or512(pshufb_m512(dup_mask[(NMSK - 1) * 2], lo),
                                pshufb_m512(dup_mask[(NMSK * 2) - 1], hi)),
                                NMSK - 1), shift_or_512_templ<NMSK - 1>(dup_mask, lo, hi));
}

template <>
m512 shift_or_512_templ<1>(const m512 *dup_mask, m512 lo, m512 hi){
    return or512(pshufb_m512(dup_mask[0], lo), pshufb_m512(dup_mask[1], hi));
}

template <int NMSK>
static really_inline
m512 prep_conf_teddy_no_reinforcement_512_templ(const m512 *lo_mask,
                                                const m512 *dup_mask,
                                                const m512 val) {
    m512 lo = and512(val, *lo_mask);
    m512 hi = and512(rshift64_m512(val, 4), *lo_mask);
    return shift_or_512_templ<NMSK>(dup_mask, lo, hi);
}

template <int NMSK>
static really_inline
m512 prep_conf_teddy_512_templ(const m512 *lo_mask, const m512 *dup_mask,
                               const u8 *ptr, const u64a *r_msk_base,
                               u32 *c_0, u32 *c_16, u32 *c_32, u32 *c_48) {
    m512 lo = and512(load512(ptr), *lo_mask);
    m512 hi = and512(rshift64_m512(load512(ptr), 4), *lo_mask);
    *c_16 = *(ptr + 15);
    *c_32 = *(ptr + 31);
    *c_48 = *(ptr + 47);
    m512 r_msk = set8x64(0ULL, r_msk_base[*c_48], 0ULL, r_msk_base[*c_32],
                           0ULL, r_msk_base[*c_16], 0ULL, r_msk_base[*c_0]);
    *c_0 = *(ptr + 63);
    return or512(shift_or_512_templ<NMSK>(dup_mask, lo, hi), r_msk);
}


#define PREP_CONF_FN_512(ptr, n)                                                  \
    prep_conf_teddy_512_templ<n>(&lo_mask, dup_mask, ptr, r_msk_base,             \
                         &c_0, &c_16, &c_32, &c_48)

template <int NMSK>
hwlm_error_t fdr_exec_teddy_512_templ(const struct FDR *fdr,
                                      const struct FDR_Runtime_Args *a,
                                      hwlm_group_t control) {
    const u8 *buf_end = a->buf + a->len;
    const u8 *ptr = a->buf + a->start_offset;
    u32 floodBackoff = FLOOD_BACKOFF_START;
    const u8 *tryFloodDetect = a->firstFloodDetect;
    u32 last_match = ones_u32;
    const struct Teddy *teddy = (const struct Teddy *)fdr;
    const size_t iterBytes = 128;
    DEBUG_PRINTF("params: buf %p len %zu start_offset %zu\n",
                 a->buf, a->len, a->start_offset);

    const m128 *maskBase = getMaskBase(teddy);

    m512 lo_mask = set1_64x8(0xf);
    m512 dup_mask[NMSK * 2];

    dup_mask[0] = set1_4x128(maskBase[0]);
    dup_mask[1] = set1_4x128(maskBase[1]);
    if constexpr (NMSK > 1){
    dup_mask[2] = set1_4x128(maskBase[2]);
    dup_mask[3] = set1_4x128(maskBase[3]);
    }
    if constexpr (NMSK > 2){
    dup_mask[4] = set1_4x128(maskBase[4]);
    dup_mask[5] = set1_4x128(maskBase[5]);
    }
    if constexpr (NMSK > 3){
    dup_mask[6] = set1_4x128(maskBase[6]);
    dup_mask[7] = set1_4x128(maskBase[7]);
    }
    const u32 *confBase = getConfBase(teddy);

    const u64a *r_msk_base = getReinforcedMaskBase(teddy, NMSK);
    u32 c_0 = 0x100;
    u32 c_16 = 0x100;
    u32 c_32 = 0x100;
    u32 c_48 = 0x100;
    const u8 *mainStart = ROUNDUP_PTR(ptr, 64);
    DEBUG_PRINTF("derive: ptr: %p mainstart %p\n", ptr, mainStart);
    if (ptr < mainStart) {
        ptr = mainStart - 64;
        m512 p_mask;
        m512 val_0 = vectoredLoad512(&p_mask, ptr, a->start_offset,
                                     a->buf, buf_end,
                                     a->buf_history, a->len_history, NMSK);
        m512 r_0 = prep_conf_teddy_no_reinforcement_512_templ<NMSK>(&lo_mask, dup_mask, val_0);
        r_0 = or512(r_0, p_mask);
        CONFIRM_TEDDY_512(r_0, 8, 0, VECTORING, ptr);
        ptr += 64;
    }

    if (ptr + 64 <= buf_end) {
        m512 r_0 = PREP_CONF_FN_512(ptr, NMSK);
        CONFIRM_TEDDY_512(r_0, 8, 0, VECTORING, ptr);
        ptr += 64;
    }

    for (; ptr + iterBytes <= buf_end; ptr += iterBytes) {
        __builtin_prefetch(ptr + (iterBytes * 4));
        CHECK_FLOOD;
        m512 r_0 = PREP_CONF_FN_512(ptr, NMSK);
        CONFIRM_TEDDY_512(r_0, 8, 0, NOT_CAUTIOUS, ptr);
        m512 r_1 = PREP_CONF_FN_512(ptr + 64, NMSK);
        CONFIRM_TEDDY_512(r_1, 8, 64, NOT_CAUTIOUS, ptr);
    }

    if (ptr + 64 <= buf_end) {
        m512 r_0 = PREP_CONF_FN_512(ptr, NMSK);
        CONFIRM_TEDDY_512(r_0, 8, 0, NOT_CAUTIOUS, ptr);
        ptr += 64;
    }

    assert(ptr + 64 > buf_end);
    if (ptr < buf_end) {
        m512 p_mask;
        m512 val_0 = vectoredLoad512(&p_mask, ptr, 0, ptr, buf_end,
                                     a->buf_history, a->len_history, NMSK);
        m512 r_0 = prep_conf_teddy_no_reinforcement_512_templ<NMSK>(&lo_mask, dup_mask,val_0);
        r_0 = or512(r_0, p_mask);
        CONFIRM_TEDDY_512(r_0, 8, 0, VECTORING, ptr);
    }

    return HWLM_SUCCESS;
}


#define FDR_EXEC_TEDDY_FN fdr_exec_teddy_512_templ

/* #endif // AVX512 vs AVX512VBMI * back to the original fully exclusive logic */

#elif defined(HAVE_AVX2) // not HAVE_AVX512 but HAVE_AVX2 reinforced teddy

#ifdef ARCH_64_BIT

hwlm_error_t confirm_teddy_64_256(m256 var, u8 bucket, u8 offset,
                                  CautionReason reason, const u8 *ptr,
                                  const struct FDR_Runtime_Args *a,
                                  const u32* confBase, hwlm_group_t *control,
                                  u32 *last_match) {
    if (unlikely(diff256(var, ones256()))) {
        m128 lo = movdq_lo(var);
        m128 hi = movdq_hi(var);
        u64a part1 = movq(lo);
        u64a part2 = movq(rshiftbyte_m128(lo, 8));
        u64a part3 = movq(hi);
        u64a part4 = movq(rshiftbyte_m128(hi, 8));
        CONF_CHUNK_64(part1, bucket, offset, reason, ptr, confBase, a, control, last_match);
        CONF_CHUNK_64(part2, bucket, offset + 8, reason, ptr, confBase, a, control, last_match);
        CONF_CHUNK_64(part3, bucket, offset + 16, reason, ptr, confBase, a, control, last_match);
        CONF_CHUNK_64(part4, bucket, offset + 24, reason, ptr, confBase, a, control, last_match);
    }
    return HWLM_SUCCESS;
}

#define confirm_teddy_256_f confirm_teddy_64_256

#else

hwlm_error_t confirm_teddy_32_256(m256 var, u8 bucket, u8 offset,
                                  CautionReason reason, const u8 *ptr,
                                  const struct FDR_Runtime_Args *a,
                                  const u32* confBase, hwlm_group_t *control,
                                  u32 *last_match) {
    if (unlikely(diff256(var, ones256()))) {
        m128 lo = movdq_lo(var);
        m128 hi = movdq_hi(var);
        u32 part1 = movd(lo);
        u32 part2 = movd(rshiftbyte_m128(lo, 4));
        u32 part3 = movd(rshiftbyte_m128(lo, 8));
        u32 part4 = movd(rshiftbyte_m128(lo, 12));
        u32 part5 = movd(hi);
        u32 part6 = movd(rshiftbyte_m128(hi, 4));
        u32 part7 = movd(rshiftbyte_m128(hi, 8));
        u32 part8 = movd(rshiftbyte_m128(hi, 12));
        CONF_CHUNK_32(part1, bucket, offset, reason, ptr, confBase, a, control, last_match);
        CONF_CHUNK_32(part2, bucket, offset + 4, reason, ptr, confBase, a, control, last_match);
        CONF_CHUNK_32(part3, bucket, offset + 8, reason, ptr, confBase, a, control, last_match);
        CONF_CHUNK_32(part4, bucket, offset + 12, reason, ptr, confBase, a, control, last_match);
        CONF_CHUNK_32(part5, bucket, offset + 16, reason, ptr, confBase, a, control, last_match);
        CONF_CHUNK_32(part6, bucket, offset + 20, reason, ptr, confBase, a, control, last_match);
        CONF_CHUNK_32(part7, bucket, offset + 24, reason, ptr, confBase, a, control, last_match);
        CONF_CHUNK_32(part8, bucket, offset + 28, reason, ptr, confBase, a, control, last_match);
    }
    return HWLM_SUCCESS;
}

#define confirm_teddy_256_f confirm_teddy_32_256

#endif

#define CONFIRM_TEDDY_256(...) if(confirm_teddy_256_f(__VA_ARGS__, a, confBase, &control, &last_match) == HWLM_TERMINATED)return HWLM_TERMINATED;

/*
static really_inline
m256 vectoredLoad2x128(m256 *p_mask, const u8 *ptr, const size_t start_offset,
                       const u8 *lo, const u8 *hi,
                       const u8 *buf_history, size_t len_history,
                       const u32 nMasks) {
    m128 p_mask128;
    m256 ret = set1_2x128(vectoredLoad128(&p_mask128, ptr, start_offset, lo, hi,
                                          buf_history, len_history, nMasks));
    *p_mask = set1_2x128(p_mask128);
    return ret;
}
*/

template <int NMSK>
static inline
m256 shift_or_256_templ(const m256 *dup_mask, m256 lo, m256 hi){
    return or256(lshift128_m256(or256(pshufb_m256(dup_mask[(NMSK-1)*2], lo),
                                pshufb_m256(dup_mask[(NMSK*2)-1], hi)),
                                (NMSK-1)), shift_or_256_templ<NMSK-1>(dup_mask, lo, hi));
}

template<>
m256 shift_or_256_templ<1>(const m256 *dup_mask, m256 lo, m256 hi){
    return or256(pshufb_m256(dup_mask[0], lo), pshufb_m256(dup_mask[1], hi));
}

template <int NMSK>
static really_inline
m256 prep_conf_teddy_no_reinforcement_256_templ(const m256 *lo_mask,
                                         const m256 *dup_mask,
                                         const m256 val) {
    m256 lo = and256(val, *lo_mask);
    m256 hi = and256(rshift64_m256(val, 4), *lo_mask);
    return shift_or_256_templ<NMSK>(dup_mask, lo, hi);
}

template <int NMSK>
static really_inline
m256 prep_conf_teddy_256_templ(const m256 *lo_mask, const m256 *dup_mask,
                        const u8 *ptr, const u64a *r_msk_base,
                        u32 *c_0, u32 *c_128) {
    m256 lo = and256(load256(ptr), *lo_mask);
    m256 hi = and256(rshift64_m256(load256(ptr), 4), *lo_mask);
    *c_128 = *(ptr + 15);
    m256 r_msk = set4x64(0ULL, r_msk_base[*c_128], 0ULL, r_msk_base[*c_0]);
    *c_0 = *(ptr + 31);
    return or256(shift_or_256_templ<NMSK>(dup_mask, lo, hi), r_msk);
}

#define PREP_CONF_FN_256_NO_REINFORCEMENT(val, n)                                 \
    prep_conf_teddy_no_reinforcement_256_templ<n>(&lo_mask, dup_mask, val)

#define PREP_CONF_FN_256(ptr, n)                                                  \
    prep_conf_teddy_256_templ<n>(&lo_mask, dup_mask, ptr, r_msk_base, &c_0, &c_128)

template <int NMSK>
hwlm_error_t fdr_exec_teddy_256_templ(const struct FDR *fdr,
                                  const struct FDR_Runtime_Args *a,
                                  hwlm_group_t control) {
    const u8 *buf_end = a->buf + a->len;
    const u8 *ptr = a->buf + a->start_offset;
    u32 floodBackoff = FLOOD_BACKOFF_START;
    const u8 *tryFloodDetect = a->firstFloodDetect;
    u32 last_match = ones_u32;
    const struct Teddy *teddy = (const struct Teddy *)fdr;
    const size_t iterBytes = 64;
    DEBUG_PRINTF("params: buf %p len %zu start_offset %zu\n",
                 a->buf, a->len, a->start_offset);

    const m128 *maskBase = getMaskBase(teddy);
    //PREPARE_MASKS_256;

    m256 lo_mask = set1_32x8(0xf);
    m256 dup_mask[NMSK * 2];
    dup_mask[0] = set1_2x128(maskBase[0]);
    dup_mask[1] = set1_2x128(maskBase[1]);
    if constexpr (NMSK > 1){
    dup_mask[2] = set1_2x128(maskBase[2]);
    dup_mask[3] = set1_2x128(maskBase[3]);
    }
    if constexpr (NMSK > 2){
    dup_mask[4] = set1_2x128(maskBase[4]);
    dup_mask[5] = set1_2x128(maskBase[5]);
    }
    if constexpr (NMSK > 3){
    dup_mask[6] = set1_2x128(maskBase[6]);
    dup_mask[7] = set1_2x128(maskBase[7]);
    }
    const u32 *confBase = getConfBase(teddy);

    const u64a *r_msk_base = getReinforcedMaskBase(teddy, NMSK);
    u32 c_0 = 0x100;
    u32 c_128 = 0x100;
    const u8 *mainStart = ROUNDUP_PTR(ptr, 32);
    DEBUG_PRINTF("derive: ptr: %p mainstart %p\n", ptr, mainStart);
    if (ptr < mainStart) {
        ptr = mainStart - 32;
        m256 p_mask;
        m256 val_0 = vectoredLoad256(&p_mask, ptr, a->start_offset,
                                     a->buf, buf_end,
                                     a->buf_history, a->len_history, NMSK);
        m256 r_0 = PREP_CONF_FN_256_NO_REINFORCEMENT(val_0, NMSK);
        r_0 = or256(r_0, p_mask);
        CONFIRM_TEDDY_256(r_0, 8, 0, VECTORING, ptr);
        ptr += 32;
    }

    if (ptr + 32 <= buf_end) {
        m256 r_0 = PREP_CONF_FN_256(ptr, NMSK);
        CONFIRM_TEDDY_256(r_0, 8, 0, VECTORING, ptr);
        ptr += 32;
    }

    for (; ptr + iterBytes <= buf_end; ptr += iterBytes) {
        __builtin_prefetch(ptr + (iterBytes * 4));
        CHECK_FLOOD;
        m256 r_0 = PREP_CONF_FN_256(ptr, NMSK);
        CONFIRM_TEDDY_256(r_0, 8, 0, NOT_CAUTIOUS, ptr);
        m256 r_1 = PREP_CONF_FN_256(ptr + 32, NMSK);
        CONFIRM_TEDDY_256(r_1, 8, 32, NOT_CAUTIOUS, ptr);
    }

    if (ptr + 32 <= buf_end) {
        m256 r_0 = PREP_CONF_FN_256(ptr, NMSK);
        CONFIRM_TEDDY_256(r_0, 8, 0, NOT_CAUTIOUS, ptr);
        ptr += 32;
    }

    assert(ptr + 32 > buf_end);
    if (ptr < buf_end) {
        m256 p_mask;
        m256 val_0 = vectoredLoad256(&p_mask, ptr, 0, ptr, buf_end,
                                     a->buf_history, a->len_history, NMSK);
        m256 r_0 = PREP_CONF_FN_256_NO_REINFORCEMENT(val_0, NMSK);
        r_0 = or256(r_0, p_mask);
        CONFIRM_TEDDY_256(r_0, 8, 0, VECTORING, ptr);
    }

    return HWLM_SUCCESS;
}

#define FDR_EXEC_TEDDY_FN fdr_exec_teddy_256_templ

#else // not defined HAVE_AVX2

#ifdef ARCH_64_BIT
static really_inline
hwlm_error_t confirm_teddy_64_128(m128 var, u8 bucket, u8 offset,
                                  CautionReason reason, const u8 *ptr,
                                  const struct FDR_Runtime_Args *a,
                                  const u32* confBase, hwlm_group_t *control,
                                  u32 *last_match) {
    if (unlikely(diff128(var, ones128()))) {
        u64a lo = 0;
        u64a hi = 0;
        u64a __attribute__((aligned(16))) vec[2];
        store128(vec, var);
        lo = vec[0];
        hi = vec[1];
        CONF_CHUNK_64(lo, bucket, offset, reason, ptr, confBase, a, control, last_match);
        CONF_CHUNK_64(hi, bucket, offset + 8, reason, ptr, confBase, a, control, last_match);
    }
    return HWLM_SUCCESS;
}

#define confirm_teddy_128_f confirm_teddy_64_128

#else // 32/64

static really_inline
hwlm_error_t confirm_teddy_32_128(m128 var, u8 bucket, u8 offset,
                                  CautionReason reason, const u8 *ptr,
                                  const struct FDR_Runtime_Args *a,
                                  const u32* confBase, hwlm_group_t *control,
                                  u32 *last_match) {
    if (unlikely(diff128(var, ones128()))) {
        u32 part1 = movd(var);
        u32 part2 = movd(rshiftbyte_m128(var, 4));
        u32 part3 = movd(rshiftbyte_m128(var, 8));
        u32 part4 = movd(rshiftbyte_m128(var, 12));
        CONF_CHUNK_32(part1, bucket, offset, reason, ptr, confBase, a, control, last_match);
        CONF_CHUNK_32(part2, bucket, offset + 4, reason, ptr, confBase, a, control, last_match);
        CONF_CHUNK_32(part3, bucket, offset + 8, reason, ptr, confBase, a, control, last_match);
        CONF_CHUNK_32(part4, bucket, offset + 12, reason, ptr, confBase, a, control, last_match);
    }
    return HWLM_SUCCESS;
}
#define confirm_teddy_128_f confirm_teddy_32_128

#endif  // 32/64


#define CONFIRM_TEDDY_128(...) if(confirm_teddy_128_f(__VA_ARGS__, a, confBase, &control, &last_match) == HWLM_TERMINATED)return HWLM_TERMINATED;

template <int NMSK>
static really_inline
m128 prep_conf_teddy_128_templ(const m128 *maskBase, m128 val) {
    m128 mask = set1_16x8(0xf);
    m128 lo = and128(val, mask);
    m128 hi = and128(rshift64_m128(val, 4), mask);
    m128 r1 = or128(pshufb_m128(maskBase[0 * 2], lo),
                             pshufb_m128(maskBase[0 * 2 + 1], hi));
    if constexpr (NMSK == 1) return r1;
    m128 res_1 = or128(pshufb_m128(maskBase[1 * 2], lo),
                       pshufb_m128(maskBase[1 * 2 + 1], hi));

    m128 old_1 = zeroes128();
    m128 res_shifted_1 = palignr(res_1, old_1, 16 - 1);
    m128 r2 = or128(r1, res_shifted_1);
    if constexpr (NMSK == 2) return r2;
    m128 res_2 = or128(pshufb_m128(maskBase[2 * 2], lo),
                       pshufb_m128(maskBase[2 * 2 + 1], hi));
    m128 res_shifted_2 = palignr(res_2, old_1, 16 - 2);
    m128 r3 = or128(r2, res_shifted_2);
    if constexpr (NMSK == 3) return r3;
    m128 res_3 = or128(pshufb_m128(maskBase[3 * 2], lo),
                       pshufb_m128(maskBase[3 * 2 + 1], hi));
    m128 res_shifted_3 = palignr(res_3, old_1, 16 - 3);
    return or128(r3, res_shifted_3);
}

template <int NMSK>
hwlm_error_t fdr_exec_teddy_128_templ(const struct FDR *fdr,
                             const struct FDR_Runtime_Args *a,
                             hwlm_group_t control) {
    const u8 *buf_end = a->buf + a->len;
    const u8 *ptr = a->buf + a->start_offset;
    u32 floodBackoff = FLOOD_BACKOFF_START;
    const u8 *tryFloodDetect = a->firstFloodDetect;
    u32 last_match = ones_u32;
    const struct Teddy *teddy = reinterpret_cast<const struct Teddy *>(fdr);
    const size_t iterBytes = 32;
    DEBUG_PRINTF("params: buf %p len %zu start_offset %zu\n",
                 a->buf, a->len, a->start_offset);

    const m128 *maskBase = getMaskBase(teddy);
    const u32 *confBase = getConfBase(teddy);

    const u8 *mainStart = ROUNDUP_PTR(ptr, 16);
    DEBUG_PRINTF("derive: ptr: %p mainstart %p\n", ptr, mainStart);
    if (ptr < mainStart) {
        ptr = mainStart - 16;
        m128 p_mask;
        m128 val_0 = vectoredLoad128(&p_mask, ptr, a->start_offset,
                                     a->buf, buf_end,
                                     a->buf_history, a->len_history, NMSK);
        m128 r_0 = prep_conf_teddy_128_templ<NMSK>(maskBase, val_0);
        r_0 = or128(r_0, p_mask);
        CONFIRM_TEDDY_128(r_0, 8, 0, VECTORING, ptr);
        ptr += 16;
    }

    if (ptr + 16 <= buf_end) {
        m128 r_0 = prep_conf_teddy_128_templ<NMSK>(maskBase, load128(ptr));
        CONFIRM_TEDDY_128(r_0, 8, 0, VECTORING, ptr);
        ptr += 16;
    }

    for (; ptr + iterBytes <= buf_end; ptr += iterBytes) {
        __builtin_prefetch(ptr + (iterBytes * 4));
        CHECK_FLOOD;
        m128 r_0 = prep_conf_teddy_128_templ<NMSK>(maskBase, load128(ptr));
        CONFIRM_TEDDY_128(r_0, 8, 0, NOT_CAUTIOUS, ptr);
        m128 r_1 = prep_conf_teddy_128_templ<NMSK>(maskBase, load128(ptr + 16));
        CONFIRM_TEDDY_128(r_1, 8, 16, NOT_CAUTIOUS, ptr);
    }

    if (ptr + 16 <= buf_end) {
        m128 r_0 = prep_conf_teddy_128_templ<NMSK>(maskBase, load128(ptr));
        CONFIRM_TEDDY_128(r_0, 8, 0, NOT_CAUTIOUS, ptr);
        ptr += 16;
    }

    assert(ptr + 16 > buf_end);
    if (ptr < buf_end) {
        m128 p_mask;
        m128 val_0 = vectoredLoad128(&p_mask, ptr, 0, ptr, buf_end,
                                     a->buf_history, a->len_history, NMSK);
        m128 r_0 = prep_conf_teddy_128_templ<NMSK>(maskBase, val_0);
        r_0 = or128(r_0, p_mask);
        CONFIRM_TEDDY_128(r_0, 8, 0, VECTORING, ptr);
    }

    return HWLM_SUCCESS;
}

#define FDR_EXEC_TEDDY_FN fdr_exec_teddy_128_templ


#endif // HAVE_AVX2 HAVE_AVX512



extern "C" {

hwlm_error_t fdr_exec_teddy_msks1(const struct FDR *fdr,
                                  const struct FDR_Runtime_Args *a,
                                  hwlm_group_t control) {
    return FDR_EXEC_TEDDY_FN<1>(fdr, a, control);
}

hwlm_error_t fdr_exec_teddy_msks1_pck(const struct FDR *fdr,
                                      const struct FDR_Runtime_Args *a,
                                      hwlm_group_t control) {
    return FDR_EXEC_TEDDY_FN<1>(fdr, a, control);
}

hwlm_error_t fdr_exec_teddy_msks2(const struct FDR *fdr,
                                  const struct FDR_Runtime_Args *a,
                                  hwlm_group_t control) {
    return FDR_EXEC_TEDDY_FN<2>(fdr, a, control);
}

hwlm_error_t fdr_exec_teddy_msks2_pck(const struct FDR *fdr,
                                      const struct FDR_Runtime_Args *a,
                                      hwlm_group_t control) {
    return FDR_EXEC_TEDDY_FN<2>(fdr, a, control);
}

hwlm_error_t fdr_exec_teddy_msks3(const struct FDR *fdr,
                                  const struct FDR_Runtime_Args *a,
                                  hwlm_group_t control) {
    return FDR_EXEC_TEDDY_FN<3>(fdr, a, control);
}

hwlm_error_t fdr_exec_teddy_msks3_pck(const struct FDR *fdr,
                                      const struct FDR_Runtime_Args *a,
                                      hwlm_group_t control) {
    return FDR_EXEC_TEDDY_FN<3>(fdr, a, control);
}

hwlm_error_t fdr_exec_teddy_msks4(const struct FDR *fdr,
                                  const struct FDR_Runtime_Args *a,
                                  hwlm_group_t control) {
    return FDR_EXEC_TEDDY_FN<4>(fdr, a, control);
}

hwlm_error_t fdr_exec_teddy_msks4_pck(const struct FDR *fdr,
                                      const struct FDR_Runtime_Args *a,
                                      hwlm_group_t control) {
    return FDR_EXEC_TEDDY_FN<4>(fdr, a, control);
}

} // extern

