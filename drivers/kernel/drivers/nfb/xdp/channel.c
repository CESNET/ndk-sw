/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/*
 * XDP driver of the NFB platform - channel module
 *	channel corresponds to one RX/TX queue pair
 *
 * Copyright (C) 2017-2025 CESNET
 * Author(s):
 *   Richard Hyros <hyros@cesnet.cz>
 */

#include "ctrl_xdp.h"
#include "channel.h"
#include "ethdev.h"

static int nfb_xdp_rx_thread(void *rxqptr)
{
	struct nfb_ethdev *ethdev;
	struct net_device *netdev;
	struct nfb_xdp_queue *rxq;
	struct nfb_xdp_channel *channel;
	struct napi_struct *napi;
	int ret;

	rxq = rxqptr;
	channel = container_of(rxq, struct nfb_xdp_channel, rxq);
	ethdev = channel->ethdev;
	netdev = ethdev->netdev;
	napi = &rxq->napi;
	ret = 0;

	while (!kthread_should_stop()) {
		local_bh_disable();
		napi_schedule(napi);
		local_bh_enable();
		while (!kthread_should_stop() && test_bit(NAPI_STATE_SCHED, &napi->state))
			usleep_range(10, 20);
	}
	return ret;
}

static int nfb_xdp_tx_thread(void *txqptr)
{
	struct nfb_ethdev *ethdev;
	struct net_device *netdev;
	struct nfb_xdp_queue *txq;
	struct nfb_xdp_channel *channel;
	struct napi_struct *napi;
	int ret;

	txq = txqptr;
	channel = container_of(txq, struct nfb_xdp_channel, txq);
	ethdev = channel->ethdev;
	netdev = ethdev->netdev;
	napi = &txq->napi;
	ret = 0;

	if (!test_bit(NFB_STATUS_IS_XSK, &channel->status)) // In page pool mode tx exits
		return ret;

	while (!kthread_should_stop()) {
		local_bh_disable();
		napi_schedule(napi);
		local_bh_enable();
		while (!kthread_should_stop() && test_bit(NAPI_STATE_SCHED, &napi->state))
			usleep_range(10, 20);
	}
	return ret;
}

static int channel_create_threads(struct nfb_xdp_channel *channel)
{
	int ret = 0;
	struct net_device *netdev = channel->ethdev->netdev;

	// Create RX thread for each queue
	channel->rxq.thread = kthread_create_on_node(nfb_xdp_rx_thread, &channel->rxq, channel->numa, "%s/%u", netdev->name, channel->nfb_index);
	if (IS_ERR(channel->rxq.thread)) {
		printk(KERN_ERR "nfb: %s - failed to create rx thread (error: %ld, channel: %d)\n",
		       netdev->name, PTR_ERR(channel->rxq.thread), channel->nfb_index);
		ret = PTR_ERR(channel->rxq.thread);
		goto err_kthread_rx;
	}
	// Increment reference counter to kthread so the thread can exit on error and kthread_stop() won't crash
	// put_task_struct() must be called after kthread_stop()
	get_task_struct(channel->rxq.thread);
	// Enable napi
	napi_enable(&channel->rxq.napi);
	// Wake up thread
	wake_up_process(channel->rxq.thread);

	// Create TX thread for each queue
	channel->txq.thread = kthread_create_on_node(nfb_xdp_tx_thread, &channel->txq, channel->numa, "%s/%u", netdev->name, channel->nfb_index);
	if (IS_ERR(channel->txq.thread)) {
		printk(KERN_ERR "nfb: %s - failed to create tx thread (error: %ld, channel: %d)\n",
		       netdev->name, PTR_ERR(channel->txq.thread), channel->nfb_index);
		ret = PTR_ERR(channel->txq.thread);
		goto err_kthread_tx;
	}
	// increment reference counter to struct_task so the thread can exit on error and kthread_stop() won't crash
	// put_task_struct() must be called after kthread_stop() on driver detach
	get_task_struct(channel->txq.thread);
	// Enable napi
	if (!test_bit(NFB_STATUS_IS_XSK, &channel->status)) {
		// pagepool doesn't use tx napi
	} else {
		napi_enable(&channel->txq.napi);
	}
	netif_tx_start_queue(netdev_get_tx_queue(netdev, channel->index));
	// Wake up thread
	wake_up_process(channel->txq.thread);

	return 0;

err_kthread_tx:
	// collect rx thread
	if (channel->rxq.thread != NULL) {
		kthread_stop(channel->rxq.thread);
		// This invalidates the task_struct reference
		put_task_struct(channel->rxq.thread);
		channel->rxq.thread = NULL;
	}
	napi_disable(&channel->rxq.napi);
	while (napi_disable_pending(&channel->rxq.napi))
		;

err_kthread_rx:
	return ret;
}

int channel_start_pp(struct nfb_xdp_channel *channel)
{
	struct nfb_ethdev *ethdev = channel->ethdev;
	struct net_device *netdev = ethdev->netdev;
	struct nfb_xdp_queue *rxq = &channel->rxq;
	struct nfb_xdp_queue *txq = &channel->txq;
	int ret = 0;

	mutex_lock(&channel->state_mutex);
	{
		if (test_bit(NFB_STATUS_IS_RUNNING, &channel->status)) {
			ret = -EBUSY;
			goto err_channel_running;
		}

#ifdef CONFIG_HAVE_NETIF_NAPI_ADD_WITH_WEIGHT
		netif_napi_add(netdev, &rxq->napi, nfb_xctrl_napi_poll_pp, NAPI_POLL_WEIGHT);
#else
		netif_napi_add_weight(netdev, &rxq->napi, nfb_xctrl_napi_poll_pp, NAPI_POLL_WEIGHT);
#endif
		memset(&txq->napi, 0, sizeof(&txq->napi));

		if (!(rxq->ctrl = nfb_xctrl_alloc_pp(netdev, channel->index, NFB_XDP_DESC_CNT, NFB_XCTRL_RX))) {
			ret = -ENOMEM;
			printk(KERN_ERR "nfb: %s - failed to alloc rx queue %d (error: %d)\n", netdev->name, channel->nfb_index, ret);
			goto err_alloc_rx;
		}

		if (!(txq->ctrl = nfb_xctrl_alloc_pp(netdev, channel->index, NFB_XDP_DESC_CNT, NFB_XCTRL_TX))) {
			ret = -ENOMEM;
			printk(KERN_ERR "nfb: %s - failed to alloc tx queue %d (error: %d)\n", netdev->name, channel->nfb_index, ret);
			goto err_alloc_tx;
		}

		if ((ret = nfb_xctrl_start(rxq->ctrl))) {
			printk(KERN_ERR "nfb: %s - failed to start rx queue %d (error: %d)\n", netdev->name, channel->nfb_index, ret);
			goto err_start_rx;
		}

		if ((ret = nfb_xctrl_start(txq->ctrl))) {
			printk(KERN_ERR "nfb: %s - failed to start tx queue %d (error: %d)\n", netdev->name, channel->nfb_index, ret);
			goto err_start_tx;
		}

		clear_bit(NFB_STATUS_IS_XSK, &channel->status);
		if ((ret = channel_create_threads(channel))) {
			printk(KERN_ERR "nfb: %s - failed to create queue threads %d (error: %d)\n", netdev->name, channel->nfb_index, ret);
			goto err_threads;
		}
		set_bit(NFB_STATUS_IS_RUNNING, &channel->status);
	}
	mutex_unlock(&channel->state_mutex);
	return ret;

err_threads:
err_start_tx:
err_start_rx:
	nfb_xctrl_destroy_pp(txq->ctrl);
err_alloc_tx:
	nfb_xctrl_destroy_pp(rxq->ctrl);
err_alloc_rx:
	netif_napi_del(&rxq->napi);
	memset(&rxq->napi, 0, sizeof(rxq->napi));
err_channel_running:
	mutex_unlock(&channel->state_mutex);
	return ret;
}

int channel_start_xsk(struct nfb_xdp_channel *channel)
{
	struct nfb_ethdev *ethdev = channel->ethdev;
	struct net_device *netdev = ethdev->netdev;
	struct nfb_xdp_queue *rxq = &channel->rxq;
	struct nfb_xdp_queue *txq = &channel->txq;
	int ret = 0;

	mutex_lock(&channel->state_mutex);
	{
		if (test_bit(NFB_STATUS_IS_RUNNING, &channel->status)) {
			ret = -EBUSY;
			goto err_channel_running;
		}

#ifdef CONFIG_HAVE_NETIF_NAPI_ADD_WITH_WEIGHT
				netif_napi_add(netdev, &rxq->napi, nfb_xctrl_napi_poll_rx_xsk, NAPI_POLL_WEIGHT);
#else
				netif_napi_add_weight(netdev, &rxq->napi, nfb_xctrl_napi_poll_rx_xsk, NAPI_POLL_WEIGHT);
#endif
#ifdef CONFIG_HAVE_NETIF_NAPI_ADD_TX_WEIGHT
				netif_napi_add_tx_weight(netdev, &txq->napi, nfb_xctrl_napi_poll_tx_xsk, NAPI_POLL_WEIGHT);
#else
				netif_tx_napi_add(netdev, &txq->napi, nfb_xctrl_napi_poll_tx_xsk, NAPI_POLL_WEIGHT);
#endif

		if (!(rxq->ctrl = nfb_xctrl_alloc_xsk(netdev, channel->index, channel->pool, NFB_XCTRL_RX))) {
			ret = -ENOMEM;
			printk(KERN_ERR "nfb: %s - failed to alloc rx queue %d (error: %d)\n", netdev->name, channel->nfb_index, ret);
			goto err_alloc_rx;
		}

		if (!(txq->ctrl = nfb_xctrl_alloc_xsk(netdev, channel->index, channel->pool, NFB_XCTRL_TX))) {
			ret = -ENOMEM;
			printk(KERN_ERR "nfb: %s - failed to alloc tx queue %d (error: %d)\n", netdev->name, channel->nfb_index, ret);
			goto err_alloc_tx;
		}

		if ((ret = nfb_xctrl_start(rxq->ctrl))) {
			printk(KERN_ERR "nfb: %s - failed to start rx queue %d (error: %d)\n", netdev->name, channel->nfb_index, ret);
			goto err_start_rx;
		}

		if ((ret = nfb_xctrl_start(txq->ctrl))) {
			printk(KERN_ERR "nfb: %s - failed to start tx queue %d (error: %d)\n", netdev->name, channel->nfb_index, ret);
			goto err_start_tx;
		}

		set_bit(NFB_STATUS_IS_XSK, &channel->status);
		if ((ret = channel_create_threads(channel))) {
			goto err_threads;
		}
		set_bit(NFB_STATUS_IS_RUNNING, &channel->status);
	}
	mutex_unlock(&channel->state_mutex);

	return ret;

err_threads:
err_start_tx:
err_start_rx:
	nfb_xctrl_destroy_xsk(txq->ctrl);
err_alloc_tx:
	nfb_xctrl_destroy_xsk(rxq->ctrl);
err_alloc_rx:
	netif_napi_del(&rxq->napi);
	memset(&rxq->napi, 0, sizeof(rxq->napi));
	netif_napi_del(&txq->napi);
	memset(&txq->napi, 0, sizeof(txq->napi));
err_channel_running:
	mutex_unlock(&channel->state_mutex);
	return ret;
}

int channel_stop(struct nfb_xdp_channel *channel)
{
	struct nfb_xdp_queue *rxq = &channel->rxq;
	struct nfb_xdp_queue *txq = &channel->txq;
	struct net_device *netdev = channel->ethdev->netdev;
	int ret = 0;

	mutex_lock(&channel->state_mutex);
	{
		if (!test_bit(NFB_STATUS_IS_RUNNING, &channel->status)) {
			ret = -EINVAL;
			goto err_channel_not_running;
		}

		// Collect rx thread
		if (rxq->thread != NULL) {
			kthread_stop(rxq->thread);
			// This invalidates the task_struct reference
			put_task_struct(rxq->thread);
			rxq->thread = NULL;
		}
		napi_disable(&rxq->napi);
		while (napi_disable_pending(&rxq->napi))
			;
		netif_napi_del(&rxq->napi);

		// Collect tx thread
		netif_tx_stop_queue(netdev_get_tx_queue(netdev, channel->index));
		if (channel->txq.thread != NULL) {
			kthread_stop(channel->txq.thread);
			put_task_struct(channel->txq.thread);
			channel->txq.thread = NULL;
		}
		if (!test_bit(NFB_STATUS_IS_XSK, &channel->status)) {
			// Only xsk uses tx napi
		} else {
			napi_disable(&txq->napi);
			while (napi_disable_pending(&txq->napi))
				;
			netif_napi_del(&txq->napi);
		}

		if (!test_bit(NFB_STATUS_IS_XSK, &channel->status)) {
			nfb_xctrl_destroy_pp(rxq->ctrl);
			nfb_xctrl_destroy_pp(txq->ctrl);
		} else {
			nfb_xctrl_destroy_xsk(rxq->ctrl);
			nfb_xctrl_destroy_xsk(txq->ctrl);
		}
		clear_bit(NFB_STATUS_IS_RUNNING, &channel->status);
	}
	mutex_unlock(&channel->state_mutex);
	return ret;

err_channel_not_running:
	mutex_unlock(&channel->state_mutex);
	return ret;
}
