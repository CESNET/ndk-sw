/* SPDX-License-Identifier: GPL-2.0 */
/*
 * sdm.c: SDM Client commands implementation
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Tomas Hak <xhakto01@stud.fit.vutbr.cz>
 */

#include "boot.h"
#include "sdm.h"

/*
 * Mailbox Client address space
 *
 *  -------------------------------------------------------------
 * | OFFSET (word) | R/W |          [32:2]           | [1] | [0] |
 *  -------------------------------------------------------------
 * |      0x0      |  W  |               Cmd FIFO                |
 * |               |     |                                       |
 * |      0x1      |  W  |             Cmd last word             |
 * |               |     |                                       |
 * |      0x2      |  R  |          Cmd FIFO empty space         |
 * |               |     |                                       |
 * |      0x5      |  R  |              Response data            |
 * |               |     |                                       |
 * |      0x6      |  R  | Response FIFO fill level  | EOP | SOP |
 * |               |     |                                       |
 * |      0x8      |  R  |       Interrupt status register       |
 *  -------------------------------------------------------------
 *
 * Command and response header format
 * (header is always the first word sent/received)
 *
 * [31 : 28] ... reserved
 * [27 : 24] ... command ID (response has the same ID in the header)
 * [     23] ... reserved
 * [22 : 12] ... number of words following the header
 * [     11] ... reserved (must be 0)
 * [10 :  0] ... Command Code (Error Code)
 *
 */


// Bit widths and offsets in the command header
#define SDM_MC_ID_WIDTH                      4
#define SDM_MC_ID_OFFSET                    24
#define SDM_MC_CMD_CODE_WIDTH               11
#define SDM_MC_CMD_CODE_OFFSET               0
#define SDM_MC_CMD_LEN_WIDTH                11
#define SDM_MC_CMD_LEN_OFFSET               12

// Mailbox Client IP address space
#define SDM_MC_CMD_FIFO                   0x00
#define SDM_MC_CMD_LAST_WORD              0x04
#define SDM_MC_CMD_FIFO_EMPTY_SPACE       0x08
#define SDM_MC_RESPONSE_FIFO              0x14
#define SDM_MC_RESPONSE_FIFO_FILL_LEVEL   0x18
#define SDM_MC_INTERRUPT_STATUS_REGISTER  0x20

// Mailbox Client IP commands opcodes
// QSPI commands
#define SDM_QSPI_OPEN_OP                  0x32
#define SDM_QSPI_SET_CS_OP                0x34
#define SDM_QSPI_CLOSE_OP                 0x33
#define SDM_QSPI_READ_OP                  0x3A
#define SDM_QSPI_READ_DEVICE_REG_OP       0x35
#define SDM_QSPI_WRITE_OP                 0x39
#define SDM_QSPI_WRITE_DEVICE_REG_OP      0x36
#define SDM_QSPI_ERASE_OP                 0x38
// sensors commands
#define SDM_GET_TEMPERATURE_OP            0x19
// RSU commands
#define SDM_RSU_IMAGE_UPDATE_OP           0x5C

// auxiliary macros
#define WORD_WIDTH                           4
#define WORD_BITS                            2
#define MAX_WORDS                         1024
#define ISR_TIMEOUT                    1000000
#define SDM_COMP_LOCK                 (1 << 0)

struct sdm {
	unsigned cmd_id;
	struct nfb_comp *comp;
	const char *card_name;
	unsigned locked;
};

static inline unsigned int sdm_len_bytes_to_words(size_t bytes)
{
	return (bytes + WORD_WIDTH-1) >> WORD_BITS;
}

struct sdm *sdm_init(struct nfb_device *nfb, int fdt_offset, const char *name)
{
	struct sdm *sdm = kzalloc(sizeof(struct sdm), GFP_KERNEL);
	if (sdm == NULL)
		goto err_sdm_alloc;

	sdm->cmd_id = 0;
	sdm->comp = nfb_comp_open(nfb, fdt_offset);
	if (sdm->comp == NULL)
		goto err_comp_open;

	sdm->card_name = name;
	sdm->locked = 0;

	return sdm;

err_comp_open:
	kfree(sdm);
err_sdm_alloc:
	return NULL;
}

void sdm_free(struct sdm *sdm)
{
	if (sdm != NULL) {
		nfb_comp_close(sdm->comp);
		kfree(sdm);
	}
}

/* @brief Send data to the Secure Device Manager via Mailbox Client IP.
 *
 * Waits for space first, then sends the data.
 * If data buffer is not word aligned, zero padding is added.
 *
 * @param sdm  Pointer to struct sdm.
 * @param data Buffer with data to write.
 * @param len  Length of the buffer (in bytes), set to WORD_WIDTH in case of a single value.
 * @param last The last value in data finishes the whole transaction
 *
 */
static int sdm_send_data(struct sdm *sdm, const void *data, int len, int last)
{
	int retries;
	uint32_t tmp;
	int i;
	unsigned free_space;

	off_t reg = SDM_MC_CMD_FIFO;

	free_space = 0;

	while (len > 0) {
		// wait for available space in command FIFO
		retries = 0;
		while (free_space == 0) {
			free_space = nfb_comp_read32(sdm->comp, SDM_MC_CMD_FIFO_EMPTY_SPACE);
			retries++;
			if (retries > 10000)
				return -EBUSY;
		}

		if (len < WORD_WIDTH) {
			tmp = 0;
			for (i = 0; i < len; i++) {
				tmp |= (uint32_t)((uint8_t*)data)[i] << (i*8);
			}
		} else {
			tmp = *((uint32_t *)data);
		}

		if (len <= WORD_WIDTH && last)
			reg = SDM_MC_CMD_LAST_WORD;

		nfb_comp_write32(sdm->comp, reg, tmp);
		data += WORD_WIDTH;
		len -= WORD_WIDTH;
		free_space--;
	}
	return 0;
}

/* @brief Read data from the Secure Device Manager via Mailbox Client IP.
 *
 * Waits for valid signal first, then reads the data from response fifo.
 * If data buffer is not word aligned, last word is trimmed appropriately.
 *
 * @param sdm  Pointer to struct sdm.
 * @param data Buffer where to put read data (can be NULL in case of reading response header only).
 * @param len  Length of the buffer (in bytes) (can be 0 in case of reading response header only).
 *
 * @return If successful, returns number of bytes read,
 *         if only response header is read then 0 means OK and negative number specifies the error (either timeout or MC error).
 *
 */
static int sdm_get_data(struct sdm *sdm, void *data, uint32_t len)
{
	uint32_t sop = 0;
	uint32_t eop = 0;
	uint32_t tmp = 0;
	uint32_t fill = 0;
	int rlen = -1;
	int read = 0;
	int i = 0;

	// TODO: fix len parameter and its usage as word alignment indicator
	len &= WORD_WIDTH-1;

	// wait for valid data in the response FIFO (LSB of ISR is DATA_VALID bit)
	while (!(nfb_comp_read32(sdm->comp, SDM_MC_INTERRUPT_STATUS_REGISTER) & 0x1)) {
		// add timeout to prevent host from getting stuck in case of an error
		if (i++ >= ISR_TIMEOUT) {
			return -ETIME;
		}
	}

	i = 0;

	do {
		//wait for start of response packet and valid data in response fifo
		while (!fill || !sop || (rlen == 1 && !eop)) {
			tmp = nfb_comp_read32(sdm->comp, SDM_MC_RESPONSE_FIFO_FILL_LEVEL);
			if (!sop) {
				sop = tmp & 0x1;
			}
			eop = tmp & 0x2;
			fill = tmp >> 2;
		}

		// read header
		if (rlen < 0) {
			tmp = nfb_comp_read32(sdm->comp, SDM_MC_RESPONSE_FIFO);
			read = (tmp >> SDM_MC_CMD_CODE_OFFSET) & ((1 << SDM_MC_CMD_CODE_WIDTH)-1);
			if (read != 0)
				return -read;
			rlen = (tmp >> SDM_MC_CMD_LEN_OFFSET ) & ((1 << SDM_MC_CMD_LEN_WIDTH )-1);
			if (rlen == 0)
				return 0;
			fill--;
		}

		// read data
		while (fill > 0) {
			// last word logic
			if (rlen == 1) {
				if (!eop) {
					break;
				} else if (len) {
					tmp = nfb_comp_read32(sdm->comp, SDM_MC_RESPONSE_FIFO);
					for (i = 0; i < len; i++) {
						((uint8_t*)data)[i] = (u_char)(tmp >> i*8); // same as mask on the lowest 8 bits
					}
					read += len;
					rlen--;
					break;
				}
			}
			*((uint32_t *)data) = nfb_comp_read32(sdm->comp, SDM_MC_RESPONSE_FIFO);
			data += WORD_WIDTH;
			read += WORD_WIDTH;
			rlen--;
			fill--;
		}
	} while (rlen > 0);

	return read;
}

/* @brief Build and send command header.
 *
 * @param sdm      Pointer to struct sdm.
 * @param cmd_code Mailbox Client command opcode.
 * @param cmd_len  Number of words following the header (without data).
 *
 * @return Returns 0 on success, negative number otherwise.
 *
 */
static int sdm_send_header(struct sdm *sdm, const uint32_t cmd_code, const uint32_t cmd_len)
{
	uint32_t header;

	// invalid arguments
	if (sdm == NULL || cmd_code >> SDM_MC_CMD_CODE_WIDTH || cmd_len >> SDM_MC_CMD_LEN_WIDTH)
		return -EINVAL;

	// build and send header of the command
	header = (sdm->cmd_id << SDM_MC_ID_OFFSET) | (cmd_len << SDM_MC_CMD_LEN_OFFSET) | (cmd_code << SDM_MC_CMD_CODE_OFFSET);

	// add space for SDM to process individual commands
	msleep(1);

	sdm_send_data(sdm, &header, WORD_WIDTH, cmd_len == 0 ? 1 : 0);

	// command id increment -> not used because every command is followed by reading the response immediately
	//sdm->cmd_id = ((sdm->cmd_id + 1) >> SDM_MC_ID_WIDTH)? 0 : sdm->cmd_id + 1;
	return 0;
}

/* @brief Lock SDM component.
 *
 * @param comp Pointer to struct nfb_comp.
 *
 * @return Returns 0 on success or -EAGAIN on failure.
 *
 */
static int sdm_try_lock(struct nfb_comp *comp)
{
	if (!nfb_comp_lock(comp, SDM_COMP_LOCK)) {
		return -EAGAIN;
	}
	return 0;
}

/* @brief Unlock SDM component.
 *
 * @param comp Pointer to struct nfb_comp.
 *
 */
static void sdm_unlock(struct nfb_comp *comp)
{
	nfb_comp_unlock(comp, SDM_COMP_LOCK);
}

int sdm_qspi_prepare(struct spi_nor *nor, enum spi_nor_ops ops)
{
	uint32_t cs = 0;
	struct nfb_boot *boot = (struct nfb_boot *)nor->priv;
	int ret;

	if ((ret = sdm_try_lock(boot->comp)) < 0)
		goto err_lock;

	if ((ret = sdm_send_header(boot->sdm, SDM_QSPI_OPEN_OP, 0)) < 0)
		goto err_open;

	if ((ret = sdm_get_data(boot->sdm, NULL, 0)) < 0)
		goto err_open;

	if ((ret = sdm_send_header(boot->sdm, SDM_QSPI_SET_CS_OP, 1)) < 0)
		goto err_cs;

	sdm_send_data(boot->sdm, &cs, WORD_WIDTH, 1);

	if ((ret = sdm_get_data(boot->sdm, NULL, 0)) < 0)
		goto err_cs;

	boot->sdm->locked = 1;
	return 0;

err_cs:
	sdm_send_header(boot->sdm, SDM_QSPI_CLOSE_OP, 0);
	sdm_get_data(boot->sdm, NULL, 0);
err_open:
	sdm_unlock(boot->comp);
err_lock:
	return ret;
}

void sdm_qspi_unprepare(struct spi_nor *nor, enum spi_nor_ops ops)
{
	struct nfb_boot *boot = (struct nfb_boot *)nor->priv;

	sdm_send_header(boot->sdm, SDM_QSPI_CLOSE_OP, 0);
	sdm_get_data(boot->sdm, NULL, 0);
	sdm_unlock(boot->comp);
	boot->sdm->locked = 0;
}

ssize_t sdm_qspi_read(struct spi_nor *nor, loff_t from, size_t len, u_char *buf)
{
	uint32_t words_len;
	ssize_t ret;
	uint32_t ufrom = (uint32_t)from;
	struct nfb_boot *boot = (struct nfb_boot *)nor->priv;

	// align data length to words
	words_len = sdm_len_bytes_to_words(len);
	if (words_len > MAX_WORDS) {
		words_len = MAX_WORDS;
	}

	// error for not correctly aligned address
	if (from & (WORD_WIDTH-1))
		return -EINVAL;

	ret = sdm_send_header(boot->sdm, SDM_QSPI_READ_OP, 2);
	if (ret < 0)
		return ret;

	// 1. argument = flash address offset to start reading from (word aligned)
	sdm_send_data(boot->sdm, &ufrom, WORD_WIDTH, 0);
	// 2. argument = number of words to read
	sdm_send_data(boot->sdm, &words_len, WORD_WIDTH, 1);

	return sdm_get_data(boot->sdm, buf, len);
}

int sdm_qspi_read_reg(struct spi_nor *nor, u8 opcode, u8 *buf, int len)
{
	int ret;
	struct nfb_boot *boot = (struct nfb_boot *)nor->priv;

	// max number of bytes to read
	int len_base = 8;
	u8 buf_base[len_base];

	int vendor_id;
	int device_id;

	int locked_before = 1;

	if (boot->sdm->locked == 0) {
		locked_before = 0;
		ret = sdm_qspi_prepare(nor, SPI_NOR_OPS_READ);
		if (ret < 0)
			goto err_prep;
	}

	ret = sdm_send_header(boot->sdm, SDM_QSPI_READ_DEVICE_REG_OP, 2);
	if (ret < 0)
		goto err_hdr;

	if (opcode == 0x9f) {
		opcode = 0x9e;
	}

	// 1. argument = opcode for the read command
	sdm_send_data(boot->sdm, &opcode, 1, 0);
	// 2. argument = number of bytes to read
	sdm_send_data(boot->sdm, &len_base, WORD_WIDTH, 1);

	ret = sdm_get_data(boot->sdm, buf_base, len_base);
	if (ret < 0)
		goto err_get;

	// return JEDEC ID in expected format
	if (opcode == 0x9e) {
		vendor_id = ((int *)buf_base)[0];
		device_id = ((int *)buf_base)[1];
		((int *)buf_base)[0] = device_id;
		((int *)buf_base)[1] = vendor_id;
	}

	memcpy(buf, buf_base, len);
	ret = 0;

err_get:
err_hdr:
	if (locked_before == 0)
		sdm_qspi_unprepare(nor, SPI_NOR_OPS_READ);
err_prep:
	return ret;
}

ssize_t sdm_qspi_write(struct spi_nor *nor, loff_t to, size_t len, const u_char *buf)
{
	uint32_t words_len;
	uint32_t uto = (uint32_t)to;
	ssize_t ret;
	struct nfb_boot *boot = (struct nfb_boot *)nor->priv;

	// align data length to words
	words_len = sdm_len_bytes_to_words(len);
	if (words_len > MAX_WORDS) {
		words_len = MAX_WORDS;
	}

	// error for not correctly aligned address
	if (to & (WORD_WIDTH-1))
		return -EINVAL;

	ret = sdm_send_header(boot->sdm, SDM_QSPI_WRITE_OP, 2 + words_len);
	if (ret < 0)
		return ret;

	// 1. argument = flash address offset to start writing to (word aligned)
	sdm_send_data(boot->sdm, &uto, WORD_WIDTH, 0);
	// 2. argument = number of words to write
	sdm_send_data(boot->sdm, &words_len, WORD_WIDTH, 0);
	// 3. argument = data to be written
	sdm_send_data(boot->sdm, buf, len, 1);

	ret = sdm_get_data(boot->sdm, NULL, 0);
	if (ret < 0)
		return ret;

	return len;
}

int sdm_qspi_write_reg(struct spi_nor *nor, u8 opcode, u8 *buf, int len)
{
	uint32_t words_len;
	int ret;
	struct nfb_boot *boot = (struct nfb_boot *)nor->priv;

	int locked_before = 1;

	// align data length to words
	words_len = sdm_len_bytes_to_words(len);

	if (boot->sdm->locked == 0) {
		locked_before = 0;
		ret = sdm_qspi_prepare(nor, SPI_NOR_OPS_WRITE);
		if (ret < 0)
			goto err_prep;
	}

	ret = sdm_send_header(boot->sdm, SDM_QSPI_WRITE_DEVICE_REG_OP, 2+words_len);
	if (ret < 0)
		goto err_hdr;

	// 1. argument = opcode for the write command
	sdm_send_data(boot->sdm, &opcode, 1, 0);

	if (len == 0) {
		// 2. argument = number of bytes to write
		sdm_send_data(boot->sdm, &len, WORD_WIDTH, 1);
	} else {
		// 2. argument = number of bytes to write
		sdm_send_data(boot->sdm, &len, WORD_WIDTH, 0);
		// 3. argument = data to be written
		sdm_send_data(boot->sdm, buf, len, 1);
	}
	ret = sdm_get_data(boot->sdm, NULL, 0);
	if (ret < 0)
		goto err_get;

	ret = 0;

err_get:
err_hdr:
	if (locked_before == 0)
		sdm_qspi_unprepare(nor, SPI_NOR_OPS_WRITE);
err_prep:
	return ret;
}

int sdm_qspi_erase(struct spi_nor *nor, loff_t off)
{
	uint32_t uoff = (uint32_t)off;
	const uint32_t size = 0x4000;
	int ret;
	struct nfb_boot *boot = (struct nfb_boot *)nor->priv;

	// error for not correctly aligned address
	if (off & (size-1))
		return -EINVAL;
	ret = sdm_send_header(boot->sdm, SDM_QSPI_ERASE_OP, 2);
	if (ret < 0)
		return ret;

	// 1. argument = flash address offset to start erase
	sdm_send_data(boot->sdm, &uoff, WORD_WIDTH, 0);
	// 2. argument = size of erased memory
	sdm_send_data(boot->sdm, &size, WORD_WIDTH, 1);

	ret = sdm_get_data(boot->sdm, NULL, 0);
	if (ret < 0)
		return ret;

	return 0;
}

int sdm_get_temperature(struct sdm *sdm, int32_t *temperature)
{
	const uint32_t bitmask = 0x1;
	int ret;

	ret = sdm_try_lock(sdm->comp);
	if (ret < 0)
		return -EAGAIN;

	ret = sdm_send_header(sdm, SDM_GET_TEMPERATURE_OP, 1);
	if (ret < 0)
		goto err_send_header;

	sdm_send_data(sdm, &bitmask, WORD_WIDTH, 1);

	ret = sdm_get_data(sdm, temperature, sizeof(*temperature));
	if (ret < 0)
		goto err_get_data;

	ret = 0;

err_get_data:
err_send_header:
	sdm_unlock(sdm->comp);
	return ret;
}

int sdm_rsu_image_update(struct sdm *sdm, uint32_t addr)
{
	int ret;
	uint32_t addr_lower = addr;
	uint32_t addr_upper = 0;

	ret = sdm_try_lock(sdm->comp);
	if (ret < 0)
		return -EAGAIN;

	ret = sdm_send_header(sdm, SDM_RSU_IMAGE_UPDATE_OP, 2);
	if (ret < 0)
		goto err_send_header;

	// 1. argument = image address offset (lower 32 bits)
	sdm_send_data(sdm, &addr_lower, WORD_WIDTH, 0);
	// 2. argument = image address offset (upper 32 bits, write as 0)
	sdm_send_data(sdm, &addr_upper, WORD_WIDTH, 1);

	ret = sdm_get_data(sdm, NULL, 0);
	if (ret < 0)
		goto err_get_data;

	ret = 0;

err_get_data:
err_send_header:
	sdm_unlock(sdm->comp);
	return ret;
}
