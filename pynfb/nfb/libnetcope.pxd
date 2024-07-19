from libc.stdint cimport uint8_t, uint16_t, uint32_t, uint64_t
from libcpp cimport bool

from posix.types cimport off_t

from libnfb cimport nfb_device, nfb_comp

cdef extern from *:
    """
    typedef uint64_t dma_addr_t;
    """
    ctypedef uint64_t dma_addr_t

cdef extern from "<netcope/rxmac.h>":
    cdef enum nc_mac_speed:
        MAC_SPEED_UNKNOWN = 0x0
        MAC_SPEED_10G = 0x3
        MAC_SPEED_40G = 0x4
        MAC_SPEED_100G = 0x5

    cdef struct nc_rxmac:
        pass

    cdef enum nc_rxmac_frame_length_limit:
        RXMAC_FRAME_LENGTH_MIN = 0x0
        RXMAC_FRAME_LENGTH_MAX = 0x1

    cdef enum nc_rxmac_mac_filter:
        RXMAC_MAC_FILTER_PROMISCUOUS = 0x0
        RXMAC_MAC_FILTER_TABLE = 0x1
        RXMAC_MAC_FILTER_TABLE_BCAST = 0x2
        RXMAC_MAC_FILTER_TABLE_BCAST_MCAST = 0x3

    cdef struct nc_rxmac_counters:
        unsigned long long cnt_total
        unsigned long long cnt_octets
        unsigned long long cnt_received
        unsigned long long cnt_erroneous
        unsigned long long cnt_overflowed

    cdef struct nc_rxmac_etherstats:
        unsigned long long octets
        unsigned long long pkts
        unsigned long long broadcastPkts
        unsigned long long multicastPkts
        unsigned long long CRCAlignErrors
        unsigned long long undersizePkts
        unsigned long long oversizePkts
        unsigned long long fragments
        unsigned long long jabbers
        unsigned long long pkts64Octets
        unsigned long long pkts65to127Octets
        unsigned long long pkts128to255Octets
        unsigned long long pkts256to511Octets
        unsigned long long pkts512to1023Octets
        unsigned long long pkts1024to1518Octets

    cdef struct nc_rxmac_status:
        unsigned enabled
        unsigned link_up
        unsigned overflow
        nc_rxmac_mac_filter mac_filter
        unsigned mac_addr_count
        unsigned error_mask
        unsigned frame_length_min
        unsigned frame_length_max
        unsigned frame_length_max_capable
        nc_mac_speed speed

    nc_rxmac *nc_rxmac_open(nfb_device *dev, int fdt_offset);
    nc_rxmac *nc_rxmac_open_index(nfb_device *dev, unsigned index);
    void             nc_rxmac_close(nc_rxmac *mac);
    void             nc_rxmac_enable(nc_rxmac *mac);
    void             nc_rxmac_disable(nc_rxmac *mac);
    int              nc_rxmac_get_link(nc_rxmac *mac);
    int              nc_rxmac_read_status(nc_rxmac *mac, nc_rxmac_status *status);
    int              nc_rxmac_read_counters(nc_rxmac *mac, nc_rxmac_counters *counters, nc_rxmac_etherstats *stats);
    int              nc_rxmac_reset_counters(nc_rxmac *mac);
    void             nc_rxmac_mac_filter_enable(nc_rxmac *mac, nc_rxmac_mac_filter mode);
    void             nc_rxmac_set_error_mask(nc_rxmac *mac, unsigned error_mask);
    int nc_rxmac_counters_initialize(nc_rxmac_counters *c, nc_rxmac_etherstats *s)

cdef extern from "<netcope/txmac.h>":
    cdef struct nc_txmac:
        pass

    cdef struct nc_txmac_counters:
        unsigned long long cnt_total
        unsigned long long cnt_octets
        unsigned long long cnt_sent
        unsigned long long cnt_erroneous

    cdef struct nc_txmac_status:
        unsigned enabled

    nc_txmac *nc_txmac_open(nfb_device *dev, int fdt_offset);
    nc_txmac *nc_txmac_open_index(nfb_device *dev, unsigned index);
    void      nc_txmac_close(nc_txmac *mac);
    void      nc_txmac_enable(nc_txmac *mac);
    void      nc_txmac_disable(nc_txmac *mac);
    int       nc_txmac_read_status(nc_txmac *mac, nc_txmac_status *s);
    int       nc_txmac_read_counters(nc_txmac *mac, nc_txmac_counters *counters);
    int       nc_txmac_reset_counters(nc_txmac *mac);

cdef extern from "<netcope/mdio.h>":
    cdef struct nc_mdio:
        pass

    nc_mdio     *nc_mdio_open(const nfb_device *dev, int fdt_offset, int fdt_offset_ctrlparam);
    nc_mdio     *nc_mdio_open_no_init(const nfb_device *dev, int fdt_offset, int fdt_offset_ctrlparam);
    void         nc_mdio_init(nc_mdio *mdio);
    void         nc_mdio_close(nc_mdio *mdio);
    int          nc_mdio_read (nc_mdio *mdio, int prtad, int devad, uint16_t addr);
    int          nc_mdio_write(nc_mdio *mdio, int prtad, int devad, uint16_t addr, uint16_t val);

cdef extern from "<netcope/eth.h>":
    int nc_eth_get_pcspma_control_node(const void *fdt, int nodeoffset, int *node_control_param)

cdef extern from "<netcope/i2c_ctrl.h>":
    cdef struct nc_i2c_ctrl:
        pass

    nc_i2c_ctrl *nc_i2c_open(const nfb_device *dev, int fdt_offset);
    void nc_i2c_set_addr(nc_i2c_ctrl *ctrl, uint8_t address);
    int nc_i2c_read_reg(nc_i2c_ctrl *ctrl, uint8_t reg, uint8_t *data, unsigned size);
    int nc_i2c_write_reg(nc_i2c_ctrl *ctrl, uint8_t reg, const uint8_t *data, unsigned size);
    void nc_i2c_close(nc_i2c_ctrl *ctrl);

cdef extern from "<netcope/rxqueue.h>":
    cdef struct nc_rxqueue:
        pass

    cdef struct nc_rxqueue_counters:
        unsigned long long received
        unsigned long long discarded
        unsigned long long received_bytes
        unsigned long long discarded_bytes
        unsigned have_bytes

    nc_rxqueue *nc_rxqueue_open(nfb_device *dev, int fdt_offset);
    void nc_rxqueue_close(nc_rxqueue *rxqueue);
    void nc_rxqueue_reset_counters(nc_rxqueue *rxqueue);
    void nc_rxqueue_read_counters(nc_rxqueue *rxqueue, nc_rxqueue_counters *counters);

cdef extern from "<netcope/txqueue.h>":
    cdef struct nc_txqueue:
        pass

    cdef struct nc_txqueue_counters:
        unsigned long long sent
        unsigned long long sent_bytes
        unsigned have_bytes

    nc_txqueue *nc_txqueue_open(nfb_device *dev, int fdt_offset);
    void nc_txqueue_close(nc_txqueue *txqueue);
    void nc_txqueue_reset_counters(nc_txqueue *txqueue);
    void nc_txqueue_read_counters(nc_txqueue *txqueue, nc_txqueue_counters *counters);

cdef extern from "<netcope/dma_ctrl_ndp.h>":
    cdef struct nc_ndp_desc:
        pass

    cdef struct nc_ndp_hdr:
        uint16_t frame_len
        uint8_t hdr_len
        unsigned meta
        unsigned reserved
        unsigned free_desc

    cdef struct nc_ndp_ctrl_start_params:
        dma_addr_t desc_buffer
        dma_addr_t data_buffer;
        dma_addr_t hdr_buffer
        dma_addr_t update_buffer
        uint32_t *update_buffer_virt
        uint32_t nb_data;
        uint32_t nb_desc
        uint32_t nb_hdr

    cdef struct nc_ndp_ctrl:
        uint64_t last_upper_addr
        uint32_t mdp
        uint32_t mhp
        uint32_t sdp
        uint32_t hdp
        uint32_t shp
        uint32_t hhp

        nfb_comp *comp
        uint32_t *update_buffer
        uint32_t dir

    nc_ndp_desc nc_ndp_rx_desc0(dma_addr_t phys)
    nc_ndp_desc nc_ndp_rx_desc2(dma_addr_t phys, uint16_t len, int next)
    nc_ndp_desc nc_ndp_tx_desc0(dma_addr_t phys)
    nc_ndp_desc nc_ndp_tx_desc2(dma_addr_t phys, uint16_t len, uint16_t meta, int next)

    void nc_ndp_ctrl_hdp_update(nc_ndp_ctrl *ctrl)
    void nc_ndp_ctrl_hhp_update(nc_ndp_ctrl *ctrl)
    void nc_ndp_ctrl_sp_flush(nc_ndp_ctrl *ctrl)
    void nc_ndp_ctrl_sdp_flush(nc_ndp_ctrl *ctrl)
    int nc_ndp_ctrl_open(nfb_device* nfb, int fdt_offset, nc_ndp_ctrl *ctrl)
    int nc_ndp_ctrl_start(nc_ndp_ctrl *ctrl, nc_ndp_ctrl_start_params *sp)
    int nc_ndp_ctrl_stop_force(nc_ndp_ctrl *ctrl)
    int nc_ndp_ctrl_stop(nc_ndp_ctrl *ctrl)
    void nc_ndp_ctrl_close(nc_ndp_ctrl *ctrl)
    int nc_ndp_ctrl_get_mtu(nc_ndp_ctrl *ctrl, unsigned int *min, unsigned int *max)
