#include <libfdt.h>
#include <Python.h>

#include <nfb/nfb.h>
#include <nfb/ext.h>

#include <nfb/ndp.h>

#include "shim_api.h"

struct libnfb_ext_abi_version libnfb_ext_abi_version = libnfb_ext_abi_version_current;

int libnfb_ext_get_ops(const char *devname, struct libnfb_ext_ops *ops)
{
	int ret;
	PyGILState_STATE gstate = PyGILState_Ensure();

	ret = import_shim();
	if (ret) {
		ret = -EBADFD;
		goto out;
	}

	ret = pynfb_ext_get_ops(devname, ops);
out:
	PyGILState_Release(gstate);
	return ret;
}
