/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Network component library - common functions
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef NETCOPE_COMMON_H
#define NETCOPE_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <err.h>

#define NC_ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

struct list_range {
	size_t items;
	int *min;
	int *max;
};

static inline int nc_fast_rand(int *srand)
{
	*srand = 214013 * *srand + 2531011;
	return (*srand >> 16) & 0x7FFF;
}

static inline int nc_strtol(char *str, long *output)
{
	char *end;

	if (!str)
		return -1;

	*output = strtol(str, &end, 0);

	if (*end != '\0')
		return -1;
	return 0;
}

static inline unsigned long nc_xstrtoul(const char *str, int base)
{
	char *end;
	unsigned long val;

	val = strtoul(str, &end, base);
	if (*end != '\0' || str[0] == '\0')
		errx(1, "Cannot parse number '%s'", str);

	return val;
}

static inline int list_range_parse_intr(struct list_range *lr, const char *s, char **e);

#define list_range_skip_space(s) while(isspace(*s)) { s++; }
#define list_range_get_number(x, s, e) ((x) = strtol((s), (e), 10), *(e) == (s))

static inline void list_range_init(struct list_range *lr)
{
	lr->items = 0;
	lr->min = NULL;
	lr->max = NULL;
}
static inline void list_range_destroy(struct list_range *lr)
{
	if (lr->items) {
		free(lr->min);
		free(lr->max);
		lr->items = 0;
	}
}

static inline int list_range_add_number(struct list_range *lr, int x)
{
	lr->min = realloc(lr->min, (lr->items + 1) * sizeof(*lr->min));
	lr->max = realloc(lr->max, (lr->items + 1) * sizeof(*lr->max));
	lr->min[lr->items] = x;
	lr->max[lr->items] = x;
	lr->items++;
	return 1;
}

static inline int list_range_add_range(struct list_range *lr, int x, int y)
{
	if (y <= x)
		return -1;

	lr->min = realloc(lr->min, (lr->items + 1) * sizeof(*lr->min));
	lr->max = realloc(lr->max, (lr->items + 1) * sizeof(*lr->max));
	lr->min[lr->items] = x;
	lr->max[lr->items] = y;
	lr->items++;
	return y - x + 1;
}

static inline int list_range_parse(struct list_range *lr, const char *s)
{
	char *ee;
	char **e = &ee;
	int x;
	int ret;
	int count = 0;

	while (1) {
		list_range_skip_space(s);
		ret = list_range_parse_intr(lr, s, e);
		if (ret < 0) {
			if (list_range_get_number(x, s, e))
				break;
			count += list_range_add_number(lr, x);
		} else {
			count += ret;
		}
		s = *e;

		list_range_skip_space(s);
		if ((*s) == '\0') {
			return count;
		}
		if ((*s) == ',') {
			s++;
			continue;
		}
		break;
	}
	return -1;
}

static inline int list_range_parse_intr(struct list_range *lr, const char *s, char **e)
{
	int x, y;
	char *ee;
	if (list_range_get_number(x, s, &ee)) {
		return -1;
	}
	s = ee;

	list_range_skip_space(s);
	if (*s != '-') {
		*(const char **)e = s;
		return -1;
	}
	s++;
	if (list_range_get_number(y, s, e)) {
		return -1;
	}
	return list_range_add_range(lr, x, y);
}

static inline int list_range_contains(struct list_range *lr, int item)
{
	unsigned i;
	for (i = 0; i < lr->items; i++) {
		if (lr->min[i] <= item && lr->max[i] >= item) {
			return 1;
		}
	}
	return 0;
}

static inline int list_range_count(struct list_range *lr)
{
	unsigned i;
	int count = 0;
	for (i = 0; i < lr->items; i++) {
		count += lr->max[i] - lr->min[i] + 1;
	}
	return count;
}

static inline int list_range_empty(struct list_range *lr)
{
	return lr->items == 0;
}

static inline int nc_query_parse(const char *query, const char* const* choices,
	int size, char **index)
{
	int len;
	char hit;
	char *query_act, *query_orig;
	char *idx = NULL;
	char idx_size = 0;
	query_orig = query_act = strdup(query);
	if (query_act == NULL) {
		errx(1, "problem with memory allocation");
		return -1;
	}
	do {
		hit = 1;
		len = strchr(query_act,',') ? (strchr(query_act,',') - query_act) : 0;
		if (len)
			query_act[len] = '\0';
		for (int i=0; i<size; ++i) {
			if (!strcmp(query_act, *(choices+i))) {
				idx_size++;
				idx = (char *) realloc(idx, idx_size*sizeof(char));
				*(idx+(idx_size-1)) = i;
				hit = 0;
				continue;
			}
		}
		if (hit) {
			fprintf(stderr, "invalid query argument - '");
			int i = 0;
			while (*(query_act+i) != '\0') {
				fprintf(stderr, "%c", *(query_act+i));
				++i;
			}
			fprintf(stderr, "'\n");
			free(query_orig);
			free(idx);
			return -1;
		}
		query_act += len + 1;
	} while (len);
	free(query_orig);
	*index = idx;
	return idx_size;
}

/*!
 * \brief Function to expand formating string %{char} to position format for printf
 *  Example usage str_expand_format(format_pcap_path, sizeof(format_pcap_path), p->pcap_path, "td", "dd");
 *  "foo %d %t %t %d bar" -> "foo %2$d %1$d %1$d %2$d bar"
 * \param [out] dst
 * \param [in] n - dst size
 * \param [in] src
 * \param [in] posArgs - characters which will be resolved to positional arguments
 * \param [in] posArgsType - types for printf positional arguments
 * \return
 *   - length of the formated string
 */
static inline size_t str_expand_format(char *dst, size_t n, const char *src, const char *posArgs, const char *posArgsType)
{
	size_t posArgsLen = strlen(posArgs);
	const char *srcI = src;
	char *dstI = dst;
	while(*srcI != '\0' && (size_t)(dstI - dst) < n-1) {
		if(*srcI == '%') {
				size_t posArgIndex = 0;
				for(size_t i = 0; i < posArgsLen; i++) {
					if(posArgs[i] == *(srcI+1)) {
						posArgIndex = i;
						break;
					}
				}
				srcI += 2; //Skip percentage and argument
				if(posArgIndex != posArgsLen && (size_t)((dstI+4)-dst) < n-1 ) {
					*dstI++ = '%';
					*dstI++ = posArgIndex + 1 + '0';
					*dstI++ = '$';
					*dstI++ = posArgsType[posArgIndex];
				}
		} else {
			*dstI++ = *srcI++;
		}
	}
	*dstI = '\0';
	return (size_t) (dstI - dst);
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* NETCOPE_COMMON_H */
