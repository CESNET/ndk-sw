/* SPDX-License-Identifier: BSD-3-Clause */
/*!
 * \file busdebugctl.c
 * \brief Tool for controlling the data streaming bus debuging master and connected probes. (STREAMING_DEBUG)
 * \author Lukas Kekely <kekely@cesnet.cz>
 * \date 2013
 *
 * Copyright (C) 2013-2018 CESNET
 *
 * Author(s):
 *   Lukas Kekely <kekely@cesnet.cz>
 *
 */

#include <stdio.h>
#include <stdbool.h>
#include <err.h>
#include <unistd.h>

#include <nfb/nfb.h>
#include <libfdt.h>
extern const char *__progname;



/*!
 * \def	VERSION
 * \brief File version
 */
#define VERSION "1.1"


/* \brief acceptable command line arguments */
#define ARGUMENTS    ":d:e:i:n:ABDElhV"

/* Register offsets of one probe */
#define PROBE_WORD_CNT_LOW      0x00
#define PROBE_WORD_CNT_HIGH     0x04
#define PROBE_WAIT_CNT_LOW      0x08
#define PROBE_WAIT_CNT_HIGH     0x0C
#define PROBE_DST_HOLD_CNT_LOW  0x10
#define PROBE_DST_HOLD_CNT_HIGH 0x14
#define PROBE_SRC_HOLD_CNT_LOW  0x18
#define PROBE_SRC_HOLD_CNT_HIGH 0x1C
#define PROBE_SOP_CNT_LOW       0x20
#define PROBE_SOP_CNT_HIGH      0x24
#define PROBE_EOP_CNT_LOW       0x28
#define PROBE_EOP_CNT_HIGH      0x2C
#define PROBE_NAME              0x30
#define PROBE_CONFIG            0x34
#define PROBE_CNT_CTRLREG       0x38
#define PROBE_BUS_CTRLREG       0x3C
#define PROBE_OFFSET(id,reg)    (((id) << 6) + (reg))
#define PROBE_SPACE_WORDS       0x10

/*!
 * \def    MASTER_COMP_NAME
 * \brief    Debug Master component name
 */
#define MASTER_COMP_NAME    "netcope,streaming_debug_master"

/*!
 * \enum    commands
 * \brief    List of available commands
 */
enum commands {
    CMD_ENABLE,
    CMD_DISABLE,
    CMD_BLOCK_BUS,
    CMD_DROP_BUS,
    CMD_ENABLE_BUS,
    CMD_PRINT_STATUS,
    CMD_LIST
};

/*!
 * \brief    Display usage of program
 */
void usage() {
    printf("Usage: %s [-ABDElhV] [-d path] [-e 0|1] [-i probe] [-n probe]\n\n", __progname);
    printf("Only one command may be used at a time.\n");
    printf("-d path        Path to device file to use\n");
    printf("-e 0|1         Start(1)/Stop(0) counters (start also resets their values)\n");
    printf("-i probe       Select probe using \"master_index:probe_id\" (default \"0:0\")\n");
    printf("-n probe       Select probe using \"probe_name\"\n");
    printf("-A             Print status or affect all probes\n");
    printf("-B             Block data on probed bus\n");
    printf("-D             Drop data on probed bus\n");
    printf("-E             Enable normal functionality on probed bus\n");
    printf("-l             List available probes\n");
    printf("-h             Show this text\n");
    printf("-V             Show version\n");
}

/**
 * \brief Convert string to MASTER index and PROBE id.
 *
 * Automatically detect number format by prefix. All C like formats are
 * supported. No prefix for base 10, 0x for base 16, 0 for base 8.
 *
 * \param str        Input string
 * \param index      Pointer to output MASTER index
 * \param probe_id   Pointer to output PROBE id
 *
 * \retval 0 success
 * \retval -1 error
 */
int parse_probe(char *str, int *index, int *probe_id) {
    char *end;
    if(str==NULL)
        return -1;
    *index = strtol(str, &end, 0);
    if(end[0]!=':')
        return -1;
    *probe_id = strtol(end+1, &end, 0);
    if(end[0]!='\0')
        return -1;
    return 0;
}

/*!
 * \brief    Map MASTER component
 *
 * \param dev           NFB device
 * \param master        MASTER component in NFB device
 * \param index         Used MASTER component
 *
 * \retval    >0 on success - number of probes connected to this master (based on version)
 * \retval    0 when there is no MASTER with selected index
 * \retval    -1 on error
 */
int map_master(struct nfb_device *dev, struct nfb_comp **master, int index) {
    int node, len;
    const uint32_t *prop32;
    int probes;
    /* find information about component */
    node = nfb_comp_find(dev, MASTER_COMP_NAME, index);
    prop32 = fdt_getprop(nfb_get_fdt(dev), node, "probes", &len);
    if(len != sizeof(*prop32))
        return 0; /* component not found */
    probes = fdt32_to_cpu(*prop32);

    /* map MASTER component */
    *master = nfb_comp_open(dev, node);
    if(!(*master)) {
        warnx("Failed to open MASTER component.");
        return -1;
    }

    return probes;
}

/*!
 * \brief Do requested operation on selected device
 *
 * \param command      Requested operation
 * \param master       MASTER component in NFB device
 * \param index        MASTER component index
 * \param probe_id     PROBE number inside selected MASTER component
 *
 * \retval    0 on success
 * \retval    -1 on error
 */
int execute_operation(enum commands command, struct nfb_comp *master, int index, int probe_id) {
    unsigned char buffer[PROBE_SPACE_WORDS<<2];
    uint32_t tmp;
    switch (command) {
        /* If start counters required */
        case CMD_ENABLE:
            nfb_comp_write32(master, PROBE_OFFSET(probe_id, PROBE_CNT_CTRLREG), 1);
            break;

        /* If stop counters required */
        case CMD_DISABLE:
            nfb_comp_write32(master, PROBE_OFFSET(probe_id, PROBE_CNT_CTRLREG), 0);
            break;

        /* If block probed bus */
        case CMD_BLOCK_BUS:
            nfb_comp_write32(master, PROBE_OFFSET(probe_id, PROBE_BUS_CTRLREG), 1);
            break;

        /* If drop data on probed bus */
        case CMD_DROP_BUS:
            nfb_comp_write32(master, PROBE_OFFSET(probe_id, PROBE_BUS_CTRLREG), 2);
            break;

        /* If enable probed bus */
        case CMD_ENABLE_BUS:
            nfb_comp_write32(master, PROBE_OFFSET(probe_id, PROBE_BUS_CTRLREG), 0);
            break;

        /* If print probe status required */
        case CMD_PRINT_STATUS:
            nfb_comp_read(master, buffer, PROBE_SPACE_WORDS<<2, PROBE_OFFSET(probe_id,0));
            printf("------------------------------------- Probe Status ----\n");
            printf("Probe number                : %d:%d\n", index, probe_id);
            printf("Probe name                  : %c%c%c%c\n", buffer[PROBE_NAME+0], buffer[PROBE_NAME+1], buffer[PROBE_NAME+2], buffer[PROBE_NAME+3]);
            tmp=*((uint32_t *)(buffer+PROBE_CONFIG));
            if(tmp & 0x80) {
                printf("Probe                       : ENABLED\n");
                printf("Counters state              : %s\n", (buffer[PROBE_CNT_CTRLREG] != 1) ? "STOPPED" : "RUNNING");
                printf("Bus state                   : %s\n", (buffer[PROBE_BUS_CTRLREG] != 0) ? ((buffer[PROBE_BUS_CTRLREG] != 1) ? "DROPPED" : "BLOCKED") : "NORMAL");
                printf("----------------------------------- Probe Counters ----\n");
                if(tmp & 0x3F) {
                    if(tmp & 0x01)
                        printf("Data words                  : %llu\n", (long long unsigned)(*((uint64_t *)(buffer+PROBE_WORD_CNT_LOW))));
                    if(tmp & 0x02)
                        printf("Wait cycles                 : %llu\n", (long long unsigned)(*((uint64_t *)(buffer+PROBE_WAIT_CNT_LOW))));
                    if(tmp & 0x04)
                        printf("Destination hold cycles     : %llu\n", (long long unsigned)(*((uint64_t *)(buffer+PROBE_DST_HOLD_CNT_LOW))));
                    if(tmp & 0x08)
                        printf("Source hold cycles          : %llu\n", (long long unsigned)(*((uint64_t *)(buffer+PROBE_SRC_HOLD_CNT_LOW))));
                    if(tmp & 0x10)
                        printf("Started transactions        : %llu\n", (long long unsigned)(*((uint64_t *)(buffer+PROBE_SOP_CNT_LOW))));
                    if(tmp & 0x20)
                        printf("Ended transactions          : %llu\n", (long long unsigned)(*((uint64_t *)(buffer+PROBE_EOP_CNT_LOW))));
                } else
                    printf("(No counters in probe)\n");
            } else
                printf("Probe                       : DISABLED\n");
            break;

        /* If print probe list required */
        case CMD_LIST:
            nfb_comp_read(master, buffer, PROBE_SPACE_WORDS<<2, PROBE_OFFSET(probe_id,0));
            printf("%d:%d - %c%c%c%c\n", index, probe_id, buffer[PROBE_NAME+0], buffer[PROBE_NAME+1], buffer[PROBE_NAME+2], buffer[PROBE_NAME+3]);
            break;

        default:
            break;
    }
    return 0;
}

int my_strtol(char *str, long *output) {
    char *end;
    *output = (long)strtol(str, &end, 0);
    if(end[0]!='\0') return -1;
    return 0;
}

/*!
 * \brief    Program main function
 *
 * \param argc        number of arguments
 * \param *argv[]     array with the arguments
 *
 * \retval    EXIT_SUCCESS on success
 * \retval    EXIT_FAILURE on error
 */
int main(int argc, char *argv[]) {
    struct nfb_device *dev = NULL;            /* NFB device */
    char    *ifc = "0:0";        /* interface identificator */
    bool    ifc_name = false;   /* interface identificator is name (true) or ID (false) */
    bool    name_found = false;
    bool    all = false;        /* indicates to affect all interfaces */
    const char    *file = nfb_default_dev_path();    /* default NFB device */
    int     c;            /* temp variable for getopt */
    long    tmp;        /* temp variable */
    int     probes;
    uint32_t tmp32;
    char    cmds = 0;    /* how many operations requested */
    char    interfaces = 0; /* how many interfaces selected */
    enum commands    command = CMD_PRINT_STATUS; /* selected command for execution */
    int     index = 0;
    int     probe_id = 0;
    struct nfb_comp *master = NULL;

    /* process command line arguments */
    opterr = 0;
    while ((c = getopt(argc, argv, ARGUMENTS)) != -1)
        switch(c) {
            case 'd':
                file = optarg;
                break;
            case 'e':
                if (my_strtol(optarg, &tmp) || (tmp != 0 && tmp != 1))
                    errx(EXIT_FAILURE, "Wrong enable value (0|1).");
                command = tmp ? CMD_ENABLE : CMD_DISABLE;
                cmds++;
                break;
            case 'i':
                ifc = optarg;
                ifc_name = 0;
                interfaces++;
                all = false;
                break;
            case 'n':
                if (strlen(optarg) != 4)
                    errx(EXIT_FAILURE, "Probe name is exactly 4 characters long.");
                ifc=optarg;
                ifc_name = 1;
                interfaces++;
                all = false;
                break;
            case 'A':
                all = true;
                interfaces++;
                break;
            case 'B':
                command = CMD_BLOCK_BUS;
                cmds++;
                break;
            case 'D':
                command = CMD_DROP_BUS;
                cmds++;
                break;
            case 'E':
                command = CMD_ENABLE_BUS;
                cmds++;
                break;
            case 'l':
                command = CMD_LIST;
                all = true;
                cmds++;
                break;
            case 'h':
                usage();
                return EXIT_SUCCESS;
            case 'V':
                printf("Bus Debug control tool - version %s\n", VERSION);
                return EXIT_SUCCESS;
            case '?':
                errx(EXIT_FAILURE, "Unknown argument '%c'", optopt);
            case ':':
                errx(EXIT_FAILURE, "Missing parameter for argument '%c'", optopt);
            default:
                errx(EXIT_FAILURE, "Unknown error");
        }
    argc -= optind;
    argv += optind;

    if (argc != 0)
        errx(EXIT_FAILURE, "stray arguments");

    if (cmds > 1)
        errx(EXIT_FAILURE, "More than one operation requested. Please select just one.");

    if (interfaces > 1)
        errx(EXIT_FAILURE, "Combination of parameters '-A', '-i', '-n' detected. Please don't combine them.");

    /* attach device and map address spaces */
    dev = nfb_open(file);
    if(!dev)
        errx(EXIT_FAILURE, "NFB device open failed");

    /* if all interfaces requested or name identification used */
    if (all || ifc_name) {
        /* go through all masters */
        for (index = 0; (probes = map_master(dev, &master, index)) > 0; index++) {
            /* go through all probes in master */
            for (probe_id = 0; probe_id < probes; probe_id++) {
                tmp32 = nfb_comp_read32(master, PROBE_OFFSET(probe_id, PROBE_NAME));
                if (all || strncmp(ifc, (char *)(&tmp32), 4) == 0) {
                    name_found = true;
                    if (execute_operation(command, master, index, probe_id) != 0) {
                        nfb_comp_close(master);
                        nfb_close(dev);
                        exit(EXIT_FAILURE);
                    }
                    if (command == CMD_PRINT_STATUS)
                        printf("\n");
                }
            }
            nfb_comp_close(master);
        }
        if (probes < 0){
            nfb_close(dev);
            exit(EXIT_FAILURE);
        }
        if (ifc_name && !name_found)
            warnx("Probe with name \"%c%c%c%c\" not found in design.", ifc[0], ifc[1], ifc[2], ifc[3]);
    } else {
        if (parse_probe(ifc, &index, &probe_id)) {
            nfb_close(dev);
            errx(EXIT_FAILURE, "Wrong probe identification format.");
        }
        /* Map space of the MASTER component */
        if (map_master(dev, &master, index) <= 0) {
            nfb_close(dev);
            errx(EXIT_FAILURE, "Component Debug Master with index %d not found in your design.", index);
        }
        if (nfb_comp_read32(master, PROBE_OFFSET(probe_id, PROBE_NAME)) == 0 || errno) {
            nfb_comp_close(master);
            nfb_close(dev);
            errx(EXIT_FAILURE, "Probe %d:%d not found in your design.", index, probe_id);
        }
        if (execute_operation(command, master, index, probe_id) != 0) {
            nfb_comp_close(master);
            nfb_close(dev);
            exit(EXIT_FAILURE);
        }
        nfb_comp_close(master);
    }

    nfb_close(dev);
    return EXIT_SUCCESS;
}
