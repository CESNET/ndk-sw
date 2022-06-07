/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Misc functions of the NFB platform
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include "nfb.h"

uint64_t reverse(const uint64_t n, const uint64_t k)
{
        uint64_t i, r = 0;
        for (i = 0; i < k; ++i)
                r |= ((n >> i) & 1) << (k - i - 1);
        return r;
}

int nfb_net_set_dev_addr(struct nfb_device *nfb, struct net_device *dev, int index)
{
	int snshift = 4;
	u8 addr[ETH_ALEN];

	if (dev->addr_len != ETH_ALEN)
		return -1;

	addr[0] = 0x00;
	addr[1] = 0x11;
	addr[2] = 0x17;

	addr[3] = reverse(nfb->nfb_pci_dev->card_type_id, 8);
	addr[4] = (nfb->serial << snshift) >> 8;
	addr[5] = ((nfb->serial << snshift) & 0xF0) | (index & 0x0F);
#ifdef CONFIG_HAVE_ETH_HW_ADDR_SET 
	eth_hw_addr_set(dev, addr);
#else
	memcpy(dev->dev_addr, addr, sizeof(addr));
#endif

	return 0;
}
