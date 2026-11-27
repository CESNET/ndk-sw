#ifndef PYNFB_EXT_PRIV_H
#define PYNFB_EXT_PRIV_H

#include <stddef.h>

#include <nfb/nfb.h>
#include <nfb/ext.h>

/* not in libnfb older than the component locks: then the extension has no locks */
#pragma weak nfb_ext_get_priv

/* the priv of the opened device of the component (the lock owner), NULL with an older libnfb */
static inline void *pynfb_comp_dev_priv(const struct nfb_comp *comp)
{
	if (nfb_ext_get_priv == NULL)
		return NULL;
	return nfb_ext_get_priv(nfb_comp_get_device(comp));
}

#endif
