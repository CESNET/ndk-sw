/* SPDX-License-Identifier: GPL-2.0 */
/*
 * sdm.h: SDM Client commands header file
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Tomas Hak <xhakto01@stud.fit.vutbr.cz>
 */

#ifndef SDM_H
#define SDM_H

#include "../../spi-nor/spi-nor.h"
#include "../nfb.h"

/** @struct Secure Device Manager structure.
 *
 */
struct sdm;

/** @brief Initialize SDM struct.
 *
 *  @param nfb NFB instance.
 *  @param fdt_offset Device Tree offset for SDM component
 *  @param name FPGA card name.
 *
 *  @return Returns pointer to the newly allocated SDM struct or NULL in case of error.
 *
 */
struct sdm *sdm_init(struct nfb_device *nfb, int fdt_offset, const char *name);

/** @brief Deallocate SDM struct memory.
 *
 *  @param sdm Pointer to the SDM struct.
 *
 */
void sdm_free(struct sdm *sdm);

/** @brief Prepare the quad SPI device for next operations.
 *
 *  @param nor QSPI device pointer.
 *  @param ops Operations enumeration.
 *
 *  @return Returns 0 on success, non-zero number indicates error.
 *
 */
int sdm_qspi_prepare(struct spi_nor *nor, enum spi_nor_ops ops);

/** @brief Unprepare the quad SPI device after previous operations.
 *
 *  @param nor QSPI device pointer.
 *  @param ops Operations enumeration.
 *
 */
void sdm_qspi_unprepare(struct spi_nor *nor, enum spi_nor_ops ops);

/** @brief Read the quad SPI device.
 *
 *  @param nor  QSPI device pointer.
 *  @param from Offset in QSPI device address space where to read from (must be word aligned).
 *  @param len  Number of bytes to read. The maximum transfer size is 1024 words (4 kB).
 *  @param buf  Buffer for data read (must be large enough).
 *
 *  @return Returns number of bytes read, negative number indicates error.
 *
 */
ssize_t sdm_qspi_read(struct spi_nor *nor, loff_t from, size_t len, u_char *buf);

/** @brief Read registers from the quad SPI device.
 *
 *  @param nor    QSPI device pointer.
 *  @param opcode The opcode for the read command.
 *  @param buf    Buffer for data read (must be large enough and word aligned).
 *  @param len    Number of bytes to read. Maximum transfer size is 2 words (8 B).
 *
 *  @return Returns 0 on success, non-zero number indicates error.
 *
 */
int sdm_qspi_read_reg(struct spi_nor *nor, u8 opcode, u8 *buf, int len);

/** @brief Write data to quad SPI device.
 *
 *
 *  @param nor QSPI device pointer.
 *  @param to  Offset in QSPI device address space where to write to (must be word aligned).
 *  @param len Number of bytes to write. Maximum transfer size is 1024 words (4 kB).
 *  @param buf Buffer with data to write.
 *
 *  @return Returns number of bytes written, negative number indicates error.
 *
 */
ssize_t sdm_qspi_write(struct spi_nor *nor, loff_t to, size_t len, const u_char *buf);

/** @brief Write to registers of the quad SPI device.
 *
 *  @param nor    QSPI device pointer.
 *  @param opcode The opcode for write command.
 *  @param buf    Buffer with data to write.
 *  @param len    Number of bytes to write. The maximum transfer size is 2 words (8 B).
 *
 *  @return Returns 0 on success, non-zero number indicates error.
 *
 */
int sdm_qspi_write_reg(struct spi_nor *nor, u8 opcode, u8 *buf, int len);

/** @brief Erase a sector of the quad SPI device.
 *
 *  Only one sector of size 0x4000 words (64 kB) is erased on given offset.
 *
 *  @param nor QSPI device pointer.
 *  @param off Offset in QSPI device address space where to start erasing (must be 64 kB aligned).
 *
 *  @return Returns 0 on success, non-zero number indicates error.
 *
 */
int sdm_qspi_erase(struct spi_nor *nor, loff_t off);

/** @brief Read FPGA core temperature from SDM.
 *
 *  @param sdm Pointer to SDM struct.
 *  @param temperature obtained temperature in millicelsius.
 *
 *  @return Returns temperature read or negative number indicating error.
 *
 */
int sdm_get_temperature(struct sdm *sdm, int32_t *temperature);

/** @brief Reconfigure FPGA from specified image.
 *
 *  @param sdm Pointer to SDM struct.
 *  @param addr Lower 32 bits of the image address.
 *
 *  @return Returns 0 on success or negative number indicating error.
 *
 */
int sdm_rsu_image_update(struct sdm *sdm, uint32_t addr);

#endif
