/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Library for unified formated output of normalized items: user readable output plugin
 *
 * Copyright (C) 2025 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef __NETCOPE_NI_USER__
#define __NETCOPE_NI_USER__

#define ni_user_f_align(x)      _ni_enc_int8_t(x, 32)
#define ni_user_f_align_dec(x)  _ni_dec_int8_t(x, 32)

#define ni_user_f_decim(x)      _ni_enc_int8_t(x, 40)
#define ni_user_f_decim_dec(x)  _ni_dec_int8_t(x, 40)

#define ni_user_f_width(x)      _ni_enc_int8_t(x, 48)
#define ni_user_f_width_dec(x)  _ni_dec_int8_t(x, 48)

#define NI_USER_ITEM_F_NO_NEWLINE       (1 << 0)
#define NI_USER_ITEM_F_NO_DELIMITER     (1 << 1)
#define NI_USER_ITEM_F_NO_ALIGN         (1 << 2)
#define NI_USER_ITEM_F_NO_VALUE         (1 << 3)
#define NI_USER_ITEM_F_SEC_LABEL        (1 << 4)
#define NI_USER_LIST_F_ENDLINE          (1 << 5)
#define NI_USER_LIST_F_NO_LABEL         (1 << 6)
#define NI_USER_LIST_F_NO_VALUE         NI_USER_ITEM_F_NO_VALUE

struct ni_user_item {
	const char *label;
	unsigned long long flags;
	const char *vp; /* Value prefix */
	const char *vs; /* Value suffix */
};

#define ni_user_n \
	{NULL, 0, NULL, NULL}

/* label only */
#define ni_user_l(label) \
	{label, 0, NULL, NULL}

#define ni_user_f(key, flags) \
	{key, (flags), NULL, NULL}

#define ni_user_v(key, flags, vp, vs) \
	{key, (flags), vp, vs}


/* Internals */

struct ni_user_stack_state {
	struct ni_user_item *item;
	int seg;                /* 0, 1=section (header), 2=list */
	int current_index;      /* Current index in array / dict */
};

struct ni_user_cbp {
	FILE *f;
	int8_t align;
	int8_t width;
	int8_t decim;
};


struct ni_user_priv {
	struct ni_user_cbp cbp; /* data for item callback */
	struct ni_common_init_params ip;

	int clo;        	/* current line offset*/

	const char *vs;         /* value suffix */
	const char *nl;
	char *buffer;
	size_t buffer_size;
	FILE *f;                /* current output */
	FILE *fb;               /* buffer file */
	FILE *fo;               /* stdout */

	struct ni_user_stack_state *s;
	struct ni_user_stack_state stack_states[];
};

static const char *_ni_user_helper_dash =
	"-----------------------------------------------------------"
	"-----------------------------------------------------------";

static inline int ni_user_init(void *init_params, struct ni_common_init_params *cip, void **ppriv)
{
	struct ni_user_priv * ctx;
	const int stack_size = 16;
	(void) init_params;

	if(cip->get == NULL || cip->items == NULL)
		return -EINVAL;

	ctx = malloc(sizeof(struct ni_user_priv) + sizeof(struct ni_user_stack_state) * stack_size + 4096);
	if (ctx == NULL)
		return -ENOMEM;

	*ppriv = ctx;
	ctx->ip = *cip;

	ctx->clo = 0;

	ctx->buffer = (void*) (ctx + 1);
	ctx->buffer += sizeof(struct ni_user_stack_state) * stack_size;

	ctx->fb = open_memstream(&ctx->buffer, &ctx->buffer_size);
	ctx->fo = stdout;

	ctx->f = ctx->fo;

	ctx->s = &ctx->stack_states[0];
	ctx->s->item = NULL;
	ctx->s->seg = 0;
	ctx->s->current_index = 0;

	return 0;
}

static inline void ni_user_flush_f(struct ni_user_priv *ctx)
{
	int db, de;
	const char *sp = " ";

	long pos = ftell(ctx->fb);
	if (pos) {
		db = 55 - pos - 2 - 4;
		de = 4;

		fflush(ctx->fb);
		ctx->buffer[pos] = 0;
		fprintf(ctx->fo, "%.*s%s%s%s%.*s\n", db, _ni_user_helper_dash, sp, ctx->buffer, sp, de, _ni_user_helper_dash);
		fflush(ctx->fo);
		fseek(ctx->fb, 0, SEEK_SET);

		ctx->clo = 0;
	}

	ctx->f = ctx->fo;
}

static inline void ni_user_section(void *priv, int item_index)
{
	struct ni_user_priv *ctx = priv;
	struct ni_user_item *item = ctx->ip.get(ctx->ip.items, item_index);

	const char *between = "";
	if (ctx->s->current_index && ctx->s->seg == 2 && ctx->s->item && ctx->s->item->vp) {
		between = ctx->s->item->vp;
		fprintf(ctx->f, "%s", between);
	}

	ctx->s->current_index++;

	ctx->s++;
	ctx->s->item = item;
	ctx->s->current_index = 0;
	ctx->s->seg = 0;

	if (item->label == NULL)
		return;

	ni_user_flush_f(ctx);

	ctx->f = ctx->fb;
	ctx->clo += fprintf(ctx->f, "%s", item->label);
}

static inline void ni_user_endsection(void *priv, int item_index)
{
	struct ni_user_priv *ctx = priv;
	struct ni_user_item *item = ctx->ip.get(ctx->ip.items, item_index);

	(void)item;
	ni_user_flush_f(ctx);

	ctx->s--;
}

static inline void ni_user_list(void *priv, int item_index)
{
	struct ni_user_priv *ctx = priv;
	struct ni_user_item *item = ctx->ip.get(ctx->ip.items, item_index);

	if (item->flags & NI_USER_LIST_F_NO_LABEL)
		return;

	ctx->s->current_index++;
	ctx->s++;
	ctx->s->current_index = 0;
	ctx->s->seg = 2;
	ctx->s->item = item;

	if (item->label) {
		ctx->clo += fprintf(ctx->f, "%s", item->label);

		if ((item->flags & NI_USER_LIST_F_NO_VALUE)) {
			const char *delim = ":";
			int align = 27 - ctx->clo;
			ctx->clo += fprintf(ctx->f, "%*s%s ", align, "", delim);
		} else {
			fprintf(ctx->f, "\n");
			ctx->clo = 0;
		}
	}
}

static inline void ni_user_endlist(void *priv, int item_index)
{
	struct ni_user_priv *ctx = priv;
	struct ni_user_item *item = ctx->ip.get(ctx->ip.items, item_index);

	if (item->flags & NI_USER_LIST_F_NO_LABEL) {
		fseek(ctx->fb, 0, SEEK_SET);
		return;
	}

	ni_user_flush_f(ctx);
	if (item->flags & NI_USER_LIST_F_ENDLINE) {
		fprintf(ctx->f, "\n");
		ctx->clo = 0;
	}

	ctx->s--;
}

static inline int ni_user_prelude(void *priv, int item_index, void **cb_priv)
{
	struct ni_user_priv *ctx = priv;
	struct ni_user_item *item = ctx->ip.get(ctx->ip.items, item_index);
	int ret;
	int align = 0;
	const char *delim;
	const char *label;
	const char *vp; /* value prefix */

	/* Alignment:
	 * 0 : align value (if is default for current mode) to default (first) column [default]
	 * -1: do not align value
	 * 1 : align value to first column
	 */

	*cb_priv = &ctx->cbp;

	ctx->cbp.align = ni_user_f_align_dec(item->flags);
	ctx->cbp.decim = ni_user_f_decim_dec(item->flags);
	ctx->cbp.width = ni_user_f_width_dec(item->flags);

	align = ctx->cbp.align;

	ctx->nl = "\n";
	label = item->label;

	delim = ": ";

	if (item->flags & NI_USER_ITEM_F_NO_NEWLINE)
		ctx->nl = "";
	if (item->flags & NI_USER_ITEM_F_NO_DELIMITER)
		delim = "";
	if (item->flags & NI_USER_ITEM_F_NO_ALIGN)
		align = -1;

	if (item->flags == 0 && item->label == NULL)
		return -1;

	if (!(item->flags & NI_USER_ITEM_F_SEC_LABEL)) {
		ni_user_flush_f(ctx);
	}
	ctx->cbp.f = ctx->f;

	const char *between = "";
	if (ctx->s->current_index && ctx->s->seg == 2 && ctx->s->item && ctx->s->item->vp) {
		between = ctx->s->item->vp;
		fprintf(ctx->f, "%s", between);
	}
	int xalign = ni_user_f_align_dec(ctx->s->item->flags);
	if (xalign && ctx->s->seg == 2 && ctx->s->current_index % xalign == 0 && ctx->s->current_index != 0) {
		//align = 27 - ctx->clo;
		fprintf(ctx->f, "\n%*s",29, "");
		ctx->clo = 0;
	}
	ctx->s->current_index++;

	vp = item->vp ? item->vp : "";
	ctx->vs = item->vs ? item->vs : "";
	label = item->label ? item->label : "";

	ret = fprintf(ctx->f, "%s", label);
	ctx->clo += ret;

	if (align == 0)
		align = 27 - ctx->clo;
	if (align == -1)
		align = 0;

	ret = fprintf(ctx->f, "%*s%s%s", align, "", delim, vp);
	ctx->clo += ret;

	if (item->flags & NI_USER_ITEM_F_NO_VALUE)
		return -2;

	return 0;
}

static inline void ni_user_postlude(void *priv, int item_index, int value_length)
{
	struct ni_user_priv *ctx = priv;
	struct ni_user_item *item = ctx->ip.get(ctx->ip.items, item_index);

	int skip_vs = 0; //item->flags & 8;
	int ret;

	(void) item;

	ret = fprintf(ctx->f, "%s%s", skip_vs ? "" : ctx->vs, ctx->nl);

	ctx->clo += ret + value_length;

	if (strcmp(ctx->nl, "\n") == 0) {
		ctx->clo = 0;
	}
}


#endif // __NETCOPE_NI_USER__
