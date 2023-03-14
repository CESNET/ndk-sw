/* SPDX-License-Identifier: GPL-2.0 */
/*
 * TSU time synchronisation tool
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <err.h>
#include <syslog.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <math.h>

#include <netcope/nccommon.h>
#include <libfdt.h>
#include <nfb/nfb.h>
#include <netcope/tsu.h>

volatile int RUN = 1;

struct nfb_device *dev = NULL;
struct nc_tsu *tsu_comp = NULL;

int arg_debug = 0;
int arg_clk_source = -1;

int logfile;

#define SLEEPTIME 1000000
#define CONVERGEIN 10.0
#define MAX_DIVERGENCE 600 // maximun divegence in sec - will reset card time if reached
#define SYSTEM_TIME_TIMEOUT 10000
#define GET_SYSTEM_TIME_REPEAT 10000
#define INC_MAX 2.980215192E-08

#define PROGNAME "nfb-tsu"

/*
 * let we define new time unit: 1 xanosecond (1 xs) = 2^-30 s
 * XANOSEC - unit of time: 2^-30[s] ~ 9.313226*10^-9,
 */
#define XANOSEC         1073741824L
#define FRAC            (4294967296.)             /* 2^32 as a double */

struct timespecx {
	int32_t tv_sec;         /* # of seconds since 1.1.1970 (when used as unsigned) */
	int32_t tv_xsec;        /* # of xanoseconds */
};

struct tclock {
	struct timespecx lastSystemTime;
	struct timespecx lastRealTime;

	struct timespecx currentSystemTime;
	struct timespecx currentRealTime;

	uint64_t incr;

	int32_t tsu_gen_frequency;
};

struct tclock ptm_clock;

typedef struct {
	uint32_t l_ui;
	uint32_t l_uf;
} l_fp;

#define FP2TX(f, t) \
	do { \
		(t).tv_sec = (f).l_ui; \
		(t).tv_xsec = ((f).l_uf >> 2); \
	} while (0)


static inline double xs2us(int32_t x)
{
	return (double) x * 1.0e6 / XANOSEC;
}

uint64_t double2frac64(double val)
{
	uint64_t ret;

	ret = val * FRAC;
	val *= FRAC;
	val -= (double) (ret);
	ret <<= 32;
	ret |= (uint32_t)(val * FRAC);
	return ret;
}

static inline long double frac64b2nsd(uint64_t fr)
{
	const long double c = FRAC * FRAC;
	return fr / c * 1e9;
}

void print_tsx(const char* prefix, struct timespecx tsx, const char * suffix)
{
	char str[80];
	struct tm *time;
	time_t timer;

	if (prefix)
		printf("%s", prefix);

	timer = (time_t) (uint32_t) tsx.tv_sec;
	time = localtime(&timer);
	strftime(str, sizeof(str), "%F %T", time);
	printf("%s", str);
	printf(".%09d", (uint32_t) (xs2us(tsx.tv_xsec)*1000));

	if (suffix)
		printf("%s", suffix);
}

void sys_get_time(l_fp *fp)
{
	struct timespec ts;
	double tmp;

	clock_gettime(CLOCK_REALTIME, &ts);

	tmp = ts.tv_nsec / 1000000000.;
	fp->l_uf = (unsigned) (tmp * FRAC);
	fp->l_ui = ts.tv_sec;
}

static inline void tsu_get_time(l_fp *time)
{
	struct nc_tsu_time tsu_time;

	tsu_time = nc_tsu_get_rtr(tsu_comp);
	time->l_ui = tsu_time.sec;
	time->l_uf = tsu_time.fraction >> 32;
}

static inline void tsu_set_time(l_fp *time)
{
	struct nc_tsu_time tsu_time;

	tsu_time.sec = time->l_ui;
	tsu_time.fraction = time->l_uf;
	tsu_time.fraction <<= 32;
	nc_tsu_set_rtr(tsu_comp, tsu_time);
}

/*
 * Detect and select appropriate CLK source for
 * core part of GENERIC TSU.
 */
void select_clk_source()
{
	int32_t sup_clk_sources;
	int i;

	/* get number of supported CLK sources */
	sup_clk_sources = nc_tsu_clk_sources_count(tsu_comp);

	/* automatic selection */
	if (arg_clk_source < 0) {
		/* check supported CLK sources and find first active CLK */
		for (i = sup_clk_sources - 1; i >= 0; i--) {
			nc_tsu_select_clk_source(tsu_comp, i);
			sleep(1);
			if (nc_tsu_clk_is_active(tsu_comp)) {
				syslog(LOG_INFO, "Selected CLK source %i", i);
				return;
			}
		}
		syslog(LOG_INFO, "There is no active CLK source available");
		syslog(LOG_INFO, "Terminating.");
		exit(1);
	/* selection by user */
	} else {
		if (arg_clk_source > (sup_clk_sources - 1)) {
			syslog(LOG_INFO, "Unsupported CLK source %i (highest supported CLK source is %i)",
					arg_clk_source, (sup_clk_sources - 1));
			syslog(LOG_INFO, "Terminating.");
			exit(1);
		} else {
			nc_tsu_select_clk_source(tsu_comp, arg_clk_source);
			syslog(LOG_INFO, "Selected %i CLK source", arg_clk_source);
		}
	}
}

struct timespecx tsx_diff(const struct timespecx t1, const struct timespecx t2)
{
	struct timespecx diff;

	diff.tv_sec = t1.tv_sec - t2.tv_sec;
	diff.tv_xsec = t1.tv_xsec - t2.tv_xsec;

	if (diff.tv_xsec < - (XANOSEC >> 1)) {
		diff.tv_xsec += XANOSEC;
		diff.tv_sec--;
	} else if (diff.tv_xsec > (XANOSEC >> 1)) {
		diff.tv_xsec -= XANOSEC;
		diff.tv_sec++;
	}

	return diff;
}

long double timespecx2us(const struct timespecx t)
{
	return t.tv_sec * 1000000 + xs2us(t.tv_xsec);
}

long double tsx_diff_us(const struct timespecx t1, const struct timespecx t2)
{
	return timespecx2us(t1) - timespecx2us(t2);
}

int get_tsu_sys_timespecx_with_mindiff(struct timespecx *tsu_time, struct timespecx *sys_time)
{
	int j = 0;
	long double min, current;

	struct timespecx tsu0_tsx, tsu1_tsx, sys_tsx;
	l_fp tsu0_fp, tsu1_fp, sys_fp;

	min = SYSTEM_TIME_TIMEOUT;

	// try to get time more times to filter when get_system gets stuck
	for (j = 0; j < GET_SYSTEM_TIME_REPEAT; j++) {
		tsu_get_time(&tsu0_fp);
		sys_get_time(&sys_fp);
		tsu_get_time(&tsu1_fp);

		FP2TX(tsu0_fp, tsu0_tsx);
		FP2TX(tsu1_fp, tsu1_tsx);
		FP2TX(sys_fp, sys_tsx);

		current = tsx_diff_us(tsu1_tsx, tsu0_tsx);
		if (current < min) {
			min = current;
			*tsu_time = tsu0_tsx;
			*sys_time = sys_tsx;
		}
	}

	if (min == SYSTEM_TIME_TIMEOUT)
		return 1;
	return 0;
}

/**
 * Will compute new increment, tries to synchronize real time with system time and get their time difference to zero/deviation of measurement
 * @param dTs difference between previous System time and current System time.
 * @param dTr difference between previous Real time and current Real time.
 * @param offset how much is clock behind - difference between current Real time and current System time
 * @param last_inc last increment
 * @return new increment
 */
long double compute_increment(const long double dTs, const long double dTr, const long double offset, const long double last_inc)
{
	const long double ratio = dTs / dTr;
	long double new_inc = ratio * last_inc;

	/* how many times time ticked during last sleep phase */
	const long double freq = dTr / last_inc;
	/* catch up with system time in CONVERGEIN */
	long double correction = ((offset) / (freq * CONVERGEIN));

	/* there is maximum slow down to catch up with offset, avoid setting inc to zero */
	if (correction > 0 && correction / new_inc > 0.5) {
		correction = new_inc / 2;
	}

	new_inc -= correction;

	/* Would not fit into ireg.f (maximum speedup) */
	if (new_inc > INC_MAX) {
		new_inc = INC_MAX;
	}

	return new_inc;
}

/*
 * adjust INC_REG according to diffs between system time and real time
 */
void adj_clock_system(struct tclock *cl)
{
	long double new_inc;
	uint64_t incr;

	/* calculate how much time passed in a card and in a system */
	struct timespecx dTs = tsx_diff(cl->currentSystemTime, cl->lastSystemTime);
	struct timespecx dTr = tsx_diff(cl->currentRealTime, cl->lastRealTime);

	/* calculate how much time is card behind/ahead */
	const long double offset = tsx_diff_us(cl->currentRealTime, cl->currentSystemTime);
	const long double lastOffset = tsx_diff_us(cl->lastRealTime, cl->lastSystemTime);

	printf("TSU-SYS off: %+1.6Lf us (change from prev: %+1.6Lf us)\n", offset, lastOffset - offset);

	/* compute new increment */
	new_inc = compute_increment(timespecx2us(dTs), timespecx2us(dTr), offset, frac64b2nsd(cl->incr) * 1e-9);

	incr = cl->incr;
	cl->incr = double2frac64(new_inc);
	nc_tsu_set_inc(tsu_comp, cl->incr);

	printf("TSU set inc: 0x%02x:%08x (change from prev: %+6d)\n", 
			(unsigned int)(cl->incr >> 32), (unsigned int)(cl->incr & 0xFFFFFFFF), (int)(cl->incr - incr));
	
	printf("TSU inc:     %.9Lf ns, %.6Lf MHz, drift: %+8.3Lf ppm\n",
			frac64b2nsd(cl->incr), 1000.0f/frac64b2nsd(cl->incr), 
			(1000000000.f/frac64b2nsd(cl->incr) - cl->tsu_gen_frequency));
}

void engine_system(struct tclock *cl)
{
	struct timespecx tsu_time, sys_time;
	struct timespecx diff;

	while (RUN) {
		if (get_tsu_sys_timespecx_with_mindiff(&tsu_time, &sys_time))
			continue;

		printf("\n");
		print_tsx("TSU time:    ", tsu_time, " (UTC)\n");
		print_tsx("System time: ", sys_time, " (UTC)\n");

		/* comparison: systime - RTR time */
		diff = tsx_diff(sys_time, tsu_time);
		if (abs(diff.tv_sec) > MAX_DIVERGENCE) {
			l_fp fp;

			printf("Time diverged too much - reseting TSU time to system time\n");
			sys_get_time(&fp);
			tsu_set_time(&fp);
			usleep(SLEEPTIME);
			continue;
		}

		cl->currentRealTime = tsu_time;
		cl->currentSystemTime = sys_time;

		adj_clock_system(cl);

		cl->lastRealTime = tsu_time;
		cl->lastSystemTime = sys_time;

		usleep(SLEEPTIME);
	}
}

void tsu_init(struct tclock *cl)
{
	l_fp fp;

	/* syslog init  */
	openlog(PROGNAME, LOG_PERROR, LOG_DAEMON);

	select_clk_source();

	cl->tsu_gen_frequency = nc_tsu_get_frequency(tsu_comp);

	if (cl->tsu_gen_frequency <= 0) {
		syslog(LOG_INFO, "Component frequency is not valid.");
		syslog(LOG_INFO, "Terminating.");
		exit(1);
	}

	syslog(LOG_INFO, "Component core frequency: %f MHz", cl->tsu_gen_frequency / 1E6f);

	/* init TSU increment to reasonable value */
	cl->incr = double2frac64(1. / cl->tsu_gen_frequency);
	nc_tsu_set_inc(tsu_comp, cl->incr);

	/* init TSU time to current system time */
	sys_get_time(&fp);
	tsu_set_time(&fp);

	nc_tsu_enable(tsu_comp);
	syslog(LOG_INFO, "TSU enabled - start generating valid timestamps");

	/* run in background */
	if (!arg_debug) {
		printf("Moving to background\n\n");
		/* no chdir, std redirection to /dev/null */
		if (daemon(1, 0) < 0) {
			perror("daemon() failed");
			exit(1);
		}
	}
}

void tsu_deinit()
{
	if (dev) {
		if (tsu_comp) {
			nc_tsu_disable(tsu_comp);
			nc_tsu_unlock(tsu_comp);
			nc_tsu_close(tsu_comp);
			tsu_comp = NULL;

			syslog(LOG_INFO, "TSU disabled - stop generating valid timestamps");
		}

		nfb_close(dev);
		dev = NULL;
	}

	syslog(LOG_INFO, "Terminating.");
	closelog();
}

void tsu_stop(int signum)
{
	printf("Stopping " PROGNAME "...\n");
	RUN = 0;
	signal(signum, tsu_stop);
}

#define ARGUMENTS              "c:d:i:Dh"

void usage()
{
	printf("Usage: %s [-Dh] [-d path] [-i index]\n", PROGNAME);
	printf("-c source    Select CLK source (higher the number -> more accurate CLK source)\n");
	printf("-d path      Use device file, instead of default %s\n", NFB_DEFAULT_DEV_PATH);
	printf("-i index     Set index of the TSU component [default: 0]\n");
	printf("-D           Debug mode (run in foreground)\n");
	printf("-h           Show this text\n");
}

int main(int argc, char *argv[])
{
	char *path = NFB_DEFAULT_DEV_PATH;
	char c;
	long param;
	int index = 0;

	while ((c = getopt(argc, argv, ARGUMENTS)) != -1) {
		switch (c) {
			case 'c': /* Select source CLK signal */
				if (nc_strtol(optarg, &param) || param > 1 || param < 0)
					errx(EINVAL, "Invalid CLK source. Please specify number 0 or 1.");
				arg_clk_source = param;
				break;

			case 'd':
				path = optarg;
				break;

			case 'i':
				if (nc_strtol(optarg, &param) || param < 0)  {
					errx(EXIT_FAILURE, "Wrong index.");
				}
				index = param;
				break;

			case 'D': /* Set debug mode */
				arg_debug = 1;
				break;

			case 'h':
				usage();
				return 0;

			default:
				errx(1, "unknown argument -%c", optopt);
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 0) {
		errx(0, "extra argument");
		return 1;
	}

	/* register TSU_CV2 component deinit function while interrupted or when error */
	signal(SIGHUP, tsu_stop);
	signal(SIGQUIT, tsu_stop);
	signal(SIGINT, tsu_stop);
	signal(SIGTERM, tsu_stop);
	atexit(tsu_deinit);

	/* attach device and map address spaces */
	dev = nfb_open(path);
	if (dev == NULL) {
		errx(1, "nfb_open failed");
	}

	int node = nfb_comp_find(dev, COMP_NETCOPE_TSU, index);
	if (node < 0) {
		errx(1, "cannot find TSU in DeviceTree");
	}

	tsu_comp = nc_tsu_open(dev, node);
	if (tsu_comp == NULL) {
		errx(1, "cannot open TSU");
	}

	if (!nc_tsu_lock(tsu_comp)) {
		errx(1, "Getting lock for TSU failed. Another instance of %s is probably running.", PROGNAME);
	}

	tsu_init(&ptm_clock);

	engine_system(&ptm_clock);

	/* deinit is performed using atexit */

	return 0;
}
