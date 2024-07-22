/*
 *  MI test tool
 *
 *  Copyright (C) CESNET, 2024
 *  Author(s):
 *    Martin Spinler <spinler@cesnet.cz>
 *
 *  SPDX-License-Identifier: GPL-2.0
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <stdio.h>
#include <time.h>

#include <libfdt.h>
#include <nfb/nfb.h>
#include <nfb/ndp.h>

#include <netcope/eth.h>
#include <netcope/nccommon.h>

#include <smmintrin.h>
#include <emmintrin.h>
#include <immintrin.h>

#define DATA_SIZE 8192
//#define DATA_SIZE 128

char data1[DATA_SIZE];
char data2[DATA_SIZE];
char datar[DATA_SIZE];
__m256i datam[DATA_SIZE/32];

struct timespec start, stop;

struct mi_test_params {
	struct nfb_comp * comp;
	int verbose;
	int trans_mask; /* 1 -> write, 2 -> read, 3 -> write + read */
	unsigned mi_off_min;
	unsigned mi_off_max;
	unsigned long long transaction_count;
	unsigned long long iteration_count;

	struct list_range length_range;
	struct list_range addr_range;
	struct list_range src_off_range;
	struct list_range dst_off_range;
};

void mstart() {
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start);
}

double mstop() {
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &stop);
	double result = (stop.tv_sec - start.tv_sec) * 1e6 + (stop.tv_nsec - start.tv_nsec) / 1e3;
	return result;
}

void randomize_data(void)
{
	int i;

	srand(time(NULL));
	for (i = 0; i < DATA_SIZE; i++) {
		data1[i] = rand();
		data2[i] = rand();
	}
	memcpy(datam, data1, sizeof(datam));
}

enum commands {
	CMD_USAGE,
	CMD_TEST_RANDOM,
	CMD_TEST_LINEAR,
	CMD_TEST_PERFORMANCE,
};

#define ARGUMENTS "Cc:d:t:l:R:S:D:T:I:rwhv"

void usage(const char *progname)
{
	printf("Usage: %s [-hv] [-d path]\n", progname);
	printf("-d path         Path to device [default: %s]\n", NFB_DEFAULT_DEV_PATH);
	printf("-c compatible   Compatible string of component to use in test\n");
	printf("-t test         Select test: random, linear, performance [default: random]\n");
	printf("-r              Use only reads\n");
	printf("-w              Use only writes\n");
	printf("-l length       Use specified length (can be range)\n");
	printf("-T count        Transaction count\n");
	printf("-I count        Iteration count\n");
	printf("-C              CSV output (for performance test)\n");
	printf("-R range        Use specified address space range (min-max) [default: all]\n");
	printf("-S range        Use specified RAM offset range [default: 0]\n");
	printf("-D range        Use specified MI offset range [default: 0]\n");
	printf("-h              Show this text\n");
	printf("-v              Increase verbosity\n");
}

void print_transaction(int type, unsigned off_mi, unsigned off_ram, unsigned size, uint8_t *data, double speed)
{
	int j;
	char * dir = (type == 3 ? "W+R" : (type == 2 ? "R/O" : (type == 1 ? "W/O": "-/-")));
	printf("Transaction: %s, MI offset: %04x, RAM offset: %04x, length: % 4d B", dir, off_mi, off_ram, size);
	if (speed)
		printf(", speed: % 14.4lf MBps", speed);
	if (data) {
		printf(" | ");
		for (j = off_mi; j < off_mi + size; j++) {
			printf("%02x ", data[j]);
		}
	}
	printf("\n");
}

void print_stats(double time, long long unsigned count, long long unsigned bytes)
{
	printf("Total time: %f s, transactions: %llu, bytes: %llu, average speed: %.3lf MBps\n",
			time / 1000000, count, bytes, ((double)bytes)/time);
}

int check_integrity(uint8_t*src, uint8_t*dst, int off_mi, int off_ram, int size)
{
	if (memcmp(src, dst, size)) {
		fprintf(stderr, "Read failed: MI offset %04x, RAM offset: %04x, length: % 4d\n", off_mi, off_ram, size);
		return 1;
	}
	return 0;
}

int do_test_random(struct mi_test_params *p)
{
	int ret = 0;
	unsigned int rseed = 15451;
	int rnd;
	const unsigned long long TRANSACTION_COUNT = p->transaction_count == 0 ? 100000 : p->transaction_count;
	unsigned long long i;

	unsigned long long bytes = 0;
	unsigned long long duals = 0;

	double time;

	const int space = p->mi_off_max - p->mi_off_min;


	int size, addr, trans;

	char *data;

	mstart();
	for (i = 0; i < TRANSACTION_COUNT; i++) {
		rnd = nc_fast_rand(&rseed);

		data = (rnd % 2) == 0 ? data1 : data2;
		rnd /= 2;

		trans = (rnd % 3) + 1;
		rnd /= 4;

		addr = rnd % space;

		rnd = nc_fast_rand(&rseed);

		size = (rnd % (space - addr)) + 1;

		trans &= p->trans_mask;
		if (trans == 0)
			trans = p->trans_mask;

		if (trans & 1)
			nfb_comp_write(p->comp, &data[addr], size, p->mi_off_min + addr);

		if (trans & 2)
			nfb_comp_read(p->comp, &datar[addr], size, p->mi_off_min + addr);

		bytes += size;

		if (trans == 3) {
			duals += 1;
			ret |= check_integrity(data + addr, datar + addr, p->mi_off_min + addr, addr, size);
		}

		if (p->verbose > 1) {
			print_transaction(trans, p->mi_off_min + addr, addr, size, p->verbose > 2 ? &data[addr] : NULL, 0);
		}
	}

	time = mstop();

	if (p->verbose > 0) {
		print_stats(time, TRANSACTION_COUNT + duals, bytes);
	}
	return ret;
}

void performance_test_single(struct mi_test_params *p, struct nfb_comp *comp, int verb, int count, int size, int src_off, int dst_off, double *time, uint64_t *transactions, uint64_t *bytes)
{
	int i;
	int trans = p->trans_mask;

	double throughput;
	char *data;

	data = (char*) datam;
	data += src_off;

	mstart();
	for (i = 0; i < count; i++) {
		if (trans & 1)
			nfb_comp_write(comp, data, size, dst_off);

		if (trans & 2)
			nfb_comp_read(comp, datar, size, dst_off);
	}
	*time = mstop();

	*transactions = count;
	*bytes = count * (uint64_t) size;
	if (trans == 3) {
		*transactions *= 2;
		*bytes *= 2;
	}

	throughput = ((double)*bytes) / *time;

	if (verb > 1) {
		print_transaction(trans, dst_off, src_off, size, verb > 2 ? data : NULL, throughput);
	} else if (verb == -1) {
		printf("% 4d, % 3d, % 3d, % 11lf\n", size, dst_off, src_off, throughput);
	}
}

void do_test_performance(struct mi_test_params *p)
{
	const uint8_t src_offs[] = {0};
	const uint8_t dst_offs[] = {0};

	uint64_t it = 0;
	double time = 0;
	uint64_t transactions = 0;
	uint64_t bytes = 0;

	double otime = 0;

	double ptime;
	uint64_t ptransactions;
	uint64_t pbytes;
	const int PI = 0 /* + 1000 */;
	int doi, soi, len;
	int src_off, dst_off;

	unsigned max_size = p->mi_off_max - p->mi_off_min;

	if (list_range_empty(&p->length_range)) 
		list_range_add_range(&p->length_range, 1, 256);

	if (list_range_empty(&p->src_off_range))
		list_range_add_number(&p->src_off_range, 0);

	if (list_range_empty(&p->dst_off_range))
		list_range_add_number(&p->dst_off_range, 0);


	it = p->iteration_count == 0 ? 100000 : p->iteration_count;

	for (doi = 0; doi <= 32/*sizeof(dst_offs)*/; doi++) {
		if (!list_range_contains(&p->dst_off_range, doi))
			continue;

		for (soi = 0; soi <= 32/*sizeof(src_offs)*/; soi++) {
			if (!list_range_contains(&p->src_off_range, soi))
				continue;

			for (len = 1; len <= max_size; len++) {
				if (!list_range_contains(&p->length_range, len))
					continue;

				if (PI) {
					it = PI;
					/* first probe test duration */
					performance_test_single(p, p->comp, 0, it, len, 0, 0, &ptime, &ptransactions, &pbytes);

					/* approximate to 100 ms for each test */
					it = (100 * 1000) / (ptime / it);
					otime = ptime;
				}

				src_off = soi /*src_offs[soi]*/;
				dst_off = doi /*dst_offs[doi]*/;

				if (len > max_size - dst_off)
					continue;

				performance_test_single(p, p->comp, p->verbose, it, len, src_off, dst_off, &ptime, &ptransactions, &pbytes);
				bytes += pbytes;
				transactions += ptransactions;
				time += ptime;
				if (p->verbose > 2)
					printf("Iterations: % 10d, time: %10.0f us | Probing iterations: % 08d Probing time %10.0f us\n", it, ptime, PI, otime);
			}
		}
	}

	if (p->verbose > 0) {
		print_stats(time, transactions, bytes);
	}
}

int do_test_linear(struct mi_test_params *p)
{
	int ret;
	int j;
	unsigned rl;
	uint64_t bytes = 0;
	uint64_t total = 0;
	char *data;
	int trans;
	double time;

	int si_min = 0x0;
	int di_min = 0x0;
	int si_max = 0x100;
	int di_max = 0x100;

	int sp, dp;
	int ii, si, di, l;

	ret = 0;

	if (p->addr_range.items == 1) {
		di_min = si_min = p->addr_range.min[0];
		di_max = si_max = p->addr_range.max[0];
	}

	if (list_range_empty(&p->length_range)) {
		list_range_add_range(&p->length_range, 1, di_max);
	}

	p->iteration_count = p->iteration_count == 0 ? 1 : p->iteration_count;

	trans = p->trans_mask;

	mstart();
	for (ii = 0; ii < p->iteration_count; ii++) {
		for (si = si_min; si < si_max; si++) {
			sp = 0;
			for (di = di_min; di < di_max; di++) {
				dp = 0;
				for (l = di_min + 1; l < di_max + 1; l++) {
					if (p->transaction_count != 0 && total >= p->transaction_count)
						break;

					if (!list_range_contains(&p->length_range, l))
						continue;

					data = l % 2 ? data1 : data2;

					rl = l;
					if (rl > di_max - di)
						rl = di_max - di;

					/* Skip already performed transactions */
					if (rl != l)
						continue;

					if (trans & 1)
						nfb_comp_write(p->comp, data+si, rl, di);
					if (trans & 2)
						nfb_comp_read(p->comp, datar+si, rl, di);

					if (trans == 3) {
						ret |= check_integrity(data+si, datar+si, si, di, rl);
					}

					if (p->verbose > 1) {
						if (p->verbose > 2 || sp == 0) {
							if (p->verbose > 3 || dp == 0) {
								sp = 1;
								dp = 1;
								print_transaction(trans, si, di, rl, p->verbose > 4 ? datar+si : NULL, 0);
							}
						}
					}

					bytes += rl;
					total++;
				}
			}
		}
	}
	time = mstop();

	if (trans == 3) {
		total *= 2;
		bytes *= 2;
	}

	if (p->verbose > 0) {
		print_stats(time, total, bytes);
	}
	return ret;
}

int main(int argc, char *argv[])
{
	int c;
	int ret = EXIT_SUCCESS;

	char *path = NFB_DEFAULT_DEV_PATH;

	const char *compatible = "cesnet,ofm,mi_test_space";

	struct mi_test_params params;
	struct nfb_device *dev;
	struct nfb_comp *comp;

	int fdt_offset;
	int proplen;
	const uint32_t *prop;

	enum commands command = CMD_TEST_RANDOM;

	params.verbose = 0;

	params.trans_mask = 3;

	params.mi_off_min = 0;
	params.mi_off_max = 0x80000000;
	params.transaction_count = 0;
	params.iteration_count = 0;

	list_range_init(&params.length_range);
	list_range_init(&params.addr_range);
	list_range_init(&params.src_off_range);
	list_range_init(&params.dst_off_range);

	while ((c = getopt(argc, argv, ARGUMENTS)) != -1) {
		switch (c) {
		case 'd':
			path = optarg;
			break;
		case 'c':
			compatible = optarg;
			break;
		case 'r':
			params.trans_mask = 2;
			break;
		case 'w':
			params.trans_mask = 1;
			break;
		case 't':
			if (strcmp(optarg, "random") == 0) {
				command = CMD_TEST_RANDOM;
			} else if (strcmp(optarg, "linear") == 0) {
				command = CMD_TEST_LINEAR;
			} else if (strcmp(optarg, "performance") == 0) {
				command = CMD_TEST_PERFORMANCE;
			} else {
				errx(1, "unknown argument -%c", optopt);
				usage(argv[0]);
				return -1;
			}
			break;
		case 'T':
			if (nc_strtoull(optarg, &params.transaction_count))
				errx(-1, "Cannot parse transaction count parameter");
			break;
		case 'I':
			if (nc_strtoull(optarg, &params.iteration_count))
				errx(-1, "Cannot parse transaction count parameter");
			break;
		case 'l':
			if (list_range_parse(&params.length_range, optarg) < 0)
				errx(EXIT_FAILURE, "Cannot parse length range argument.");
			break;
		case 'h':
			command = CMD_USAGE;
			break;
		case 'v':
			params.verbose = params.verbose < 0 ? 1 : params.verbose + 1;
			break;
		case 'C':
			params.verbose = -1;
			break;
		case 'R':
			if (list_range_parse(&params.addr_range, optarg) < 0 || params.addr_range.items != 1) 
				errx(EXIT_FAILURE, "Cannot parse address space range.");
			break;
		case 'S':
			if (list_range_parse(&params.src_off_range, optarg) < 0) 
				errx(EXIT_FAILURE, "Cannot parse source offset range.");
			break;
		case 'D':
			if (list_range_parse(&params.dst_off_range, optarg) < 0) 
				errx(EXIT_FAILURE, "Cannot parse destination offset range.");
			break;
		default:
			errx(1, "unknown argument -%c", optopt);
		}
	}

	if (command == CMD_USAGE) {
		usage(argv[0]);
		list_range_destroy(&params.length_range);
		list_range_destroy(&params.addr_range);
		list_range_destroy(&params.src_off_range);
		list_range_destroy(&params.dst_off_range);
		return 0;
	}
	argc -= optind;
	argv += optind;

	if (argc)
		errx(1, "stray arguments");

	dev = nfb_open(path);
	if (!dev)
		errx(1, "can't open device file");

	fdt_offset = nfb_comp_find(dev, compatible, 0);

	prop = fdt_getprop(nfb_get_fdt(dev), fdt_offset, "reg", &proplen);
        if (proplen != sizeof(*prop) * 2) {
                errno = EBADFD;
                return -1;
        }

	if (fdt32_to_cpu(prop[1]) < params.mi_off_max)
		params.mi_off_max = fdt32_to_cpu(prop[1]);

	if (params.mi_off_min > params.mi_off_max)
		params.mi_off_min = params.mi_off_max;

	if (params.mi_off_max - params.mi_off_min > DATA_SIZE) {
		params.mi_off_max = params.mi_off_min + DATA_SIZE;
		warnx("Internal buffer size is not sufficient, testing only part of MI address space");
	}

	comp = nfb_comp_open(dev, fdt_offset);

	if (comp == NULL) {
		errx(1, "Can't open MI bus component with compatible: %s", compatible);
	}

	params.comp = comp;

	randomize_data();

	/* Force map page to userspace before time sensitive tests */
	if (params.trans_mask & 1) {
		nfb_comp_write(comp, data1, 1, 0);
	} else {
		nfb_comp_read(comp, datar, 1, 0);
	}

	if (command == CMD_TEST_RANDOM)
		do_test_random(&params);
	else if (command == CMD_TEST_PERFORMANCE)
		do_test_performance(&params);
	else if (command == CMD_TEST_LINEAR)
		do_test_linear(&params);

	nfb_comp_close(comp);
	nfb_close(dev);

	list_range_destroy(&params.length_range);
	list_range_destroy(&params.addr_range);
	list_range_destroy(&params.src_off_range);
	list_range_destroy(&params.dst_off_range);
	return ret;
}
