#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mempool.h>
#include <rte_malloc.h>
#include <wordexp.h>
#include <stdio.h>
#include <getopt.h>
#include <nfb/nfb.h>
#include <netcope/nccommon.h>
#include "../common.h"
#include "./dpdk_tools_common.h"

#define MAX_CPU_COUNT 99
#define DEFAULT_MEMPOOL_SIZE 65536
#define MULTI_MEMPOOL_SIZE 4096
#define HWRING_SIZE 2048
#define DEFAULT_MEMPOOL_CACHE_SIZE 256
#define DEFAULT_PKT_SIZE 1518
#define NAME_BUFF_LEN 50

static int use_multipool = false;
static int use_native = false;
static int clear_data = false;
static uint16_t tx_desc = HWRING_SIZE;
static uint16_t rx_desc = HWRING_SIZE;
static uint32_t pool_size = 0;
static uint32_t mbuf_size = DEFAULT_PKT_SIZE;
static uint32_t pool_cache = DEFAULT_MEMPOOL_CACHE_SIZE;
static uint32_t burst_size;

void dpdk_generate_destroy(struct ndp_tool_params *p);
int dpdk_generate_loop(void *params);
int dpdk_generate_run_single(struct ndp_tool_params *p);
int dpdk_generate_check(struct ndp_tool_params *p);
void *dpdk_generate_run_thread(void *tmp);
void dpdk_generate_print_help();
int _dpdk_generate_parse_app_opt(int argc, char **argv);

int dpdk_generate_run_single(struct ndp_tool_params *p)
{
	int core_id;
	int ret = -ENODEV;
	p->update_stats = update_stats;
	p->si.progress_letter = 'G';
	gettimeofday(&p->si.startTime, NULL);
	RTE_LCORE_FOREACH_WORKER(core_id) {
		ret = rte_eal_remote_launch(dpdk_generate_loop, p, core_id);
		if (ret < 0) {
			fprintf(stderr, "rte_eal_remote_launch() failed: %d\n", ret);
			goto launch_fail;
		}
		break;
	}
	rte_eal_mp_wait_lcore();

launch_fail:
	return ret;
}

void *dpdk_generate_run_thread(void *tmp)
{
	struct thread_data **thread_data = tmp;
	struct ndp_tool_params *p = &(*thread_data)->params;
	unsigned thread_counter = 0;
	int core_id;
	int ret;

	if ((*thread_data)->thread_id != 0) {	// in dpdk we can only have one main thread spawning lcores
		return NULL;			// but we need other threads for integration into ndp-tool;
	}					// also the main thread must be id = 0 else segfault will occur

	RTE_LCORE_FOREACH_WORKER(core_id) {
		while(thread_counter < p->mode.dpdk.queue_count) {
			if (thread_data[thread_counter]->params.queue_index < 0) { 
				thread_counter++;
				continue;
			}
			thread_data[thread_counter]->state = TS_RUNNING;
			thread_data[thread_counter]->params.update_stats = update_stats_thread;
			thread_data[thread_counter]->params.si.progress_letter = 'G';
			gettimeofday(&thread_data[thread_counter]->params.si.startTime, NULL);
			ret = rte_eal_remote_launch(dpdk_generate_loop, &thread_data[thread_counter]->params, core_id);
			thread_data[thread_counter]->ret = ret;
			if (ret < 0) {
				fprintf(stderr, "rte_eal_remote_launch() failed: %d\n", ret);
			}
			thread_counter++;
			break;
		}
	}
	rte_eal_mp_wait_lcore();
	for (thread_counter = 0; thread_counter < p->mode.dpdk.queue_count; thread_counter++) {
		thread_data[thread_counter]->state = TS_FINISHED;
	}
	
	return NULL;
}

int dpdk_generate_loop(void *params)
{ 
	struct ndp_tool_params *p = params;
	struct ndp_mode_dpdk_params dpdk_params = p->mode.dpdk;
	unsigned queue_idx = p->queue_index;
	struct ndp_mode_dpdk_queue_data queue_data = dpdk_params.queue_data_arr[queue_idx];
	unsigned port_id = queue_data.port_id;
	unsigned queue_id = queue_data.queue_id;
	unsigned cnt = 0;
	unsigned brst_size = burst_size;
	struct rte_mempool *pool = queue_data.pool;
	struct ndp_packet statpackets[brst_size];
	struct rte_mbuf *packets[brst_size];
	struct stats_info *si = &p->si;
	update_stats_t update_stats = p->update_stats;
	int ret = 0;
	int gen_index = 0;
	int clr_data = clear_data;
	unsigned i;
	enum progress_type progress_holder = si->progress_type;

	unsigned long long bytes_cnt = 0;
	unsigned long long packets_rem = p->limit_packets;

	const bool limit_bytes   = p->limit_bytes > 0 ? true : false;
	const bool limit_packets = p->limit_packets > 0 ? true : false;

	/* Clear length of packet header */
	for (i = 0; i < brst_size; i++) {
		statpackets[i].header_length = 0;
		statpackets[i].flags = 0;
	}

	/* OPT: Set the constant values in packet for one packet length */
	if (p->mode.dpdk.range.items == 1 && p->mode.dpdk.range.max[0] == 0) {
		gen_index = -1;
		for (i = 0; i < brst_size; i++) {
			statpackets[i].data_length = p->mode.dpdk.range.min[0];
		}
	}

	while (!stop) {
		if (limit_packets > 0) {
			/* Packet limit was reached in previous burst */
			if (packets_rem == 0) {
				break;
			}
			/* Packet limit will be reached in one burst */
			if (packets_rem < brst_size) {
				brst_size = packets_rem;
			}
		}
		
		if (limit_bytes > 0) {
			if (bytes_cnt >= p->limit_bytes) {
				break;
			}
		}

		// allocate the packets
		ret = rte_pktmbuf_alloc_bulk(pool, packets, brst_size);
		if (ret < 0){
			cnt = 0;
			RTE_LOG(DEBUG, MBUF, "rte_pktmbuf_alloc_bulk() failed\n");
			while(rte_pktmbuf_alloc_bulk(pool, packets, brst_size)) {
				if (cnt > 100) {
					RTE_LOG(CRIT, MBUF, "THREAD %d: rte_pktmbuf_alloc_bulk() failed 100 times in a row, killing the thread\n", rte_lcore_id());
					goto alloc_fail;
				}
				++cnt;
				rte_delay_us_sleep(10);
			}
		}

		/* Fill parameters for packets to send */
		if (gen_index != -1) {
			for (i = 0; i < brst_size; i++) {
				statpackets[i].data_length = p->mode.dpdk.range.min[gen_index];
				if (p->mode.dpdk.range.max[gen_index])
					statpackets[i].data_length += nc_fast_rand(&p->mode.dpdk.srand) % p->mode.dpdk.range.max[gen_index];
				if ((unsigned) ++gen_index == p->mode.dpdk.range.items)
					gen_index = 0;
			}
		}

		for (i = 0; i < brst_size; i++) {
			rte_pktmbuf_append(packets[i], statpackets[i].data_length);
			statpackets[i].data = rte_pktmbuf_mtod(packets[i], unsigned char *);
		}

		// theoretically this shouldn't ever be necessary, it's here for compatibility
		if (clr_data) {
			for (i = 0; i < cnt; i++) {
				memset(statpackets[i].data, 0, statpackets[i].data_length);
			}
		}

		// this allows for printing packets without copying the data
		// we cannot guarantee that printed packets were successfully sent
		si->progress_type = progress_holder;
		if (si->progress_type != PT_NONE && si->progress_type != PT_LETTER) {
			for (i = 0; i < brst_size; i++) {
				print_packet(&statpackets[i], si);
			}
			si->progress_type = PT_NONE;
		}

		cnt = rte_eth_tx_burst(port_id, queue_id, packets, brst_size);

		/* Update limits */
		packets_rem -= cnt;
		if (limit_bytes) {
			for (i = 0; i < cnt; i++) {
				bytes_cnt += packets[i]->data_len;
			}
		}

		update_stats(statpackets, cnt, si);

		if (unlikely(cnt != brst_size)){
			for (unsigned i = cnt; i < brst_size; i++){
				rte_pktmbuf_free(packets[i]);
			}
			rte_delay_us_sleep(1);
		}
	}

	gettimeofday(&p->si.endTime, NULL);
	update_stats(0, 0, si);
alloc_fail:
	return ret;
}

int dpdk_generate_init(struct ndp_tool_params *p)
{
	int ret;
	list_range_init(&p->mode.dpdk.range);
	wordexp_t *args = &p->mode.dpdk.args;
	ret = wordexp("DPDK_GENERATE", args, 0);
	return ret;
}

int dpdk_generate_check(struct ndp_tool_params *p)
{	
	int ret;
	int port_id;
	unsigned queue_idx = 0;
	unsigned queue_id = 0;
	unsigned tx_num = 0;
	unsigned rx_num = 0;
	struct rte_eth_dev_info dev_info;
	struct rte_mempool *pool[MAX_CPU_COUNT];
	struct ndp_mode_dpdk_params *dpdk_params = &p->mode.dpdk;
	struct list_range *queue_range = &dpdk_params->queue_range;
	struct ndp_mode_dpdk_queue_data *queue_data_arr;
	wordexp_t *args = &dpdk_params->args;
	unsigned argc = args->we_wordc + 1;
	char **argv;
	char **arg_app;
	char pool_name_buff[NAME_BUFF_LEN];
	unsigned i;
	const char *device_path;
	char addr_buff[NAME_BUFF_LEN];
	int core_id = -1;
	int socket_id;
	unsigned queues_started = 0;
	unsigned pools_assigned = 0;

	burst_size = TX_BURST;
	
	//rte_log_set_global_level(RTE_LOG_CRIT);

	// handling eal/app args
	argv = malloc(sizeof(char *) * argc);
	if (!argv) {
		ret = -ENOMEM;
		goto pre_eal_fail;
	}
	argv[0] = args->we_wordv[0];
	argv[1] = addr_buff;
	for (i = 2; i < argc; i++) {
		argv[i] = args->we_wordv[i - 1];
	}

	ret = dpdk_get_dev_path(p, &device_path);
	if (ret) {
		goto pre_eal_fail;
	}

	if (use_native) {
		sprintf(addr_buff ,"-a%s,queue_driver=native", device_path);
	} else {
		sprintf(addr_buff ,"-a%s", device_path);
	}

	ret = list_range_empty(&p->mode.dpdk.range);
	if (ret) {
		fprintf(stderr, "Unspecified size parameter\n");
		goto pre_eal_fail;
	}

	for (i = 0; i < p->mode.dpdk.range.items; i++) {
		p->mode.dpdk.range.max[i] -= p->mode.dpdk.range.min[i];
		/* Increase by 1 if it's a real range (not one value) for random values including max */
		if (p->mode.dpdk.range.max[i])
			p->mode.dpdk.range.max[i]++;
	}

	ret = rte_eal_init(argc, argv);
	if (ret < 0) {
		fprintf(stderr, "rte_eal_init() failed: %d\n", ret);
		goto pre_eal_fail;
	}

	argc -= ret;
	arg_app = argv + ret;

	if (argc > 1) {
		ret = _dpdk_generate_parse_app_opt(argc, arg_app);
		if (ret) {
			fprintf(stderr, "_dpdk_generate_parse_app_opt failed: %d\n", ret);
			goto eal_fail;	
		}
	}

	if (!pool_size) {
		if (use_multipool)
			pool_size = MULTI_MEMPOOL_SIZE;
		else
			pool_size = DEFAULT_MEMPOOL_SIZE;
	}

	ret = dpdk_get_queues_available(&dpdk_params->queues_available);
	if (ret){
		goto eal_fail;
	}

	// This queue count is purely for compatibility with the already existing tools
	// It doesn't check if user inputted queues really exist and that's intentional
	dpdk_params->queue_count = list_range_count(queue_range);
	if (dpdk_params->queue_count == 0) {
		list_range_add_range(queue_range, 0, dpdk_params->queues_available);
		dpdk_params->queue_count = dpdk_params->queues_available;
	}

	ret = dpdk_queue_data_init(dpdk_params);
	if (ret) {
		fprintf(stderr, "_dpdk_generate_queue_data_init() failed: %d\n", ret);
		goto eal_fail;
	}

	queue_data_arr = dpdk_params->queue_data_arr;

	if (!use_multipool) {
		memset(pool, 0, sizeof(pool));
		for (i = 0; i < rte_socket_count(); i++) {
			sprintf(pool_name_buff, "pool%d", i);
			pool[i] = rte_pktmbuf_pool_create(
						pool_name_buff,
						pool_size,
						pool_cache,
						0,
						mbuf_size + RTE_PKTMBUF_HEADROOM,
						rte_socket_id_by_idx(i));
			if (pool[i] == NULL || rte_socket_id_by_idx(i) == -1) {
				ret = -rte_errno;
				fprintf(stderr, "rte_pktmbuf_pool_create() failed: %d\n", ret);
				goto setup_fail;
			}
		}
	}

	// In this loop we setup every queue, which together with deferred start flag
	// gives us controll over which queues exactly are going to be used 
	// (it's not possible to use queue_id=3 at port_id=1 without setting up at least 3 queues at that port)
	// this is necessary because native mode doesn't allow multiple apps to start the same queue
	RTE_ETH_FOREACH_DEV(port_id) {
		if(!rte_eth_dev_is_valid_port(port_id)) {
			continue;
		}

		ret = rte_eth_dev_info_get(port_id, &dev_info);
		if (ret < 0) {
			fprintf(stderr, "rte_eth_dev_info_get() failed: %d\n", ret);
			goto setup_fail;
		}

		struct rte_eth_conf eth_conf;
		memset(&eth_conf, 0, sizeof(eth_conf));
		eth_conf.link_speeds = dev_info.speed_capa;

		tx_num = dev_info.max_tx_queues;

		ret = rte_eth_dev_configure(port_id, rx_num, tx_num, &eth_conf);
		if (ret < 0) {
			fprintf(stderr, "rte_eth_dev_configure() failed: %d\n", ret);
			goto setup_fail;
		}

		ret = rte_eth_dev_adjust_nb_rx_tx_desc(port_id, &rx_desc, &tx_desc);
		if (ret < 0) {
			fprintf(stderr, "rte_eth_dev_adjust_nb_rx_tx_desc() failed: %d\n", ret);
			goto setup_fail;
		}

		for (queue_id = 0; queue_id < dev_info.max_tx_queues && queue_idx < dpdk_params->queues_available; queue_id++, queue_idx++) {
			struct rte_eth_txconf tx_conf;
			tx_conf = dev_info.default_txconf;
			tx_conf.tx_deferred_start = 1;

			core_id = rte_get_next_lcore(core_id, 1, 1);
			socket_id = rte_lcore_to_socket_id(core_id);

			ret = rte_eth_tx_queue_setup(
				port_id,
				queue_id,
				tx_desc,
				socket_id,
				&tx_conf);
			if (ret < 0) {
				fprintf(stderr, "rte_eth_tx_queue_setup() failed: %d\n", ret);
				goto setup_fail;
			}

			queue_data_arr[queue_idx].port_id = port_id;
			queue_data_arr[queue_idx].queue_id = queue_id;

			if (list_range_contains(queue_range, queue_idx) && pools_assigned < rte_lcore_count() - 1) {
				if (!use_multipool) {
					if (socket_id == -1) {
						queue_data_arr[queue_idx].pool = pool[queue_idx % rte_socket_count()];
					} else {
						for (i = 0; i < rte_socket_count(); i++) {
							if (rte_socket_id_by_idx(i) == socket_id) {
								queue_data_arr[queue_idx].pool = pool[i];
							}
						}
					}
					pools_assigned++;
				} else {
					sprintf(pool_name_buff, "pool%d", queue_idx);
					queue_data_arr[queue_idx].pool = rte_pktmbuf_pool_create(
						pool_name_buff,
						pool_size,
						0,
						0,
						mbuf_size + RTE_PKTMBUF_HEADROOM,
						socket_id);
					if (!queue_data_arr[queue_idx].pool) {
						ret = -rte_errno;
						fprintf(stderr, "rte_pktmbuf_pool_create() failed: %d\n", ret);						
						goto setup_fail;
					}
					pools_assigned++;
				}
			}
		}

		ret = rte_eth_dev_start(port_id);
		if (ret < 0) {
			fprintf(stderr, "rte_eth_dev_start() has failed: %d\n", ret);
			goto setup_fail;
		}
	}

	// starting the queues
	queue_idx = 0;
	RTE_ETH_FOREACH_DEV(port_id) {
		if(!rte_eth_dev_is_valid_port(port_id)) {
			continue;
		}

		ret = rte_eth_dev_info_get(port_id, &dev_info);
		if (ret < 0) {
			fprintf(stderr, "rte_eth_dev_info_get() failed: %d\n", ret);
			goto setup_fail;
		}

		for (queue_id = 0; queue_id < dev_info.max_tx_queues; queue_id++, queue_idx++) {
			if (list_range_contains(queue_range, queue_idx) && queues_started < rte_lcore_count() - 1) {
				ret = rte_eth_dev_tx_queue_start(port_id, queue_id);
				if (ret < 0) {
					fprintf(stderr, "rte_eth_dev_tx_queue_start() failed: %d\n", ret);
					goto setup_fail;
				}
				queues_started++;
			}
		}
	}

	free(argv);
	wordfree(&p->mode.dpdk.args);
	return 0;

setup_fail:
	if (use_multipool) {
		for (i = 0; i < dpdk_params->queues_available; i++) {
			if (list_range_contains(queue_range, i)) {
				if (!queue_data_arr[i].pool)
					break;
				rte_mempool_free(queue_data_arr[i].pool);
			}
		}
	} else {
		for (i = 0; pool[i] && i < rte_socket_count(); i++) {
			rte_mempool_free(pool[i]);
		}
	}
eal_fail:
	rte_eal_cleanup();
pre_eal_fail:
	free(argv);
	wordfree(&p->mode.dpdk.args);
	return ret;
}

void dpdk_generate_destroy(struct ndp_tool_params *p) 
{
	struct ndp_mode_dpdk_params dpdk_params = p->mode.dpdk;
	struct ndp_mode_dpdk_queue_data *queue_data_arr = dpdk_params.queue_data_arr;
	unsigned queue_avail = dpdk_params.queues_available;
	unsigned port_id;
	unsigned queue_idx = 0;
	unsigned i;
	int ret;

	if (!queue_data_arr) {
		free(queue_data_arr);
	}

	RTE_ETH_FOREACH_DEV(port_id){
		if (!rte_eth_dev_is_valid_port(port_id)) {
			continue;
		}

		ret = rte_eth_dev_stop(port_id);
		if (ret < 0) {
			fprintf(stderr, "rte_eth_dev_stop() failed: %d\n", ret);
		}
		
		ret = rte_eth_dev_close(port_id);
		if (ret < 0) {
			fprintf(stderr, "rte_eth_dev_close() failed: %d\n", ret);
		}
	}

	if (use_multipool) {
		queue_avail = dpdk_params.queues_available;
		for (queue_idx = 0; queue_idx < queue_avail; queue_idx++) {
			if (list_range_contains(&dpdk_params.queue_range, queue_idx)) {
				if (!queue_data_arr[queue_idx].pool)
						break;
				rte_mempool_free(queue_data_arr[queue_idx].pool);
			}
		}
	} else {
		for (i = 0; i < rte_socket_count(); i++) {
			for (queue_idx = 0; queue_idx < queue_avail; queue_idx++) {
				if ((unsigned) rte_socket_id_by_idx(i) == rte_lcore_to_socket_id(queue_idx + 1) && list_range_contains(&dpdk_params.queue_range, queue_idx)) {
					rte_mempool_free(queue_data_arr[queue_idx].pool);
					break;
				}
			}
		}
	}

	list_range_destroy(&p->mode.dpdk.range);
	rte_eal_cleanup();
}


int dpdk_generate_parseopt(struct ndp_tool_params *p, int opt, char *optarg)
{
	wordexp_t *args = &p->mode.dpdk.args;

	switch (opt) {
	case 'a':
		wordexp(optarg, args, WRDE_APPEND);
		break;
	case 'n':
		use_native = true;
		break;
	case 's':
		if (list_range_parse(&p->mode.dpdk.range, optarg) < 0) {
			fprintf(stderr, "Cannot parse size range\n");
			return -EINVAL;
		}
		break;
	case 'C':
		clear_data = true;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

int _dpdk_generate_parse_app_opt(int argc, char **argv) {
	int ret = 0;
	int opt_index=0;

	const struct option long_opts[] = {
		{"multipool", no_argument, 0, 'm'},
		{"mbuf-size", required_argument, 0, 's'},
		{"pool-size", required_argument, 0, 'p'},
		{"tx-desc", required_argument, 0, 't'},
		{"pool-cache", required_argument, 0, 'c'},
		{0, 0, 0, 0},
	};

	optind = 1;
	while (true) {
		ret = getopt_long(argc, argv, "s:p:t:c:m", long_opts, &opt_index);
		if (ret == -1)
			break;

		switch (ret) {
		case 's':
			mbuf_size = atoi(optarg);
			break;
		case 'p':
			pool_size = atoi(optarg);
			break;
		case 't':
			tx_desc = atoi(optarg);
			break;
		case 'c':
			pool_cache = atoi(optarg);
			break;
		case 'm':
			use_multipool = true;
			break;
		case '?':
			return -EINVAL;
		default: 
			return -EINVAL;
		}
	}

	return 0;
}

void dpdk_generate_print_help()
{
	printf("-----------------------------------------------------------------\n");
	printf("DPDK Generate parameters:\n");
	printf("  -a \"\" 	pass arguments to dpdk - format in quotation marks: \"EAL params -- App params\" the '--' should not be used if no App params are present\n");
	printf("  -n		use driver in native mode (much faster but two apps cannot use the same queue at the same time)\"\n");
	printf("  -s size       Packet size - list or random from range, e.g \"64,128-256\"\n");
	printf("  -C            Clear packet data before send (shouldn't be necessary, has performance penalty)\n");
	printf("-----------------------------------------------------------------\n");
	printf("Usefull EAL params: (these params go into -a\" here -- xxx \")\n");
	printf("  --file-prefix	prefix		 	allows to run multiple apps at the same time using differnet prefixes\n");
	printf("  -l corelist 				tells dpdk which lcores to use. Usefull for running multiple apps. (-l 0,5-16)\n");
	printf("  --lcores coremask			sets lcores to cpus - format '(0-32)@0' for lcores 0-32 to run at cpu 0, '(' and ')' needs to be escaped in cmd either by \\ or ''\n");
	printf("					'(3-5)@1,6@7,(8-9)@8' runs lcores 3-5 at cpu 1, lcore 6 at cpu 7 and lcores 8-9 at cpu 8\n");
	printf("					'(0-32)@(0-31)' runs 32 queues on 31 core cpu\n");
	printf("					- use multipool option when running multiple threads on the same cpu\n");
	printf("  --main-lcore core_ID			tells dpdk which lcore is to be used for main - defult core 0\n");
	printf("  --help 		-h		EAL help (-a \"-h\")\n");
	printf("-----------------------------------------------------------------\n");
	printf("App params: (these params go into -a\" xxx -- here \")\n");
	printf("  --multipool		-m		mulutipool mode - allocate one mempool for each queue default is one mempool per socket\n");
	printf("  --mbuf-size		-s size		mbuf size - size of one mbuf (packet buffer) in pool - default 1518\n");
	printf("  --pool-size		-p size		pool size - number of mbufs in the mempool default 64 * 512\n");
	printf("  --pool-cache		-c size		pool cache size - default 128 mbufs, it is 0 in multipool mode - reserves cache in the mempool to use for each queue\n");
	printf("					too high values will result in error - maximum seems to be 512\n");
	printf("  --tx-desc		-t size		number of tx descriptors to use with each queue - default 2048 - has to be 2^n\n");
	printf("-----------------------------------------------------------------\n");
	printf("Important notes:\n");
	printf("  -To use DPDK app you must first setup hugepages (use 'dpdk-hugepages' tool)\n");
	printf("  -DPDK app must be run with root permissions to access hugepages\n");
	printf("  -Parameter '-D' can have a performance penalty, which can be mitigated by better mempool / descriptor settings\n");
	printf("  -From testing ideal pool size is >=(2 * descriptors * queues) \\ cpu sockets for normal mode, for multipool: >=2 * descriptors\n");
	printf("  -When using many queues it's best to keep poolsizes and descriptors to minimum else they will take up too much memory and performance will tank.\n");
	printf("Known issues:\n");
	printf("  -When multiple queues use the same mempool and the mempool doesn't have enough buffers then threads can get stuck waiting on a spinlock for accesing mempool.\n");
	printf("  	(It seems that you have to acquire the lock to free mbufs, issue 95%% happens when you run 32 lcores on one cpu)\n");
	printf("  	Running many queues at very low cpu count makes this issue noticable, in the worst cases the app has to be killed.\n");
	printf("  	To go around this use reccomended descriptor to mbufs ratio or multimempool mode\n");
	printf("  	To partialy solve this i made protection that kills the thread when allocation fails for 100 times\n");
}