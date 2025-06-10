/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Library for unified formated output of normalized items: JSON output plugin
 *
 * Copyright (C) 2025 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef __NETCOPE_NI_JSON__
#define __NETCOPE_NI_JSON__

#define _NI_JSON_TYPE_COMMON    0
#define _NI_JSON_TYPE_ARRAY     1
#define _NI_JSON_TYPE_OBJECT    2

#define _NI_JSON_FLAG_NOKEY     (1 << 0)

struct ni_json_item {
	const char *key;
	unsigned long long flags;
};

/* Predefined shortcuts for specifying items */
/* Default item */
#define ni_json_k(key) {key, 0}

/* No-output item */
#define ni_json_n {NULL, 0}

/* Item with no key (e.g. array item) */
#define ni_json_e {NULL, _NI_JSON_FLAG_NOKEY}

/* Item with specific flags */
#define ni_json_f(key, flags) {key, flags}

#define ni_json_f_decim(x)      _ni_enc_int8_t(x, 32)
#define ni_json_f_decim_dec(x)  _ni_dec_int8_t(x, 32)


/* Internals */
struct ni_json_stack_state {
	int current_index;      /* Current item index in array / object */
	int current_type;
	const char *nip;
};

struct ni_json_cbp {
	FILE *f;
	int item;
	int8_t decim;
};

struct ni_json_priv {
	struct ni_json_cbp cbp; /* data for item callback */
	struct ni_common_init_params ip;
	void *items;

	/* constants */
	const char *nls;

	int use_nls;
	int indent_size;

	/* variables */
	const char *label;
	const char *delim;      /* delimiter between key (label) and value */
	const char *vc;         /* value container: put value inside of */
	const char *pfx;
	const char *nl;
	int align;
	int ind;

	int indent;

	/* internals */
	struct ni_json_stack_state *s;
	struct ni_json_stack_state stack_states[];
};

static inline int ni_json_init(void *init_params, struct ni_common_init_params *cip, void **ppriv)
{
	const int stack_size = 16;

	struct ni_json_priv *ctx;

	(void) init_params;

	if(cip->get == NULL || cip->items == NULL)
		return -EINVAL;

	ctx = malloc(sizeof(struct ni_json_priv) + sizeof(struct ni_json_stack_state) * stack_size);
	if (ctx == NULL)
		return -ENOMEM;
	*ppriv = ctx;
	ctx->ip = *cip;

	ctx->use_nls = 1;
	ctx->nls = "\n";
	ctx->indent_size = 4;

	ctx->indent = 0;
	ctx->s = &ctx->stack_states[0];

	ctx->s->current_type = _NI_JSON_TYPE_COMMON;
	ctx->s->current_index = 0;
	ctx->s->nip = "";
	return 0;
}

static inline void ni_json_close(void *priv)
{
	struct ni_json_priv *ctx = priv;
	(void)ctx;

	printf("\n");
}

static inline void ni_json_section(void *priv, int item_index)
{
	struct ni_json_priv *ctx = priv;
	struct ni_json_item *item = ctx->ip.get(ctx->ip.items, item_index);
	const char *pfx = ctx->s->nip;
	int ct;

	if (item->key == NULL && item->flags == 0)
		return;

	ctx->s->current_index++;
	ct = ctx->s->current_type;

	ctx->s++;
	ctx->s->current_index = 0;
	ctx->s->current_type = _NI_JSON_TYPE_OBJECT;

	ctx->s->nip = ctx->use_nls ? "\n" : "";
	if (ct != _NI_JSON_TYPE_OBJECT) {
		printf("%s%*s%s{%s", pfx, ctx->indent * ctx->indent_size, "", "", "");
	} else {
		printf("%s%*s\"%s\": {%s", pfx, ctx->indent * ctx->indent_size, "", item->key, "");
	}

	ctx->indent++;
}

static inline void ni_json_endsection(void *priv, int item_index)
{
	struct ni_json_priv *ctx = priv;
	struct ni_json_item *item = ctx->ip.get(ctx->ip.items, item_index);
	(void) item;

	if (item->key == NULL && item->flags == 0)
		return;

	ctx->indent--;
	if (ctx->s->current_index == 0)
		printf("}");
	else
		printf("%s%*s}", ctx->nls, ctx->indent * ctx->indent_size, "");

	ctx->s--;

	if (ctx->s->current_type == _NI_JSON_TYPE_ARRAY || ctx->s->current_type == _NI_JSON_TYPE_OBJECT)
		ctx->s->nip = ctx->use_nls ? ",\n" : ", ";
}

static inline void ni_json_list(void *priv, int item_index)
{
	struct ni_json_priv *ctx = priv;
	struct ni_json_item *item = ctx->ip.get(ctx->ip.items, item_index);
	const char *pfx = ctx->s->nip;
	int ct;

	if (item->key == NULL && item->flags == 0)
		return;

	ctx->s->current_index++;
	ct = ctx->s->current_type;

	ctx->s++;
	ctx->s->current_index = 0;
	ctx->s->current_type = _NI_JSON_TYPE_ARRAY;

	ctx->s->nip = ctx->use_nls ? "\n" : "";
	if (ct != _NI_JSON_TYPE_OBJECT || item->key == NULL) {
		printf("%s%*s%s[%s", pfx, ctx->indent * ctx->indent_size, "", "", "");
	} else {
		printf("%s%*s\"%s\": [%s", pfx, ctx->indent * ctx->indent_size, "", item->key, "");
	}
	ctx->indent++;
}

static inline void ni_json_endlist(void*priv, int item_index)
{
	struct ni_json_priv *ctx = priv;
	struct ni_json_item *item = ctx->ip.get(ctx->ip.items, item_index);

	if (item->key == NULL && item->flags == 0)
		return;

	ctx->indent--;
	if (ctx->s->current_index == 0)
		printf("]");
	else
		printf("%s%*s]", ctx->nls, ctx->indent * ctx->indent_size, "");
	ctx->s--;

	if (ctx->s->current_type == _NI_JSON_TYPE_ARRAY || ctx->s->current_type == _NI_JSON_TYPE_OBJECT)
		ctx->s->nip = ctx->use_nls ? ",\n" : ", ";
}

static inline int ni_json_prelude(void *priv, int item_index, void **cb_priv)
{
	struct ni_json_priv *ctx = priv;
	struct ni_json_item *item = ctx->ip.get(ctx->ip.items, item_index);

	ctx->label = item->key;
	ctx->ind = ctx->use_nls ? ctx->indent * ctx->indent_size : 0;
	*cb_priv = &ctx->cbp;

	if (ctx->label == NULL && item->flags == 0) {
		return -1;
	}
	ctx->cbp.f = stdout;
	ctx->cbp.decim = ni_json_f_decim_dec(item->flags);

	if (ctx->s->current_type != _NI_JSON_TYPE_ARRAY)
		printf("%s%*s\"%s\": ", ctx->s->nip, ctx->ind, "", ctx->label);
	else
		printf("%s%*s%s", ctx->s->nip, ctx->ind, "", "");

	ctx->s->nip = ctx->use_nls ? ",\n" : ", ";
	return 0;
}

static inline void ni_json_postlude(void *priv, int item_index, int value_length)
{
	struct ni_json_priv *ctx = priv;
	struct ni_json_item *item = ctx->ip.get(ctx->ip.items, item_index);
	(void) value_length;
	(void) item;

	ctx->s->current_index++;
}

#endif // __NETCOPE_NI_JSON__
