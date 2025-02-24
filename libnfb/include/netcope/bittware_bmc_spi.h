/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Network component library - SPI controller for BittWare BMC IP
 *
 * Copyright (C) 2025 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef BW_BMC_SPI_H
#define BW_BMC_SPI_H

#ifdef __cplusplus
extern "C" {
#endif


/* ~~~~[ DATA TYPES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
struct nc_bw_bmc {
	unsigned char * buffer;
	unsigned int buffer_len;
	unsigned int recv_len;
	unsigned int pos;
	int throttle_write: 1;
};


/* ~~~~[ PROTOTYPES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
static inline struct nc_bw_bmc *nc_bw_bmc_open(const struct nfb_device *dev, int fdt_offset, unsigned char*buffer, unsigned int len);
static inline void nc_bw_bmc_close(struct nc_bw_bmc *spi);
static inline int nc_bw_bmc_send_mctp(struct nc_bw_bmc *spi);
static inline int nc_bw_bmc_send_mctp_ext(struct nc_bw_bmc *spi, const unsigned char *bytes, int len, int last, int wait_for_status);
static inline int nc_bw_bmc_receive_mctp_ext(struct nc_bw_bmc *spi, unsigned char* bytes, unsigned int len, unsigned int *received);


/* ~~~~[ REGISTERS ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
#define NC_BW_BMC_SPI_WR_CSR    0x0000
#define NC_BW_BMC_SPI_RD_CSR    0x0004
#define NC_BW_BMC_SPI_WR_FIFO   0x0008
#define NC_BW_BMC_SPI_RD_FIFO   0x000C
#define NC_BW_BMC_SPI_SYS_CSR   0x0010


#define COMP_NETCOPE_BW_BMC_SPI "bittware,bmc"

static inline void nc_bw_bmc_write32(struct nfb_comp *comp, uint32_t offset, uint32_t val)
{
	struct nc_bw_bmc * spi = (struct nc_bw_bmc*) nfb_comp_to_user(comp);
	nfb_comp_write32(comp, offset, val);
	if (spi->throttle_write)
		nfb_comp_read32(comp, NC_BW_BMC_SPI_WR_FIFO);
}

/* ~~~~[ IMPLEMENTATION ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
static inline struct nc_bw_bmc *nc_bw_bmc_open(const struct nfb_device *dev, int fdt_offset, unsigned char*buffer, unsigned int len)
{
	int size;
	struct nc_bw_bmc *spi;
	struct nfb_comp *comp;
	const void *prop;
	const void *fdt = nfb_get_fdt(dev);
	int retries = 0;

	uint32_t status;
	if (len == 0)
		return NULL;
	if (fdt_node_check_compatible(fdt, fdt_offset, COMP_NETCOPE_BW_BMC_SPI))
		return NULL;

	size = sizeof(*spi);
	if (buffer == NULL)
		size += len;

	comp = nfb_comp_open_ext(dev, fdt_offset, size);
	if (!comp)
		return NULL;

	spi = (struct nc_bw_bmc*) nfb_comp_to_user(comp);
	spi->buffer = buffer ? buffer : (unsigned char*) (spi + 1);
	spi->buffer_len = len;
	spi->pos = 0;
	spi->recv_len = 0;

	prop = fdt_getprop(fdt, fdt_offset, "throttle_write", NULL);
	spi->throttle_write = prop ? 1 : 0;

	retries = 0;
	status = nfb_comp_read32(comp, NC_BW_BMC_SPI_RD_FIFO);
	while (status != 0xdeadbeef && ++retries < 1000) {
#ifdef __KERNEL__
	        udelay(1000);
#else
	        usleep(1000);
#endif
		status = nfb_comp_read32(comp, NC_BW_BMC_SPI_RD_FIFO);
	}

	retries = 0;
	status = nfb_comp_read32(comp, NC_BW_BMC_SPI_SYS_CSR);
	while ((status & (1 << 1)) == 0 && ++retries < 1000) {
#ifdef __KERNEL__
	        udelay(1000);
#else
	        usleep(1000);
#endif
		status = nfb_comp_read32(comp, NC_BW_BMC_SPI_SYS_CSR);
	}

	nfb_comp_read32(comp, NC_BW_BMC_SPI_WR_CSR);
	//nc_bw_bmc_write32(comp, NC_BW_BMC_SPI_SYS_CSR, 1 << 0);
	nc_bw_bmc_write32(comp, NC_BW_BMC_SPI_WR_CSR, 1 << 12);
	nc_bw_bmc_write32(comp, NC_BW_BMC_SPI_RD_CSR, 1 << 14);

	return spi;
}

static inline void nc_bw_bmc_close(struct nc_bw_bmc *spi)
{
	nfb_comp_close(nfb_user_to_comp(spi));
}

/* send raw mctp: header must be in bytes already (hdr, dst, src, flags) */
static inline int nc_bw_bmc_send_mctp_ext(struct nc_bw_bmc *spi, const unsigned char *bytes, int len, int last, int wait_for_status)
{
	int retries = 0;
	struct nfb_comp * comp = nfb_user_to_comp(spi);
	int i;
	uint32_t status;

	status = nfb_comp_read32(comp, NC_BW_BMC_SPI_RD_CSR);
	if (status & 1) {
		/* Previous command doesn't red the data */
		nc_bw_bmc_write32(comp, NC_BW_BMC_SPI_RD_CSR, 1 << 14); /* Read FIFO Reset */
	}

	if (len == 0)
		return 0;

	nc_bw_bmc_write32(comp, NC_BW_BMC_SPI_WR_FIFO, 0x11); /* MCTP command */

	for (i = 0; i < len - 1; i++) {
		nc_bw_bmc_write32(comp, NC_BW_BMC_SPI_WR_FIFO, bytes[i]);
	}
	status = bytes[i];
	status |= (last ? 0x100 : 0);

	nfb_comp_write32(comp, NC_BW_BMC_SPI_WR_FIFO, status);
	if (!wait_for_status)
		return 0;
	if (spi->throttle_write)
		nfb_comp_read32(comp, NC_BW_BMC_SPI_WR_FIFO);

	status = nfb_comp_read32(comp, NC_BW_BMC_SPI_WR_CSR);
	if (status & (1 << 13)) {
		/* Write FIFO Overflow */
		return -ENOBUFS;
	}
	while ((status & (1 << 0)) == 0 && ++retries < 100000) {
#ifdef __KERNEL__
	        udelay(10);
#else
	        usleep(10);
#endif
		status = nfb_comp_read32(comp, NC_BW_BMC_SPI_WR_CSR);
	}
	if (retries >= 100000)
		return -EPIPE;

        status = status >> 24;
	if (status != 0x20) {
		/* 0x20: MCTP Success
		 * 0x21: MCTP Invalid Length
		 * 0x22: MCTP Invalid Source
		 * 0x23: MCTP Invalid Message
		 * 0xF0: Unrecognized Command
		 */
		return -EOPNOTSUPP;
	}

	return 0;
}

static inline int nc_bw_bmc_send_mctp(struct nc_bw_bmc *spi)
{
	return nc_bw_bmc_send_mctp_ext(spi, spi->buffer, spi->pos, 1, 1);
}

static inline int nc_bw_bmc_min(int a, int b) {
    return a < b ? a : b;
}

static inline int nc_bw_bmc_receive_mctp_ext(struct nc_bw_bmc *spi, unsigned char* data, unsigned int len, unsigned int *received)
{
	struct nfb_comp * comp = nfb_user_to_comp(spi);

	unsigned int i;
	int retries = 0;
	uint32_t status;
	uint32_t bytes;

	*received = 0;

	status = nfb_comp_read32(comp, NC_BW_BMC_SPI_RD_CSR);
	while ((status & 1) == 0 && ++retries < 100000) {
#ifdef __KERNEL__
	        udelay(10);
#else
	        usleep(10);
#endif
		status = nfb_comp_read32(comp, NC_BW_BMC_SPI_RD_CSR);
	}
	if (retries >= 100000)
		return -EPIPE;

	bytes = (status >> 2) & 0x7FF;
	if (bytes == 0)
		return -EBADF;

	/* Output data buffer must be sufficient */
	if (--bytes > len)
		return bytes;

	status = nfb_comp_read32(comp, NC_BW_BMC_SPI_RD_FIFO);
	if ((status & 0xFF) != 0x11) {
		nc_bw_bmc_write32(comp, NC_BW_BMC_SPI_RD_CSR, 1 << 14); /* Read FIFO Reset */
		nc_bw_bmc_write32(comp, NC_BW_BMC_SPI_RD_CSR, 1 << 1); /* Read Transfer Complete */
		return -EBADF;
	}

	if (status & 0x100)
		return bytes;

	for (i = 0; i < len; i++) {
		status = nfb_comp_read32(comp, NC_BW_BMC_SPI_RD_FIFO);
		if (status == 0xDEADBEEF) {
			nc_bw_bmc_write32(comp, NC_BW_BMC_SPI_RD_CSR, 1 << 1); /* Read Transfer Complete */
			return -EBADF;
		}

		data[i] = status & 0xFF;
		(*received)++;

		if (status & 0x100)
			break;
	}

	/* Data continues */
	if (!(status & 0x100) || bytes > *received)
		return bytes - *received;

	nc_bw_bmc_write32(comp, NC_BW_BMC_SPI_RD_CSR, 1 << 1); /* Read Transfer Complete */

	return 0;
}

static inline int nc_bw_bmc_receive_mctp(struct nc_bw_bmc *spi)
{
	return nc_bw_bmc_receive_mctp_ext(spi, spi->buffer, spi->buffer_len, &spi->recv_len);
}

static inline int nc_bw_bmc_mctp_header_default(struct nc_bw_bmc *spi)
{
	int i = 0;
	if (spi->buffer_len < 4)
		return -ENOMEM;
	spi->buffer[i++] = 0x01; /* hdr */
	spi->buffer[i++] = 0x00; /* dst */
	spi->buffer[i++] = 0x69; /* src */
	spi->buffer[i++] = 0xc8; /* 7:0 = som, eom, seq:2, to, tag:3 */
	spi->pos = i;
	return 0;
}

static inline int nc_bw_bmc_pldm_vndr(struct nc_bw_bmc *spi, int vndr_type)
{
	int i = spi->pos;
	if (spi->buffer_len < spi->pos + 4)
		return -ENOMEM;
	spi->buffer[i++] = 0x01; /* MCTP type: PLDM */
	spi->buffer[i++] = 0x80;
	spi->buffer[i++] = 0x3f;
	spi->buffer[i++] = vndr_type;
	spi->pos = i;
	return 0;
}

static inline int nc_bw_bmc_push(struct nc_bw_bmc *spi, const void *val, unsigned len)
{
	if (spi->buffer_len < spi->pos + len)
		return -ENOMEM;
	memcpy(spi->buffer + spi->pos, val, len);
	spi->pos += len;
	return 0;
}

static inline int nc_bw_bmc_push_uint8(struct nc_bw_bmc *spi, uint8_t val)
{
	return nc_bw_bmc_push(spi, &val, sizeof(val));
}

static inline int nc_bw_bmc_push_uint16(struct nc_bw_bmc *spi, uint16_t val)
{
	/* TODO: add support for big-endian */
	return nc_bw_bmc_push(spi, &val, sizeof(val));
}

static inline int nc_bw_bmc_push_uint32(struct nc_bw_bmc *spi, uint32_t val)
{
	/* TODO: add support for big-endian */
	return nc_bw_bmc_push(spi, &val, sizeof(val));
}

static inline int nc_bw_bmc_pop_mctp_header(struct nc_bw_bmc *spi)
{
	if (spi->recv_len < 4)
		return -ENOMEM;
	spi->pos = 4;
	return 0;
}

static inline int nc_bw_bmc_pop_pldm_header(struct nc_bw_bmc *spi)
{
	if (spi->recv_len < spi->pos + 4)
		return -ENOMEM;
	spi->pos += 4;
	return 0;
}

static inline int nc_bw_bmc_pop(struct nc_bw_bmc *spi, void *buffer, unsigned len)
{
	if (spi->recv_len < spi->pos + len)
		return -ENOMEM;
	if (buffer)
		memcpy(buffer, spi->buffer + spi->pos, len);
	spi->pos += len;
	return 0;
}

static inline int nc_bw_bmc_pop_uint8(struct nc_bw_bmc *spi, uint8_t *val)
{
	return nc_bw_bmc_pop(spi, val, sizeof(*val));
}

static inline int nc_bw_bmc_pop_uint32(struct nc_bw_bmc *spi, uint32_t *val)
{
	/* TODO: add support for big-endian */
	return nc_bw_bmc_pop(spi, val, sizeof(*val));
}

static inline int nc_bw_bmc_download_file(struct nc_bw_bmc *spi, const char *path, unsigned char *buffer, unsigned int len)
{
	int ret = 0;
	const uint32_t data_length = 512;

	uint32_t offset = 0;

	uint32_t out_offset = -1;
	uint32_t response_count;
	uint8_t cc;

	do {
		ret |= nc_bw_bmc_mctp_header_default(spi);
		ret |= nc_bw_bmc_pldm_vndr(spi, 0x14);

		ret |= nc_bw_bmc_push_uint32(spi, offset);
		ret |= nc_bw_bmc_push_uint32(spi, data_length);
		ret |= nc_bw_bmc_push_uint32(spi, strlen(path));
		ret |= nc_bw_bmc_push(spi, path, strlen(path));

		if (ret)
			return ret;

		ret = nc_bw_bmc_send_mctp(spi);
		if (ret)
			return ret;

		ret = nc_bw_bmc_receive_mctp(spi);
		if (ret)
			return ret;

		ret |= nc_bw_bmc_pop_mctp_header(spi);
		ret |= nc_bw_bmc_pop_pldm_header(spi);
		ret |= nc_bw_bmc_pop_uint8(spi, &cc);
		ret |= nc_bw_bmc_pop_uint32(spi, &out_offset);
		ret |= nc_bw_bmc_pop_uint32(spi, &response_count);

		ret |= nc_bw_bmc_pop(spi, buffer + offset, nc_bw_bmc_min(len - offset, response_count));

		if (ret)
			return ret;

		if (cc != 0)
			return -ECANCELED;

		offset += response_count;
	} while (response_count == data_length);

	return offset;
}

static inline int nc_bw_bmc_file_unlink(struct nc_bw_bmc *spi, const char *path)
{
	int ret = 0;
	uint8_t cc;

	ret |= nc_bw_bmc_mctp_header_default(spi);
	ret |= nc_bw_bmc_pldm_vndr(spi, 0x18);

	ret |= nc_bw_bmc_push_uint16(spi, strlen(path));
	ret |= nc_bw_bmc_push(spi, path, strlen(path));

	if (ret)
		return ret;

	ret = nc_bw_bmc_send_mctp(spi);
	if (ret)
		return ret;

	ret = nc_bw_bmc_receive_mctp(spi);
	if (ret)
		return ret;

	ret |= nc_bw_bmc_pop_mctp_header(spi);
	ret |= nc_bw_bmc_pop_pldm_header(spi);
	ret |= nc_bw_bmc_pop_uint8(spi, &cc);

	if (ret)
		return ret;

	if (cc != 0)
		return -ECANCELED;

	return 0;
}

static inline int nc_bw_bmc_file_move(struct nc_bw_bmc *spi, const char *src, const char *dst)
{
	int ret = 0;
	uint8_t cc = 0;

	ret = 0;
	ret |= nc_bw_bmc_mctp_header_default(spi);
	ret |= nc_bw_bmc_pldm_vndr(spi, 0x19);

	ret |= nc_bw_bmc_push_uint16(spi, strlen(src));
	ret |= nc_bw_bmc_push_uint16(spi, strlen(dst));
	ret |= nc_bw_bmc_push(spi, src, strlen(src));
	ret |= nc_bw_bmc_push(spi, dst, strlen(dst));
	if (ret)
		return -1;

	ret = nc_bw_bmc_send_mctp(spi);
	if (ret)
		return -1;

	ret = nc_bw_bmc_receive_mctp(spi);
	if (ret)
		return -1;

	ret |= nc_bw_bmc_pop_mctp_header(spi);
	ret |= nc_bw_bmc_pop_pldm_header(spi);
	ret |= nc_bw_bmc_pop_uint8(spi, &cc);

	if (ret)
		return -1;

	if (cc != 0)
		return -ECANCELED;

	return 0;
}

static inline int nc_bw_bmc_file_upload(struct nc_bw_bmc *spi, const char *path, const void *data, unsigned len)
{
	int ret = 0;
	uint32_t data_length = 512;

	uint32_t offset = 0;

	uint8_t cc = 0;

	do {
		data_length = nc_bw_bmc_min(len, data_length);

		ret |= nc_bw_bmc_mctp_header_default(spi);
		ret |= nc_bw_bmc_pldm_vndr(spi, 0x13);

		ret |= nc_bw_bmc_push_uint32(spi, strlen(path));
		ret |= nc_bw_bmc_push_uint32(spi, offset);
		ret |= nc_bw_bmc_push_uint32(spi, data_length);
		ret |= nc_bw_bmc_push(spi, path, strlen(path));

		ret |= nc_bw_bmc_push(spi, (unsigned char*)data + offset, data_length);

		ret = nc_bw_bmc_send_mctp(spi);
		if (ret)
			return ret;

		ret = nc_bw_bmc_receive_mctp(spi);
		if (ret)
			return ret;

		ret |= nc_bw_bmc_pop_mctp_header(spi);
		ret |= nc_bw_bmc_pop_pldm_header(spi);
		ret |= nc_bw_bmc_pop_uint8(spi, &cc);

		if (ret)
			return ret;

		if (cc != 0)
			return -ECANCELED;

		offset += data_length;
	} while (offset != len);

	return 0;
}

static inline int nc_bw_bmc_fpga_load_ext(struct nc_bw_bmc *spi, const void *data, unsigned len, unsigned flash_offset, void (*cb)(void*, unsigned current_offset), void *cb_priv)
{
	int ret = 0;
	uint32_t data_length = 512;

	uint32_t offset = 0;

	uint8_t cc = 0;
	uint8_t transfer_flag = 0;

	if (cb)
		cb(cb_priv, 0);

	do {
		data_length = nc_bw_bmc_min(len - offset, data_length);

		if (offset == 0) {
			transfer_flag = (len == data_length + offset ? 0x05 : 0x00);
		} else {
			transfer_flag = (len == data_length + offset ? 0x04 : 0x01);
		}
		ret |= nc_bw_bmc_mctp_header_default(spi);
		ret |= nc_bw_bmc_pldm_vndr(spi, 0x17);

		ret |= nc_bw_bmc_push_uint8(spi, transfer_flag);
		ret |= nc_bw_bmc_push_uint32(spi, flash_offset);
		ret |= nc_bw_bmc_push_uint16(spi, data_length);

		ret |= nc_bw_bmc_push(spi, (unsigned char*)data + offset, data_length);

		ret = nc_bw_bmc_send_mctp(spi);
		if (ret)
			return ret;

		ret = nc_bw_bmc_receive_mctp(spi);
		if (ret)
			return ret;

		ret |= nc_bw_bmc_pop_mctp_header(spi);
		ret |= nc_bw_bmc_pop_pldm_header(spi);
		ret |= nc_bw_bmc_pop_uint8(spi, &cc);

		if (ret)
			return ret;

		if (cc != 0)
			return -ECANCELED;

		offset += data_length;
		if (cb)
			cb(cb_priv, offset);
	} while (offset != len);

	return 0;
}

int nc_bw_bmc_send_reload(struct nc_bw_bmc * spi, const char *filename)
{
	int ret = 0;
	int offset = 0;
	static const char *target_path = "/fpga/load/name";

	ret |= nc_bw_bmc_mctp_header_default(spi);
	ret |= nc_bw_bmc_pldm_vndr(spi, 0x13);

	ret |= nc_bw_bmc_push_uint32(spi, strlen(target_path));
	ret |= nc_bw_bmc_push_uint32(spi, offset);
	ret |= nc_bw_bmc_push_uint32(spi, strlen(filename) + 1);

	ret |= nc_bw_bmc_push(spi, target_path, strlen(target_path));
	ret |= nc_bw_bmc_push(spi, filename, strlen(filename) + 1);

	nc_bw_bmc_send_mctp_ext(spi, spi->buffer, spi->pos, 1, 0);
	return 0;
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* BW_BMC_SPI_H */
