/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Misc functions of the NFB platform
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <linux/netdevice.h>
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

	if (dev->addr_len != ETH_ALEN)
		return -1;

	dev->dev_addr[0] = 0x00;
	dev->dev_addr[1] = 0x11;
	dev->dev_addr[2] = 0x17;

	dev->dev_addr[3] = reverse(nfb->nfb_pci_dev->card_type_id, 8);
	dev->dev_addr[4] = (nfb->serial << snshift) >> 8;
	dev->dev_addr[5] = ((nfb->serial << snshift) & 0xF0) | (index & 0x0F);

	return 0;
}
