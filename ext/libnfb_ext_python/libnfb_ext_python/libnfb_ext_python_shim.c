#include <libfdt.h>
#include <Python.h>

#include <nfb/nfb.h>
#include <nfb/ext.h>

#include "libnfb_ext_python_api.h"


struct nfb_pynfb_priv {
	PyObject * nfb;
	void *fdt;
};

struct nfb_pynfb_bus_priv {
	int bus_node;
	int comp_node;
	off_t base;
	struct nfb_pynfb_priv * nfb;

	char *bus_path;
	char *comp_path;
};

static const char *nfb_pynfb_prefix = "pynfb:";

static int nfb_pynfb_open(const char *devname, int oflag, void **priv, void **fdt)
{
	int ret;
	uint64_t addr;
	struct nfb_pynfb_priv *dev;
	const void * cfdt;

	addr = strtoull(devname + strlen(nfb_pynfb_prefix), NULL, 10);

	dev = (struct nfb_pynfb_priv*) malloc(sizeof(struct nfb_pynfb_priv));
	if (dev == NULL) {
		return -ENOMEM;
	}

	dev->nfb = (PyObject*)(void*) addr;
	Py_INCREF(dev->nfb);

	cfdt = nfb_ext_python_get_fdt(dev->nfb);
	if (cfdt == NULL) {
		ret = -ENODEV;
		goto err_fdt;
	}

	ret = fdt_totalsize(cfdt);
	dev->fdt = malloc(ret);
	if (dev->fdt == NULL) {
		ret = -ENOMEM;
		goto err_malloc_fdt;
	}
	memcpy(dev->fdt, cfdt, ret);

	*priv = dev;
	*fdt = dev->fdt;
	return 0;

err_malloc_fdt:
err_fdt:
	Py_DECREF(dev->nfb);
	free(dev);
	return ret;
}

static void nfb_pynfb_close(void *dev_priv)
{

	struct nfb_pynfb_priv *dev = dev_priv;

	Py_DECREF(dev->nfb);
	free(dev);
}

static ssize_t nfb_bus_pynfb_read(void *bus_priv, void *buf, size_t nbyte, off_t offset)
{
	struct nfb_pynfb_bus_priv *bus = (struct nfb_pynfb_bus_priv *)bus_priv;
	return nfb_ext_python_bus_read(bus->nfb->nfb, bus->bus_path, bus->comp_path, buf, nbyte, offset - bus->base);
}

static ssize_t nfb_bus_pynfb_write(void *bus_priv, const void *buf, size_t nbyte, off_t offset)
{
	struct nfb_pynfb_bus_priv *bus = (struct nfb_pynfb_bus_priv *)bus_priv;
	return nfb_ext_python_bus_write(bus->nfb->nfb, bus->bus_path, bus->comp_path, buf, nbyte, offset - bus->base);
}

static int nfb_pynfb_bus_open(void *dev_priv, int bus_node, int comp_node, void ** bus_priv, struct libnfb_bus_ext_ops* ops)
{
	struct nfb_pynfb_priv *dev = (struct nfb_pynfb_priv*) dev_priv;
	struct nfb_pynfb_bus_priv *bus;

	int proplen;
	const fdt32_t *prop;

	prop = (fdt32_t*) fdt_getprop(dev->fdt, comp_node, "reg", &proplen);
	if (proplen != sizeof(*prop) * 2) {
		return -EBADFD;
	}

	bus = (struct nfb_pynfb_bus_priv *) malloc(sizeof(*bus));
	if (bus == NULL)
		return -ENOMEM;

	bus->bus_path = malloc(1024);
	if (bus->bus_path == NULL) {
		free(bus);
		return -ENOMEM;
	}

	bus->comp_path = malloc(1024);
	if (bus->comp_path == NULL) {
		free(bus->bus_path);
		free(bus);
		return -ENOMEM;
	}

	fdt_get_path(dev->fdt, comp_node, bus->comp_path, 1024);
	fdt_get_path(dev->fdt, bus_node, bus->bus_path, 1024);

	bus->nfb = dev;
	bus->base = fdt32_to_cpu(prop[0]);

	ops->read = nfb_bus_pynfb_read;
	ops->write = nfb_bus_pynfb_write;
	*bus_priv = bus;

	return 0;
}

static void nfb_pynfb_bus_close(void *bus_priv)
{
	struct nfb_pynfb_bus_priv *bus = bus_priv;
	free(bus->bus_path);
	free(bus->comp_path);
	free(bus);
}

static int nfb_pynfb_comp_lock(const struct nfb_comp *comp, uint32_t features)
{
	/* TODO */
	return 1;
}

static void nfb_pynfb_comp_unlock(const struct nfb_comp *comp, uint32_t features)
{
	/* TODO */
}

struct libnfb_ext_abi_version libnfb_ext_abi_version = libnfb_ext_abi_version_current;

static struct libnfb_ext_ops nfb_pynfb_ops = {
	.open = nfb_pynfb_open,
	.close = nfb_pynfb_close,
	.bus_open_mi = nfb_pynfb_bus_open,
	.bus_close_mi = nfb_pynfb_bus_close,
	.comp_lock = nfb_pynfb_comp_lock,
	.comp_unlock = nfb_pynfb_comp_unlock,
};

int libnfb_ext_get_ops(const char *devname, struct libnfb_ext_ops *ops)
{
	if (import_libnfb_ext_python())
		return 0;

	if (strncmp(devname, nfb_pynfb_prefix, strlen(nfb_pynfb_prefix)) == 0) {
		*ops = nfb_pynfb_ops;
		return 1;
	} else {
		return 0;
	}
}
