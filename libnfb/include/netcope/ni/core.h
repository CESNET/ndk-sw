/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Library for unified formated output of normalized items: core
 *
 * Copyright (C) 2025 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef __NETCOPE_NI_CORE__
#define __NETCOPE_NI_CORE__


/*
 * Core implementation for unified formated output consist of:
 *
 * struct ni_callback - exposes base interface to be implemented in each output plugin
 *
 * struct ni_context *ni_init_root_context(ni_callbacks, items, init_params, item_cbs)
 * void ni_close_root_context(ni_context)
 *
 * void ni_section(c, i) / void ni_endsection(c, i)
 * void ni_list(c, i) / void ni_endlist(c, i)
 *   Functions for generating structural elements.
 *   args:
 *       c (struct ni_context*): context handle
 *       i (int): item index to be produced
 *
 * NI_ITEM_CB(name, type, cb_t, vp_n)
 *   Common macro for generating value-print function (ni_item_*)
 *   args:
 *       name: suffix for generated ni_item_ function
 *       type: type of accepted value
 *       cb_t: type of struct with value print callbacks
 *       vp_n: function name inside cb_t for value print
 *
 * Generated function ni_item_name(c, i, v)
 *   args:
 *   	c (struct ni_context *): context handle
 *   	i (int): item index to be produced
 *   	v (_generic_): value to be produced
 *
 */


struct ni_common_init_params {
	void *items;
	void* (*get)(void *items, int item_index);
};

struct ni_callbacks {
	int (*init)(void *init_params, struct ni_common_init_params *cip, void **priv);
	void (*close)(void *priv);

	/* produce plugin-specific output before producing item output
	 *
	 * args:
	 *     priv: plugin private data
	 *     item: custom data structure for specified item
	 *     cb_priv: pointer to handle which will be passed to value-print callback
	 * ret:
	 *     -1: do not print value
	 *     -2: do not call postlude
	 *   >= 0: print value, call postlude
	 */
	int (*prelude)(void *priv, int item, void **cb_priv);

	/* produce plugin-specific output after producing item output
	 * args:
	 *     item: custom data structure for specified item
	 *     value_length: length of item produced output
	 */
	void (*postlude)(void *priv, int item, int value_length);

	/* produce structural element output */
	void (*section)(void *priv, int item);
	void (*endsection)(void *priv, int item);
	void (*list)(void *priv, int item);
	void (*endlist)(void *priv, int item);
};

struct ni_context {
	const struct ni_callbacks *cbs;
	void *priv;
	const void *item_cbs;
};


typedef struct ni_context_item ni_context_item_t;


static inline struct ni_context *ni_init_root_context(const struct ni_callbacks * cbs, void *init_params, struct ni_common_init_params * cip, const void *item_cbs)
{
	int ret = 0;
	struct ni_context *ctx;

	if (cbs == NULL || item_cbs == NULL)
		return NULL;

	ctx = malloc(sizeof *ctx);
	if (ctx == NULL)
		return NULL;

	ctx->cbs = cbs;
	ctx->item_cbs = item_cbs;
	if (ctx->cbs->init)
		ret = ctx->cbs->init(init_params, cip, &ctx->priv);
	if (ret) {
		free(ctx);
		return NULL;
	}
	return ctx;
}

static inline void ni_close_root_context(struct ni_context *ctx)
{
	if (ctx) {
		if (ctx->cbs->close) {
			ctx->cbs->close(ctx->priv);
		}
		free(ctx);
	}
}

static inline void ni_section(struct ni_context *ctx, int item)
{
	if (ctx && ctx->cbs->section) {
		ctx->cbs->section(ctx->priv, item);
	}
}

static inline void ni_endsection(struct ni_context *ctx, int item)
{
	if (ctx && ctx->cbs->endsection) {
		ctx->cbs->endsection(ctx->priv, item);
	}
}

static inline void ni_list(struct ni_context *ctx, int item)
{
	if (ctx && ctx->cbs->list) {
		ctx->cbs->list(ctx->priv, item);
	}
}

static inline void ni_endlist(struct ni_context *ctx, int item)
{
	if (ctx && ctx->cbs->endlist) {
		ctx->cbs->endlist(ctx->priv, item);
	}
}

#define NI_ITEM_CB(name, type, cb_t, cb_n)\
static inline void ni_item_##name(struct ni_context * ctx, int item, type value)\
{\
	int _ret;\
	int _len = 0;\
	const struct cb_t * cbs;\
	void * cb_priv = NULL;\
\
	if (ctx == NULL)\
		return;\
	cbs = ctx->item_cbs;\
\
	_ret = ctx->cbs->prelude(ctx->priv, item, &cb_priv);\
	if (_ret == -1) {\
		return;\
	} else if (_ret >= 0) {\
		_len = cbs->cb_n(cb_priv, item, value);\
	}\
	ctx->cbs->postlude(ctx->priv, item, _len);\
}

/* Common helpers */
#define _ni_enc_int8_t(x, bit)\
		((x) >= 0 ?\
		((((unsigned long long)(+(x))) & 0x7F) << (bit)) :\
		((((unsigned long long)(-(x))) & 0x7F) << (bit)) | (1ll << ((bit) + 7)))

#define _ni_dec_int8_t(x, bit)\
		((x) & (1ll << ((bit) + 7)) ?\
		-((((signed long long)(x)) >> (bit)) & 0x7F) :\
		+((((signed long long)(x)) >> (bit)) & 0x7F))

#endif // __NETCOPE_NI_CORE__
