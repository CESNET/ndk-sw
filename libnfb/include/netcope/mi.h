/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * libnfb - boot module - memory interface module
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */


/*
 * INFO1: Some CPU/machines hangs on high frequency bus access with size < 32bits
 * INFO2: Also Valgrind does ugly accesses with classic memcpy function
 * Here is a workaround for these issues
 */

#define _NFB_BUS_MI_MEMCPY_CYCLE_ITER(nbyte, size, src, dst, mtype, wc_used, wc_instr) \
do {							\
	if (*nbyte == (size)) {			\
		if (wc_instr)				\
			*wc_used = true;		\
		return true;				\
	}						\
	*(src) = (const mtype *) *(src) + 1; 		\
	*(dst) = (mtype *) *(dst) + 1; 			\
	*nbyte -= (size);				\
} while (0)

#define _NFB_BUS_MI_MEMCPY_CYCLE_LS(nbyte, size, src, dst, mtype, load, store, wc_used, wc_instr) \
do {							\
	mtype tmp;					\
	while (*nbyte >= (size)) {			\
		tmp = load(*(src));			\
		store(*(dst), tmp);			\
		_NFB_BUS_MI_MEMCPY_CYCLE_ITER(nbyte, size, src, dst, mtype, wc_used, wc_instr);	\
	}						\
} while (0)

#define _NFB_BUS_MI_MEMCPY_CYCLE_ST(nbyte, size, src, dst, mtype, stream, wc_used, wc_instr) \
do {							\
	while (*nbyte >= (size)) {			\
		stream(*(dst), *(const mtype *) *(src));	\
		_NFB_BUS_MI_MEMCPY_CYCLE_ITER(nbyte, size, src, dst, mtype, wc_used, wc_instr);	\
	}						\
} while (0)

#define _NFB_BUS_MI_MEMCPY_CYCLE_AS(nbyte, size, src, dst, mtype, wc_used, wc_instr) \
do {							\
	while (*nbyte >= (size)) {			\
		*(mtype *) *(dst) = *(const mtype *) *(src);	\
		_NFB_BUS_MI_MEMCPY_CYCLE_ITER(nbyte, size, src, dst, mtype, wc_used, wc_instr);	\
	}						\
} while (0)


static inline bool nfb_bus_mi_memcopy_simple(void *dst, const void *src, size_t nbyte, size_t offset, bool *wc_used)
{
	(void) offset;
	(void) wc_used;

	if (nbyte == 4) {
		*(uint32_t *) dst = *(const uint32_t *) src;
		return true;
	} else if (nbyte == 8) {
		*(uint64_t *) dst = *(const uint64_t *) src;
		return true;
	}
	return false;
}

static inline bool nfb_bus_mi_memcopy_prelude(void **dst, const void **src, size_t *nbyte, size_t *offset, bool *wc_used)
{
	/* The offset variable is just for alignment check. The dst variable is alredy offseted. */
	/* Align access on 4/8B boundary first */
	if (*offset & 0x03) {
		if (*offset & 0x01 && *nbyte >= 1) {
			*(uint8_t *) *dst = *(const uint8_t *) *src;
			_NFB_BUS_MI_MEMCPY_CYCLE_ITER(nbyte, 1, src, dst, uint8_t, wc_used, false);
			*offset += 1;
		}
		if (*offset & 0x02 && *nbyte >= 2) {
			*(uint16_t *) *dst = *(const uint16_t *) *src;
			_NFB_BUS_MI_MEMCPY_CYCLE_ITER(nbyte, 2, src, dst, uint16_t, wc_used, false);
			*offset += 2;
		}
	}

	if (*offset & 0x04 && *nbyte >= 4) {
		*(uint32_t *) *dst = *(const uint32_t *) *src;
		_NFB_BUS_MI_MEMCPY_CYCLE_ITER(nbyte, 4, src, dst, uint32_t, wc_used, false);
		*offset += 4;
	}
	return false;
}

#ifdef CONFIG_HAVE_MAVX2
static inline bool nfb_bus_mi_memcopy_interlude_avx_sse2(void **dst, const void **src, size_t *nbyte, size_t *offset, bool *wc_used)
{
	(void) offset;

	bool src256a = ((uintptr_t) *src & 0x1F) ? false : true;
	bool dst256a = ((uintptr_t) *dst & 0x1F) ? false : true;
	bool src128a = ((uintptr_t) *src & 0x0F) ? false : true;
	bool dst128a = ((uintptr_t) *dst & 0x0F) ? false : true;

	/* The _mm_stream* instructions are using the non-temporal hint.
	 * The non-temporal hint is implemented by using a write combining (WC) memory type protocol.
	 * The WC protocol uses a weakly-ordered memory consistency model, fencing operation should be used. */

	if (src256a && dst256a) {
		_NFB_BUS_MI_MEMCPY_CYCLE_ST(nbyte, 32, src, dst, __m256i, _mm256_stream_si256, wc_used, true);
	} else if (src256a && !dst256a) {
		_NFB_BUS_MI_MEMCPY_CYCLE_LS(nbyte, 32, src, dst, __m256i, _mm256_stream_load_si256, _mm256_storeu_si256, wc_used, false);
	} else if (!src256a && dst256a) {
		_NFB_BUS_MI_MEMCPY_CYCLE_LS(nbyte, 32, src, dst, __m256i, _mm256_loadu_si256, _mm256_store_si256, wc_used, false);
	} else {
		_NFB_BUS_MI_MEMCPY_CYCLE_LS(nbyte, 32, src, dst, __m256i, _mm256_loadu_si256, _mm256_storeu_si256, wc_used, false);
	}

	if (src128a && dst128a) {
		_NFB_BUS_MI_MEMCPY_CYCLE_ST(nbyte, 16, src, dst, __m128i, _mm_stream_si128, wc_used, true);
	} else if (src128a && !dst128a) {
		_NFB_BUS_MI_MEMCPY_CYCLE_LS(nbyte, 16, src, dst, __m128i, _mm_load_si128, _mm_storeu_si128, wc_used, false);
	} else if (!src128a && dst128a) {
		_NFB_BUS_MI_MEMCPY_CYCLE_LS(nbyte, 16, src, dst, __m128i, _mm_loadu_si128, _mm_stream_si128, wc_used, false);
	} else {
		_NFB_BUS_MI_MEMCPY_CYCLE_LS(nbyte, 16, src, dst, __m128i, _mm_loadu_si128, _mm_storeu_si128, wc_used, false);
	}

	return false;
}
#endif

static inline bool nfb_bus_mi_memcopy_postlude(void **dst, const void **src, size_t *nbyte, size_t *offset, bool *wc_used)
{
	(void) offset;

	_NFB_BUS_MI_MEMCPY_CYCLE_AS(nbyte, 8, src, dst, uint64_t, wc_used, false);

	/* Access the remaining bytes */
	if (*nbyte >= 4) {
		*(uint32_t *) *dst = *(const uint32_t *) *src;
		_NFB_BUS_MI_MEMCPY_CYCLE_ITER(nbyte, 4, src, dst, uint32_t, wc_used, false);
	}
	if (*nbyte >= 2) {
		*(uint16_t *) *dst = *(const uint16_t *) *src;
		_NFB_BUS_MI_MEMCPY_CYCLE_ITER(nbyte, 2, src, dst, uint16_t, wc_used, false);
	}
	if (*nbyte >= 1) {
		*(uint8_t *) *dst = *(const uint8_t *) *src;
		_NFB_BUS_MI_MEMCPY_CYCLE_ITER(nbyte, 1, src, dst, uint8_t, wc_used, false);
	}

	return false;
}

static inline ssize_t nfb_bus_mi_memcopy_avx2_sse2(void *dst, const void *src, size_t nbyte, size_t offset, bool *wc_used)
{
	ssize_t ret = nbyte;
	if (nfb_bus_mi_memcopy_simple(dst, src, nbyte, offset, wc_used))
		return ret;

	if (nfb_bus_mi_memcopy_prelude(&dst, &src, &nbyte, &offset, wc_used))
		return ret;
#ifdef CONFIG_HAVE_MAVX2
	if (nfb_bus_mi_memcopy_interlude_avx_sse2(&dst, &src, &nbyte, &offset, wc_used))
		return ret;
#endif
	if (nfb_bus_mi_memcopy_postlude(&dst, &src, &nbyte, &offset, wc_used))
		return ret;

	return ret;
}

static inline ssize_t nfb_bus_mi_memcopy_noopt(void *dst, const void *src, size_t nbyte, size_t offset, bool *wc_used)
{
	ssize_t ret = nbyte;
	if (nfb_bus_mi_memcopy_simple(dst, src, nbyte, offset, wc_used))
		return ret;

	if (nfb_bus_mi_memcopy_prelude(&dst, &src, &nbyte, &offset, wc_used))
		return ret;
	if (nfb_bus_mi_memcopy_postlude(&dst, &src, &nbyte, &offset, wc_used))
		return ret;

	return ret;
}
