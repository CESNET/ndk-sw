/* SPDX-License-Identifier: GPL-2.0 */
/*!
 * \file busreplay.c
 * \brief Tool to write dumped Frame Link Unaligned data into firmware
 * \author Lukas Kekely <kekely@cesnet.cz>
 * \date 2016
 *
 * Copyright (C) 2016-2018 CESNET
 *
 * Author(s):
 *   Lukas Kekely <kekely@cesnet.cz>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include <unistd.h>
#include <nfb/nfb.h>
extern const char *__progname;


#define VERSION "$Id: busreplay.c 010000 2018-03-01 00:00:00Z kekely $"

#define ARGUMENTS ":d:e:hi:Vw:"

#define START_CMD 0x1
#define STOP_CMD 0x2
#define WRITE_CMD 0x4

#define READY_MASK  0x1000000
#define ACTIVE_MASK 0x2000000
#define FULL_MASK   0x4000000
#define DATA_WIDTH_GET(x)    ((x) & 0xFFFF)

#define DATA_REG 0x4
#define DATA_REGS(dw) ((((dw) - 1) >> 5) + 2)

int my_strtoui32(char *str, uint32_t *output) {
    char *end;
    *output = (uint32_t)strtoul(str, &end, 0);
    if(end[0]!='\0') return -1;
    return 0;
}



void usage() {
    printf("Usage: %s [-hV] [-d path] [-e 0|1] [-i comp] [-w file]\n\n", __progname);
    printf("Only one command may be used at a time.\n");
    printf("-d path        Path to device file to use\n");
    printf("-e 0|1         Disable/Enable replay\n");
    printf("-h             Show this text\n");
    printf("-i comp        Select replay component to control (default 0)\n");
    printf("-V             Show program version.\n");
    printf("-w file        Write data from file into firmware\n");
}



int main(int argc, char *argv[]) {
    uint32_t tmp;
    const char *dev_file = nfb_default_dev_path(), *out_file = NULL;
    struct nfb_device *dev = NULL;
    int enable = 0, disable = 0, comp_id = 0;
    struct nfb_comp *comp = NULL;
    int node;
    FILE *out;
    int c, i, regs, cnt;
    uint32_t buffer[1024];
    unsigned dw;

    while ((c = getopt(argc, argv, ARGUMENTS)) != -1) {
        switch (c) {
            case 'd':
                dev_file = optarg;
                break;
            case 'e':
                if((my_strtoui32(optarg, &tmp)) || (tmp > 1))
                    errx(EXIT_FAILURE, "Wrong enable/disable switch");
                if(tmp)
                    enable = 1;
                else
                    disable = 1;
                break;
            case 'h':
                usage();
                return EXIT_SUCCESS;
                break;
            case 'i':
                if((my_strtoui32(optarg, &tmp)))
                    errx(EXIT_FAILURE, "Component selection must be unsigned integer");
                comp_id = tmp;
                break;
            case 'V':
                printf("%s\n", VERSION);
                return EXIT_SUCCESS;
                break;
            case 'w':
                out_file = optarg;
                break;
            case '?':
                errx(EXIT_FAILURE, "Unknown argument '%c'", optopt);
            case ':':
                errx(EXIT_FAILURE, "Missing parameter for argument '%c'", optopt);
            default:
                errx(EXIT_FAILURE, "Unknown error");
        }
    }
    if(argc != optind)
        errx(EXIT_FAILURE, "Stray arguments");
    if((enable && disable) || (enable && out_file) || (disable && out_file))
        errx(EXIT_FAILURE, "Multiple actions required");

    dev = nfb_open(dev_file);
    if(!dev)
        errx(EXIT_FAILURE, "NFB device open failed");

    node = nfb_comp_find(dev, "netcope,busreplay", comp_id);
    comp = nfb_comp_open(dev, node);
    if(!comp) {
        nfb_close(dev);
        errx(EXIT_FAILURE, "Can't find busreplay #%d inside firmware", comp_id);
    }

    if(enable) {
        nfb_comp_write32(comp, 0, START_CMD);
    } else if(disable) {
        nfb_comp_write32(comp, 0, STOP_CMD);
    } else if(out_file) {
        out = fopen(out_file, "r");
        if(!out) {
            nfb_comp_close(comp);
            nfb_close(dev);
            errx(EXIT_FAILURE, "Can't read file %s", out_file);
        }
        tmp = nfb_comp_read32(comp, 0);
        regs = DATA_REGS(DATA_WIDTH_GET(tmp));
        if((fscanf(out, "%u\n", &dw) != 1) || (dw != DATA_WIDTH_GET(tmp))) {
            fclose(out);
            nfb_comp_close(comp);
            nfb_close(dev);
            errx(EXIT_FAILURE, "Wrong file header format for selected replay component");
        }
        cnt = 0;
        while(!feof(out)) {
            for(i = regs-1; i >= 0; i--)
                if(fscanf(out, "%08x ", &(buffer[i])) != 1) {
                    fclose(out);
                    nfb_comp_close(comp);
                    nfb_close(dev);
                    errx(EXIT_FAILURE, "Wrong data format in file");
                }
            if(nfb_comp_read32(comp, 0) & FULL_MASK) {
                warnx("firmware storage full before end of file\n");
                break;
            }
            nfb_comp_write(comp, buffer, regs<<2, DATA_REG);
            nfb_comp_write32(comp, 0, WRITE_CMD);
            cnt++;
        }
        fclose(out);
        printf("%d records written\n", cnt);
    } else {
        tmp = nfb_comp_read32(comp, 0);
        printf("-------------------------------- Bus Replay Status ----\n");
        printf("Bus data width          : %db\n", DATA_WIDTH_GET(tmp));
        printf("Replaying               : %s\n", (tmp & ACTIVE_MASK) ? "ON" : "OFF");
        printf("Replay storage          : %s\n", (tmp & FULL_MASK) ? "FULL" : ((tmp & READY_MASK) ? "READY" : "EMPTY" ));
        printf("\n");
    }

    nfb_comp_close(comp);
    nfb_close(dev);
    return EXIT_SUCCESS;
}



// /////////////////////////////////////////////////////////////////////////////
// Verification specific implementation:
#ifdef DPI_VERIFICATION
__attribute__((visibility("default"))) int busreplay(int argc, char *argv[]) {
    return main(argc,argv);
}

void dpiregister(const char *name, int (*main)(int argc, char *argv[]));

void __dpiregisterself() {
    dpiregister("busreplay", busreplay);
}
#endif
// /////////////////////////////////////////////////////////////////////////////
