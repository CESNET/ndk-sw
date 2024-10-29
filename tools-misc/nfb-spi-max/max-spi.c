/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Tool for access MAX chip over SPI on specific boards
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 *   Stepan Friedl <friedl@cesnet.cz>
 */

#include <err.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>

#include <nfb/nfb.h>
#include <netcope/nccommon.h>

#define ARGUMENTS 		"B:c:d:fhvs:w:W"

/* Default base address of the I2C controller */
#define CTRL_BASE 		0x2008
#define CTRL_REG   		0x4
#define DATA_REG   		0x0
#define RDATA_BASE		0x8
#define WDATA_BASE		0x4

#define CMD_FLASH_RD		0x1
#define CMD_FLASH_WR		0x2
#define CMD_FCTRL_RD		0x3
#define CMD_FCTRL_WR		0x4
#define CMD_VER_RD		0x8
#define CMD_DT_RD		0x9
#define CMD_ID_RD		0xD
#define CMD_SCR_RD		0xC
#define CMD_SCR_WR		0xB

int wait_for_spi(struct nfb_comp *comp, int spi_base)
{
	int counter = 0;
	int res;

	while ((res = ((nfb_comp_read32(comp, spi_base + CTRL_REG) & 0x01) == 0x0)) && counter < 10000) {
		counter++;
		usleep(1);
	}
	return !res;
}

int spi_comm(struct nfb_comp *comp, int spi_base, uint32_t cmd, uint32_t addr)
{
	int res;

	nfb_comp_write32(comp, spi_base + CTRL_REG, (cmd << 28) | addr);
	res = wait_for_spi(comp, spi_base);
	if (!res)
		errx(1, "SPI communication failed, exiting");
	return res;
}

void usage(const char *me)
{
	printf("Usage: %s [-ahilW] [-c n] [-d s] [-s s] [-w x] [addr] [val]\n", me);
        printf("-B a   Set SPI controller base address (default 0x%08X) \n", CTRL_BASE);
	printf("-c n   Read 'n' (dec) 32bit values (default = 1)\n");
	printf("-d s   Set device file path to 's'\n");
	printf("-s s   Set space to work with: 'flash', 'fctrl', 'ver', 'date' or 'id' (default 'flash')\n");
	printf("-h     Show this text\n");
	printf("-v     Be verbose\n");
	printf("-w x   Write value 'x' (hex), read otherwise\n");
	printf("-W     Write 4-byte values read from stdin\n");
	printf("addr   Hexadecimal offset (default 0x0)\n");
	printf("val    Write value 'val' (hex), same as -w val\n");
}

int main(int argc, char *argv[])
{
	struct nfb_device *dev;
	struct nfb_comp *comp;
	uint32_t 		data, offs;
	int 			do_write, count;
	int 			do_write_ff;
	int 			c, verbose;
	uint32_t		spi_base;
	const char		*file;
	char 			*cmdstr;
	int 			cmd, i;

	do_write 	= 0;
	count 		= 1;
	do_write_ff 	= 0;
	verbose		= 0;
	file 		= nfb_default_dev_path();
	spi_base 	= CTRL_BASE;

	cmd = CMD_FLASH_RD;
	cmdstr = "flash";


	while((c = getopt(argc, argv, ARGUMENTS)) != -1) {
		switch (c) {
		case 'B':
			spi_base = nc_xstrtoul(optarg, 16);
			break;

		case 'c': 		/* Set read count 	*/
			count = nc_xstrtoul(optarg, 10);
			if (count < 0)
				errx(1, "invalid count");
			break;
		case 'd': 		/* Select device 	*/
			file = optarg;
			break;
                case 'v':		/* */
                        verbose = 1;
                        break;
		case 'h': 		/* Help text 		*/
			usage(argv[0]);
			return 0;

		case 'w': 		/* Write value 		*/
			data = nc_xstrtoul(optarg, 16);
			do_write = 1;
			break;

		case 'W': 		/* Write values from file */
			do_write = 1;
			do_write_ff = 1;
			break;
		case 's':
			cmdstr = optarg;
			break;
		default:
			errx(1, "unknown option -%c", (char) optopt);
			return 1;
		}
	}

	argc -= optind;
	argv += optind;

	if (argc == 0)
		offs = 0;
	else
		offs = nc_xstrtoul(argv[0], 16);

	if (argc > 2)
		errx(1, "stray arguments");

	if (argc == 2) {
		if (do_write)
			errx(1, "inconsistent usage");

		data = nc_xstrtoul(argv[1], 16);
		do_write = 1;
	}

	if (strcmp(cmdstr, "flash") == 0) {
		cmd = do_write ? CMD_FLASH_WR : CMD_FLASH_RD;
	} else if (strcmp(cmdstr, "fctrl") == 0) {
		cmd = do_write ? CMD_FCTRL_WR : CMD_FCTRL_RD;
	} else if (strcmp(cmdstr, "ver") == 0) {
		cmd = CMD_VER_RD;
	} else if (strcmp(cmdstr, "date") == 0) {
		cmd = CMD_DT_RD;
	} else if (strcmp(cmdstr, "id") == 0) {
		cmd = CMD_ID_RD;
	} else {
		errx(1, "unknown command - %s. Allowed commands are: 'flash', 'fctrl', 'ver', 'date', 'id'.",  cmdstr);
		return 1;
	}

	if ((dev = nfb_open(file)) == NULL)
               	err(1, "nfb_open failed");

	if ((comp = nfb_comp_open(dev, nfb_comp_find(dev, "netcope,bus,mi", 0))) == NULL)
		errx(1, "nfb_comp_open failed - MI bus not availible. Try load nfb module with parameter mi_debug=1");

	if (verbose) {
		if (do_write) {
			printf("Writing address 0x%08x, data 0x%08x\n", offs, data);
		} else {
			printf("Reading address 0x%08x\n", offs);
		}
	}

	if (do_write) {
               	if (do_write_ff) {
               	        while (! feof(stdin)) {
       	                        do {
                       	                c = getc(stdin);
                               	} while (isspace(c));

                                if (feof(stdin))
       	                                break;

               	                ungetc(c, stdin);
                       	        if (fscanf(stdin, "%08x", &data) != 1 && \
                               	    feof(stdin) == 0) {
                                       	warnx("invalid input token");
                                        goto fail;
       	                        }

				if (verbose) {
	               	                printf("%08x%c", data, \
        	               	            ((offs + 1) % 8 == 0) ? '\n' : ' ');
                	               	fflush(stdout);
				}

				/* Test FLASH status: it should be "IDLE before executing the command */
				if (cmd == CMD_FLASH_WR) {
					uint32_t fstatus = 2;
					while (fstatus != 0) {
						/* Read flash status reg and wait until the write operation finishes */
						spi_comm(comp, spi_base, CMD_FCTRL_RD, 0x0);
						fstatus = (nfb_comp_read32(comp, spi_base + DATA_REG) & 0x00000003);
					}
				}

				/* Issue the write command */
				nfb_comp_write32(comp, spi_base + DATA_REG, data);
				spi_comm(comp, spi_base, cmd, offs);

       	                        offs += 1;
               	        }
			printf("\n");
               	} else {
			nfb_comp_write32(comp, spi_base + DATA_REG, data);
			spi_comm(comp, spi_base, cmd, offs);
		}
	} else {
		for (i = 1; i <= count; i++) {
			spi_comm(comp, spi_base, cmd, (offs+i-1));
			data = nfb_comp_read32(comp, spi_base + DATA_REG);
			printf("%08x\n", (data));
		}
	}

        nfb_comp_close(comp);
        nfb_close(dev);
	return 0;

fail:
        nfb_comp_close(comp);
        nfb_close(dev);
        return 1;
}
