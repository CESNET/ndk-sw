#include <libfdt.h>
#include <Python.h>

#include <nfb/nfb.h>
#include <nfb/ext.h>

#include <nfb/ndp.h>

#include "libnfb_ext_python_api.h"

struct libnfb_ext_abi_version libnfb_ext_abi_version = libnfb_ext_abi_version_current;

int libnfb_ext_get_ops(const char *devname, struct libnfb_ext_ops *ops)
{
	if (import_libnfb_ext_python())
		return 0;

	return pynfb_ext_get_ops(devname, ops);
}
