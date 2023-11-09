/* SPDX-License-Identifier: GPL-2.0 */
/*
 * NDP driver of the NFB platform - private header
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef NDP_PRIV_FILE_FILE
#define NDP_PRIV_FILE_FILE

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/poll.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE <= KERNEL_VERSION(4,10,0)
#include <linux/sched.h>
#else
#include <linux/sched/signal.h>
#endif
#include <linux/signal.h>

#include <linux/nfb/ndp.h>

#define NDP_SUB_STATUS_INIT		0
#define NDP_SUB_STATUS_SUBSCRIBED	1
#define NDP_SUB_STATUS_RUNNING		2

#define NDP_WAKE_RX                     1

struct nfb_device;
struct nfb_comp;

struct ndp;
struct ndp_channel;
struct ndp_subscriber;

/**
 * struct ndp_block - ring buffer page descriptor
 *
 * @size: virtual address of the page
 * @virt: virtual address of the page
 * @phys: physical (DMA-ble) address of the page
 */
struct ndp_block {
	size_t size;
	void *virt;
	dma_addr_t phys;
};
/**
 * struct ndp_ring - szedata per channel info
 *
 * @area: descriptor for each area (heads, tails, ...)
 * @areas: areas per space (size of @adesc array)
 * @blk_count: overall count of blocks in this space
 * @blk_size: block size
 * @ring: pointers to ring blocks
 */
struct ndp_ring {
	size_t size;
	size_t mmap_size;
	size_t mmap_offset;
	size_t block_count;
	struct ndp_block *blocks;
	struct device *dev;
	void *vmap;
};

struct ndp_ctrl;

struct ndp_subscription {
	struct ndp_channel *channel;
	int status;		// unknown, init, started, stopped, ...

	struct list_head list_item;
	struct list_head ndp_subscriber_list_item;
	unsigned long hwptr;
	unsigned long swptr;

	struct ndp_subscriber *subscriber;
};

struct ndp_subscriber {
	struct ndp *ndp;
	struct list_head list_head;
	struct list_head list_head_subscriptions;
	wait_queue_head_t poll_wait;
	struct hrtimer poll_timer;
	unsigned long wake_reason;
};

struct ndp_channel_ops {
	int (*start)(struct ndp_channel *channel, uint64_t *hwptr);
	int (*stop)(struct ndp_channel *channel, int force);
	int (*attach_ring)(struct ndp_channel *channel);
	void (*detach_ring)(struct ndp_channel *channel);
	uint64_t (*get_hwptr)(struct ndp_channel *channel);
	void (*set_swptr)(struct ndp_channel *channel, uint64_t ptr);
	uint64_t (*get_flags)(struct ndp_channel *channel);
	uint64_t (*set_flags)(struct ndp_channel *channel, uint64_t flags);
	uint64_t (*get_free_space)(struct ndp_channel *channel);
};

struct ndp_channel_id {
	int index : 30;
	int type : 2;
};

/**
 * struct ndp_channel - channel descriptor
 *
 * @roffset: offset in ring space
 * @asize: block size * blks (pointers modulo)
 * @timeout: current timeout (for adaptive timeout)
 * @poll_thresh: after how much data wake up applications
 * @start_count: how many times it was started
 * list_app: list_head with
 * list_subscriptions: list_head with active subscriptions
 * list_sd: list item in ndp structure
 *
 */
struct ndp_channel {
	struct ndp_channel_ops *ops;
	spinlock_t lock;
	struct mutex mutex;
	struct ndp_subscription *locked_sub;
	uint64_t hwptr;
	uint64_t swptr;
	uint64_t ptrmask;

	uint32_t start_count;
	uint32_t subscriptions_count;
	uint32_t flags;

	struct ndp_ring ring;

	struct list_head list_subscriptions;
	struct list_head list_ndp;

	struct device dev;
	struct ndp *ndp;
	struct ndp_channel_id id;
};

/**
 * struct ndp - ndp information holder
 *
 * The structure holds all needed information to know about ndp.
 * Including application list, HW-specific hooks, ring buffer
 * descriptors etc.
 *
 * @open, @close, @start and @stop are guaranteed to be called serialized
 * (under a lock).
 *
 * @cdev: /dev/szedataII node information
 * @app_list: list of application being on @cdev
 * @open_count: how many openers are there
 * @dev: device itself (for chardev and attributes)
// * @devxxx: real dev device, e.g. a pci device
 * @owner: who is HW part
 * @set_intr: to set interrupt values
 * @destroy: called when ndp is about to dismiss
 * @private: contains pointer to data of private_size (ndp_alloc param)
 */
struct ndp {
	struct nfb_device *nfb;

	struct list_head list_channels;
	struct list_head list_subscribers;
	struct mutex lock;

	struct device dev;
};

/* driver.c */
int nfb_ndp_attach(struct nfb_device* nfb, void **priv);
void nfb_ndp_detach(struct nfb_device* nfb, void *priv);

/* char.c */
int ndp_char_open(void *priv, void **app_priv, struct file *file);
void ndp_char_release(void *priv, void *app_priv, struct file *file);
long ndp_char_ioctl(void *priv, void *app_priv, struct file *file, unsigned int cmd, unsigned long arg);

/* ring.c */
int ndp_ring_mmap(struct vm_area_struct *vma, unsigned long offset, unsigned long size, void*priv);
ssize_t ndp_channel_get_ring_size(struct device *dev, struct device_attribute *attr, char *buf);
ssize_t ndp_channel_set_ring_size(struct device *dev, struct device_attribute *attr, const char *buf, size_t size);

extern const struct kernel_param_ops ndp_param_size_ops;

/* channel.c */
void ndp_channel_init(struct ndp_channel *channel, struct ndp_channel_id);
int ndp_channel_add(struct ndp_channel *channel, struct ndp *ndp, uint32_t phandle);
void ndp_channel_del(struct ndp_channel *channel);
int ndp_channel_subscribe(struct ndp_subscription *sub, uint32_t *flags);
void ndp_channel_unsubscribe(struct ndp_subscription *sub);

extern struct ndp_subscriber *ndp_open(struct ndp *sd);
extern unsigned int ndp_poll(struct ndp_subscriber *app);
extern void ndp_close(struct ndp_subscriber *app);

int ndp_channel_ring_create(struct ndp_channel *channel, struct device *dev,
		 size_t block_count, size_t block_size);
void ndp_channel_ring_destroy(struct ndp_channel *channel);
int ndp_channel_ring_resize(struct ndp_channel *channel, size_t size);

ssize_t ndp_channel_get_discard(struct device *dev, struct device_attribute *attr, char *buf);
ssize_t ndp_channel_set_discard(struct device *dev, struct device_attribute *attr, const char *buf, size_t size);

int ndp_subscription_start(struct ndp_subscription *sub,
	struct ndp_subscription_sync *sync);
int ndp_subscription_stop(struct ndp_subscription *sub, int force);

int ndp_subscription_sync(struct ndp_subscription *sub,
		struct ndp_subscription_sync *sync);

size_t ndp_subscription_rx_data_available(struct ndp_subscription *sub);

int ndp_subscribe_channel(struct ndp_subscription *sub,
		struct ndp_channel_request *req);

struct ndp_subscription *ndp_subscription_by_id(struct ndp_subscriber *subscriber, void *id);

struct ndp_subscriber *ndp_subscriber_create(struct ndp *ndp);
void ndp_subscriber_destroy(struct ndp_subscriber *subscriber);

int ndp_subscriber_poll(struct ndp_subscriber *subscriber, struct file *filp, struct poll_table_struct *wait);

int ndp_channel_start(struct ndp_subscription *sub);
int ndp_channel_stop(struct ndp_subscription *sub, int force);
void ndp_channel_txsync(struct ndp_subscription *sub, struct ndp_subscription_sync *sync);
void ndp_channel_rxsync(struct ndp_subscription *sub, struct ndp_subscription_sync *sync);
void ndp_channel_sync(struct ndp_subscription *sub, struct ndp_subscription_sync *sync);

extern struct ndp_channel *ndp_channel_create(struct ndp *ndp, struct ndp_channel_ops *ctrl_ops,
		int node_offset, int index);
extern void ndp_remove_channel(struct ndp *sd,
		struct ndp_channel *channel);


struct ndp_subscription *ndp_subscription_create(
		struct ndp_subscriber *app,
		struct ndp_channel_request *channel_req);

void ndp_subscription_destroy(struct ndp_subscription *sub);

extern struct ndp_block *ndp_block_alloc(struct device *dev,
		unsigned int count, size_t size);
extern void ndp_block_free(struct device *dev, struct ndp_block *blks,
		unsigned int count);

int ndp_ring_mmap(struct vm_area_struct *vma, unsigned long offset, unsigned long size, void*priv);

static inline __attribute__((const)) size_t
ndp_tail_head_size(unsigned long head, unsigned long tail, size_t size)
{
	size_t ret;

	ret = size + head - tail;
	ret &= size - 1;

	return ret;
}

static inline __attribute__((const)) size_t
ndp_head_tail_size(unsigned long head, unsigned long tail, size_t size)
{
	size_t ret;

	if (head == tail)
		return size - 1;

	ret = size + tail - head;
	ret &= size - 1;

	/* disallow meeting of head and tail */
	if (ret)
		ret--;

	return ret;
}

static inline __attribute__((const)) bool ispow2(size_t val)
{
	return ((val - 1) & val) == 0;
}

static inline int ndp_kill_signal_pending(struct task_struct *p)
{
	int ret;

	if (!signal_pending(p))
		return 0;
	spin_lock_irq(&p->sighand->siglock);
	ret = sigismember(&p->pending.signal, SIGKILL) ||
		sigismember(&p->signal->shared_pending.signal, SIGKILL);
	spin_unlock_irq(&p->sighand->siglock);
	return ret;
}

#endif
