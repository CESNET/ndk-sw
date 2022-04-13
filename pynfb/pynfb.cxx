/*
 *  libnfb - base module
 *
 *  Copyright (C) CESNET, 2018
 *  Author(s):
 *    Martin Spinler <spinler@cesnet.cz>
 *
 *  SPDX-License-Identifier: GPL-2.0
 */

#include <iostream>

extern "C" {
#include <libfdt.h>
}
#include <nfb/nfb.h>

#include "pynfb.h"

struct nfb_device **nfb_new(const char *path)
{
	struct nfb_device **dev = new struct nfb_device *;
	*dev = nfb_open(path);
	if (*dev == nullptr) {
		delete dev;
		throw NFBDeviceOpenException();
	}
	return dev;
}

void nfb_delete(struct nfb_device **dev)
{
	nfb_close(*dev);
	delete dev;
}

struct nfb_comp **nfb_comp_new(struct nfb_device *dev, int fdtOffset)
{
	struct nfb_comp **comp = new struct nfb_comp *;

	*comp = nfb_comp_open(dev, fdtOffset);
	if (*comp == nullptr) {
		delete comp;
		throw NFBCompOpenException();
	}
	return comp;
}

void nfb_comp_delete(struct nfb_comp **comp)
{
	nfb_comp_close(*comp);
	delete comp;
}

NFBDevice::NFBDevice()
{
	m_dev = std::shared_ptr<struct nfb_device*>(nfb_new(NFB_DEFAULT_DEV_PATH), nfb_delete);
}

NFBDevice::NFBDevice(const char *path)
{
	m_dev = std::shared_ptr<struct nfb_device*>(nfb_new(path), nfb_delete);
}

NFBDevice::~NFBDevice()
{
}

NFBComp NFBDevice::getComp(int fdtOffset)
{
	return NFBComp(*this, fdtOffset);
}

NFBComp NFBDevice::getComp(const char *compatible, int index)
{
	return getComp(nfb_comp_find(*m_dev, compatible, index));
}

NFBComp NFBDevice::getComp(const char *path)
{
	return getComp(fdt_path_offset(nfb_get_fdt(*m_dev), path));
}

NFBComp::NFBComp(const NFBComp &comp)
{
	m_dev = comp.m_dev;
	m_comp = comp.m_comp;
}

NFBComp::NFBComp(const NFBDevice &dev, int fdtOffset)
{
	m_dev = dev.m_dev;
	m_comp = std::shared_ptr<struct nfb_comp *>(nfb_comp_new(*m_dev, fdtOffset), nfb_comp_delete);
}

NFBComp::~NFBComp()
{
}

ssize_t NFBComp::read(void *buf, size_t nbyte, unsigned offset)
{
	return nfb_comp_read(*m_comp, buf, nbyte, offset);
}

ssize_t NFBComp::write(const void *buf, size_t nbyte, unsigned offset)
{
	return nfb_comp_write(*m_comp, buf, nbyte, offset);
}
