/*
 * Copyright (c) 2016-2020, Intel Corporation
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
 * \brief Teddy literal matcher: common runtime procedures.
 */

#ifndef TEDDY_RUNTIME_COMMON_H_
#define TEDDY_RUNTIME_COMMON_H_

#include "fdr_confirm.h"
#include "fdr_confirm_runtime.h"
#include "ue2common.h"
#include "util/bitutils.h"
#include "util/simd_utils.h"
#include "util/uniform_ops.h"


#if defined(HAVE_AVX512VBMI)
static const u8 ALIGN_DIRECTIVE p_sh_mask_arr[80] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f
};
#endif

#ifdef ARCH_64_BIT
#define TEDDY_CONF_TYPE u64a
#define TEDDY_FIND_AND_CLEAR_LSB(conf) findAndClearLSB_64(conf)
#else
#define TEDDY_CONF_TYPE u32
#define TEDDY_FIND_AND_CLEAR_LSB(conf) findAndClearLSB_32(conf)
#endif

#define CHECK_HWLM_TERMINATE_MATCHING                                       \
do {                                                                        \
    if (unlikely(control == HWLM_TERMINATE_MATCHING)) {                     \
        return HWLM_TERMINATED;                                             \
    }                                                                       \
} while (0);

#define CHECK_FLOOD                                                         \
do {                                                                        \
    if (unlikely(ptr > tryFloodDetect)) {                                   \
        tryFloodDetect = floodDetect(fdr, a, &ptr, tryFloodDetect,          \
                                     &floodBackoff, &control, iterBytes);   \
        CHECK_HWLM_TERMINATE_MATCHING;                                      \
    }                                                                       \
} while (0);

/*
 * \brief Copy a block of [0,15] bytes efficiently.
 *
 * This function is a workaround intended to stop some compilers from
 * synthesizing a memcpy function call out of the copy of a small number of
 * bytes that we do in vectoredLoad128.
 */
static really_inline
void copyRuntBlock128(u8 *dst, const u8 *src, size_t len) {
    switch (len) {
    case 0:
        break;
    case 1:
        *dst = *src;
        break;
    case 2:
        unaligned_store_u16(dst, unaligned_load_u16(src));
        break;
    case 3:
        unaligned_store_u16(dst, unaligned_load_u16(src));
        dst[2] = src[2];
        break;
    case 4:
        unaligned_store_u32(dst, unaligned_load_u32(src));
        break;
    case 5:
    case 6:
    case 7:
        /* Perform copy with two overlapping 4-byte chunks. */
        unaligned_store_u32(dst + len - 4, unaligned_load_u32(src + len - 4));
        unaligned_store_u32(dst, unaligned_load_u32(src));
        break;
    case 8:
        unaligned_store_u64a(dst, unaligned_load_u64a(src));
        break;
    default:
        /* Perform copy with two overlapping 8-byte chunks. */
        assert(len < 16);
        unaligned_store_u64a(dst + len - 8, unaligned_load_u64a(src + len - 8));
        unaligned_store_u64a(dst, unaligned_load_u64a(src));
        break;
    }
}

// Note: p_mask is an output param that initialises a poison mask.
//       *p_mask = load128(p_mask_arr[n] + 16 - m) means:
//       m byte 0xff in the beginning, followed by n byte 0x00,
//       then followed by the rest bytes 0xff.
// ptr >= lo:
//     no history.
//     for end/short zone, ptr==lo and start_offset==0
//     for start zone, see below
//          lo         ptr                      hi           hi
//          |----------|-------|----------------|............|
//          -start     0       -start+offset    MIN(avail,16)
// p_mask              ffff..ff0000...........00ffff..........
// ptr < lo:
//     only start zone.
//             history
//          ptr        lo                       hi           hi
//          |----------|-------|----------------|............|
//          0          start   start+offset     end(<=16)
// p_mask   ffff.....ffffff..ff0000...........00ffff..........

// replace the p_mask_arr table.
// m is the length of the zone of bytes==0 , n is
// the offset where that zone begins. more specifically, there are
// 16-n bytes of 1's before the zone begins.
// m,n 4,7  - 4 bytes of 0s, and 16-7 bytes of 1's before that.
// 00 00 00 00 ff..ff
// ff ff ff ff ff ff ff ff 00 00 00 00 ff..ff
// m,n 15,15 - 15 bytes of 0s , f's high, but also with 16-15=1 byte of 1s
// in the beginning - which push the ff at the end off the high end , leaving
// ff 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
// m,n 15,16 - 15 bytes of 0s, ff high , with 16-16 = 0 ones on the low end
// before that, so,
// 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ff
// so to get the one part, with the f's high, we start out with 1's and
// shift them up (right) by m+n.
// now to fill in any ones that belong on the low end we have to take
// some 1's and shift them down. the ones zone there needs to be 16-n long,
// meaning shifted down by 16-(16-n) , or of course just n.
// then we should be able to or these together.
static really_inline
m128 p_mask_gen(u8 m, u8 n){
    m128 a = ones128();
    m128 b = ones128();
    m%=17; n%=17;
    m+=(16-n); m%=17;
    a = rshiftbyte_m128(a, n);
    b = lshiftbyte_m128(b, m);
    return or128(a, b);
}

static really_inline
m128 vectoredLoad128(m128 *p_mask, const u8 *ptr, const size_t start_offset,
                     const u8 *lo, const u8 *hi,
                     const u8 *buf_history, size_t len_history,
                     const u32 nMasks) {
    union {
        u8 val8[16];
        m128 val128;
    } u;
    u.val128 = zeroes128();

    uintptr_t copy_start;
    uintptr_t copy_len;

    if (ptr >= lo) { // short/end/start zone
        uintptr_t start = (uintptr_t)(ptr - lo);
        uintptr_t avail = (uintptr_t)(hi - ptr);
        if (avail >= 16) {
            assert(start_offset - start <= 16);
            *p_mask = p_mask_gen(16 - start_offset + start, 16 - start_offset + start);
            return loadu128(ptr);
        }
        assert(start_offset - start <= avail);
        *p_mask = p_mask_gen(avail - start_offset + start, 16 - start_offset + start);
        copy_start = 0;
        copy_len = avail;
    } else { // start zone
        uintptr_t need = MIN((uintptr_t)(lo - ptr),
                             MIN(len_history, nMasks - 1));
        uintptr_t start = (uintptr_t)(lo - ptr);
        uintptr_t i;
        for (i = start - need; i < start; i++) {
            u.val8[i] = buf_history[len_history - (start - i)];
        }
        uintptr_t end = MIN(16, (uintptr_t)(hi - ptr));
        assert(start + start_offset <= end);
        *p_mask = p_mask_gen(end - start - start_offset, 16 - start - start_offset);
        copy_start = start;
        copy_len = end - start;
    }

    // Runt block from the buffer.
    copyRuntBlock128(&u.val8[copy_start], &ptr[copy_start], copy_len);

    return u.val128;
}

#if defined(HAVE_AVX2)
/*
 * \brief Copy a block of [0,31] bytes efficiently.
 *
 * This function is a workaround intended to stop some compilers from
 * synthesizing a memcpy function call out of the copy of a small number of
 * bytes that we do in vectoredLoad256.
 */
static really_inline
void copyRuntBlock256(u8 *dst, const u8 *src, size_t len) {
    switch (len) {
    case 0:
        break;
    case 1:
        *dst = *src;
        break;
    case 2:
        unaligned_store_u16(dst, unaligned_load_u16(src));
        break;
    case 3:
        unaligned_store_u16(dst, unaligned_load_u16(src));
        dst[2] = src[2];
        break;
    case 4:
        unaligned_store_u32(dst, unaligned_load_u32(src));
        break;
    case 5:
    case 6:
    case 7:
        /* Perform copy with two overlapping 4-byte chunks. */
        unaligned_store_u32(dst + len - 4, unaligned_load_u32(src + len - 4));
        unaligned_store_u32(dst, unaligned_load_u32(src));
        break;
    case 8:
        unaligned_store_u64a(dst, unaligned_load_u64a(src));
        break;
    case 9:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
        /* Perform copy with two overlapping 8-byte chunks. */
        unaligned_store_u64a(dst + len - 8, unaligned_load_u64a(src + len - 8));
        unaligned_store_u64a(dst, unaligned_load_u64a(src));
        break;
    case 16:
        storeu128(dst, loadu128(src));
        break;
    default:
        /* Perform copy with two overlapping 16-byte chunks. */
        assert(len < 32);
        storeu128(dst + len - 16, loadu128(src + len - 16));
        storeu128(dst, loadu128(src));
        break;
    }
}

// Note: p_mask is an output param that initialises a poison mask.
//       *p_mask = load256(p_mask_arr256[n] + 32 - m) means:
//       m byte 0xff in the beginning, followed by n byte 0x00,
//       then followed by the rest bytes 0xff.
// ptr >= lo:
//     no history.
//     for end/short zone, ptr==lo and start_offset==0
//     for start zone, see below
//          lo         ptr                      hi           hi
//          |----------|-------|----------------|............|
//          -start     0       -start+offset    MIN(avail,32)
// p_mask              ffff..ff0000...........00ffff..........
// ptr < lo:
//     only start zone.
//             history
//          ptr        lo                       hi           hi
//          |----------|-------|----------------|............|
//          0          start   start+offset     end(<=32)
// p_mask   ffff.....ffffff..ff0000...........00ffff..........

// like the pmask gen above this replaces the large array.
static really_inline
m256 fat_pmask_gen(u8 m, u8 n){
    m256 a=ones256();
    m256 b=ones256();
    m%=33; n%=33;
    m+=(32-n); m%=33;

    a = rshift_byte_m256(a, m);
    b = lshift_byte_m256(b, n);
    return or256(a, b);
}

static really_inline
m256 vectoredLoad256(m256 *p_mask, const u8 *ptr, const size_t start_offset,
                     const u8 *lo, const u8 *hi,
                     const u8 *buf_history, size_t len_history,
                     const u32 nMasks) {
    union {
        u8 val8[32];
        m256 val256;
    } u;
    u.val256 = zeroes256();

    uintptr_t copy_start;
    uintptr_t copy_len;

    if (ptr >= lo) { // short/end/start zone
        uintptr_t start = (uintptr_t)(ptr - lo);
        uintptr_t avail = (uintptr_t)(hi - ptr);
        if (avail >= 32) {
            assert(start_offset - start <= 32);
            *p_mask = fat_pmask_gen(32 - start_offset + start, 32 - start_offset + start);
            return loadu256(ptr);
        }
        assert(start_offset - start <= avail);
        *p_mask = fat_pmask_gen(avail - start_offset + start, 32 - start_offset + start);
        copy_start = 0;
        copy_len = avail;
    } else { //start zone
        uintptr_t need = MIN((uintptr_t)(lo - ptr),
                             MIN(len_history, nMasks - 1));
        uintptr_t start = (uintptr_t)(lo - ptr);
        uintptr_t i;
        for (i = start - need; i < start; i++) {
            u.val8[i] = buf_history[len_history - (start - i)];
        }
        uintptr_t end = MIN(32, (uintptr_t)(hi - ptr));
        assert(start + start_offset <= end);
        *p_mask = fat_pmask_gen(end - start - start_offset, 32 - start - start_offset);
        copy_start = start;
        copy_len = end - start;
    }

    // Runt block from the buffer.
    copyRuntBlock256(&u.val8[copy_start], &ptr[copy_start], copy_len);

    return u.val256;
}
#endif // HAVE_AVX2

#if defined(HAVE_AVX512)
// Note: p_mask is an output param that initialises a poison mask.
//       u64a k = ones_u64a << n' >> m'; // m' < n'
//       *p_mask = set_mask_m512(~k);
//       means p_mask is consist of:
//       (n' - m') poison bytes "0xff" at the beginning,
//       followed by (64 - n') valid bytes "0x00",
//       then followed by the rest m' poison bytes "0xff".
// ptr >= lo:
//     no history.
//     for end/short zone, ptr==lo and start_offset==0
//     for start zone, see below
//          lo         ptr                      hi           hi
//          |----------|-------|----------------|............|
//          -start     0       -start+offset    MIN(avail,64)
// p_mask              ffff..ff0000...........00ffff..........
// ptr < lo:
//     only start zone.
//             history
//          ptr        lo                       hi           hi
//          |----------|-------|----------------|............|
//          0          start   start+offset     end(<=64)
// p_mask   ffff.....ffffff..ff0000...........00ffff..........
static really_inline
m512 vectoredLoad512(m512 *p_mask, const u8 *ptr, const size_t start_offset,
                     const u8 *lo, const u8 *hi, const u8 *hbuf, size_t hlen,
                     const u32 nMasks) {
    m512 val = zeroes512();

    uintptr_t copy_start;
    uintptr_t copy_len;

    if (ptr >= lo) { // short/end/start zone
        uintptr_t start = (uintptr_t)(ptr - lo);
        uintptr_t avail = (uintptr_t)(hi - ptr);
        if (avail >= 64) {
            assert(start_offset - start <= 64);
            u64a k = ones_u64a << (start_offset - start);
            *p_mask = set_mask_m512(~k);
            return loadu512(ptr);
        }
        assert(start_offset - start <= avail);
        u64a k = ones_u64a << (64 - avail + start_offset - start)
                           >> (64 - avail);
        *p_mask = set_mask_m512(~k);
        copy_start = 0;
        copy_len = avail;
    } else { //start zone
        uintptr_t need = MIN((uintptr_t)(lo - ptr),
                             MIN(hlen, nMasks - 1));
        uintptr_t start = (uintptr_t)(lo - ptr);
        u64a j = 0x7fffffffffffffffULL >> (63 - need) << (start - need);
        val = loadu_maskz_m512(j, &hbuf[hlen - start]);
        uintptr_t end = MIN(64, (uintptr_t)(hi - ptr));
        assert(start + start_offset <= end);
        u64a k = ones_u64a << (64 - end + start + start_offset) >> (64 - end);
        *p_mask = set_mask_m512(~k);
        copy_start = start;
        copy_len = end - start;
    }

    assert(copy_len < 64);
    assert(copy_len > 0);
    u64a j = ones_u64a >> (64 - copy_len) << copy_start;
    val = loadu_mask_m512(val, j, ptr);

    return val;
}
#endif // HAVE_AVX512

static really_inline
u64a getConfVal(const struct FDR_Runtime_Args *a, const u8 *ptr, u32 byte,
                UNUSED CautionReason reason) {
    u64a confVal = 0;
    const u8 *buf = a->buf;
    size_t len = a->len;
    const u8 *confirm_loc = ptr + byte - 7;
#if defined(HAVE_AVX512VBMI)
    if (likely(confirm_loc >= buf)) {
#else
    if (likely(reason == NOT_CAUTIOUS || confirm_loc >= buf)) {
#endif
        confVal = lv_u64a(confirm_loc, buf, buf + len);
    } else { // r == VECTORING, confirm_loc < buf
        u64a histBytes = a->histBytes;
        confVal = lv_u64a_ce(confirm_loc, buf, buf + len);
        // stitch together confVal and history
        u32 overhang = buf - confirm_loc;
        histBytes >>= 64 - (overhang * 8);
        confVal |= histBytes;
    }
    return confVal;
}

static really_inline
void do_confWithBit_teddy(TEDDY_CONF_TYPE *conf, u8 bucket, u8 offset,
                          const u32 *confBase, CautionReason reason,
                          const struct FDR_Runtime_Args *a, const u8 *ptr,
                          hwlmcb_rv_t *control, u32 *last_match) {
    do  {
        u32 bit = TEDDY_FIND_AND_CLEAR_LSB(conf);
        u32 byte = bit / bucket + offset;
        u32 idx  = bit % bucket;
        u32 cf = confBase[idx];
        if (!cf) {
            continue;
        }
#ifdef __cplusplus
        const struct FDRConfirm *fdrc = reinterpret_cast<const struct FDRConfirm *>
                                        (reinterpret_cast<const u8 *>(confBase) + cf);
#else
        const struct FDRConfirm *fdrc = (const struct FDRConfirm *)
                                        ((const u8 *)confBase + cf);
#endif
        if (!(fdrc->groups & *control)) {
            continue;
        }
        u64a tmp = 0;
        u64a confVal = getConfVal(a, ptr, byte, reason);
        confWithBit(fdrc, a, ptr - a->buf + byte, control,
                    last_match, confVal, &tmp, 0);
    } while (unlikely(*conf));
}

static really_inline
const m128 *getMaskBase(const struct Teddy *teddy) {
#ifdef __cplusplus
    return reinterpret_cast<const m128 *>(reinterpret_cast<const u8 *>(teddy) + ROUNDUP_CL(sizeof(struct Teddy)));
#else
    return (const m128 *)((const u8 *)teddy + ROUNDUP_CL(sizeof(struct Teddy)));
#endif
}

static really_inline
const u64a *getReinforcedMaskBase(const struct Teddy *teddy, u8 numMask) {
#ifdef __cplusplus
    return reinterpret_cast<const u64a *>(reinterpret_cast<const u8 *>(getMaskBase(teddy))
                          + ROUNDUP_CL(2 * numMask * sizeof(m128)));
#else
    return (const u64a *)((const u8 *)getMaskBase(teddy)
                          + ROUNDUP_CL(2 * numMask * sizeof(m128)));
#endif
}

static really_inline
const u32 *getConfBase(const struct Teddy *teddy) {
#ifdef __cplusplus
    return reinterpret_cast<const u32 *>(reinterpret_cast<const u8 *>(teddy) + teddy->confOffset);
#else
    return (const u32 *)((const u8 *)teddy + teddy->confOffset);
#endif
}

#endif /* TEDDY_RUNTIME_COMMON_H_ */
