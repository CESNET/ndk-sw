#include <stdio.h>
#include <nfb/nfb.h>
#include <nfb/ndp.h>

#define NDP_PACKET_COUNT 16

int main(int argc, char *argv[])
{
    int i, ret, bursts;
    struct nfb_device *dev;
    struct ndp_queue *rxq, *txq;
    struct ndp_packet pkts[NDP_PACKET_COUNT];

    /* Get handle to NFB device for futher operation */
    if ((dev = nfb_open("0")) == NULL)
        errx(1, "Can't open device file");

    /* Open one RX and one TX NDP queue for data transmit */
    rxq = ndp_open_rx_queue(dev, 0);
    txq = ndp_open_tx_queue(dev, 0);

    if (rxq == NULL || txq == NULL)
        errx(1, "Can't open queue");

    /* Start transmission on both queues */ 
    ndp_queue_start(rxq);
    ndp_queue_start(txq);

    for (i = 0; i < NDP_PACKET_COUNT; i++) {
        /* Request space for some packets */
        pkts[i].data_length = 64 + i;
        pkts[i].header_length = 0;
    }

    /* Request placeholders for packets with specified length */
    ret = ndp_tx_burst_get(txq, pkts, NDP_PACKET_COUNT);
    if (ret != NDP_PACKET_COUNT)
        warnx("Requested %d packet placeholders to send, got %d", NDP_PACKET_COUNT, ret);

    for (i = 0; i < ret; i++) {
        /* Fill data space with some values */
        memset(pkts[i].data, 0, pkts[i].data_length);
        /* Pretend IPv4 */
        pkts[i].data[13] = 0x08;
    }
    /* Optional PUT (rather just for symmetricity)
       Beware the PUT operation may not send immediately,
       it can wait for more packets to be PUTed for best throughput */
    // ndp_tx_burst_put(txq);

    /* Force send immediately (implies PUT) */
    ndp_tx_burst_flush(txq);

    /* Let try to receive some packets */
    for (bursts = 0; bursts < 32; bursts++) {
        /* Let the library fill at most NDP_PACKET_COUNT, but it may be less */
        ret = ndp_rx_burst_get(rxq, pkts, NDP_PACKET_COUNT);
        if (ret == 0) {
            usleep(10000);
            continue;
        }

        for (i = 0; i < ret; i++) {
            /* If the metadata is present, it typically holds packet timestamp at offset 0 */
            if (pkts[i].header_length >= 8)
                printf("Timestamp: %lld\n", *((uint64_t*) (pkts[i].header + 0)));
        }
        /* We must ensure the return of the processed packets 
           (although this doesn't have to be done for every GET) */
        if ((bursts % 5) == 4)
            ndp_rx_burst_put(rxq);
    }

    ndp_rx_burst_put(rxq);

    /* Cleanup */
    ndp_close_tx_queue(txq);
    ndp_close_rx_queue(rxq);
    nfb_close(dev);
    return 0;
}
