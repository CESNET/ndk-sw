/*
 *  libnfb - base module header
 *
 *  Copyright (C) CESNET, 2018
 *  Author(s):
 *    Martin Spinler <spinler@cesnet.cz>
 *
 *  SPDX-License-Identifier: GPL-2.0
 */

#ifndef NFB_HPP
#define NFB_HPP

#include <stdint.h>
#include <memory>
#include <nfb/nfb.h>

struct NFBDeviceOpenException {};
struct NFBCompOpenException {};

class NFBDevice;

#define __NFBComp_write(bits) \
inline void write##bits(unsigned offset, uint##bits##_t val) \
{ \
	write(&val, sizeof(val), offset); \
}
#define __NFBComp_read(bits) \
inline uint##bits##_t read##bits(unsigned offset) \
{ \
	uint##bits##_t val = 0; \
	read(&val, sizeof(val), offset); \
	return val; \
}

class NFBComp {
	std::shared_ptr<struct nfb_device*> m_dev;
	std::shared_ptr<struct nfb_comp*> m_comp;

public:
	NFBComp(const NFBComp &comp);
	NFBComp(const NFBDevice &dev, int fdtOffset);

	virtual ~NFBComp();

	ssize_t read (void *buf, size_t nbyte, unsigned offset);
	ssize_t write(const void *buf, size_t nbyte, unsigned offset);

	__NFBComp_read(8)
	__NFBComp_read(16)
	__NFBComp_read(32)
	__NFBComp_read(64)

	__NFBComp_write(8)
	__NFBComp_write(16)
	__NFBComp_write(32)
	__NFBComp_write(64)
};

class NFBDevice {
	std::shared_ptr<struct nfb_device*> m_dev;
	friend class NFBComp;

public:
	NFBDevice();
	NFBDevice(const char *path);
	virtual ~NFBDevice();

	NFBComp getComp(int fdtOffset);
	NFBComp getComp(const char *path);
	NFBComp getComp(const char *compatible, int index);

private:
	void open(const char *path);
	void close();
};

#endif /* NFB_HPP */
