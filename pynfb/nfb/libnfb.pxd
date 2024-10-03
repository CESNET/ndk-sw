from libc.stdint cimport uint8_t, uint16_t, uint32_t, uint64_t
from libc.string cimport memcpy

from posix.types cimport off_t


cdef extern from "<libfdt.h>":
    """
    static inline int c_fdt_totalsize(const void *fdt)
    {
        return fdt_totalsize(fdt);
    }
    """
    int c_fdt_totalsize(const void *fdt)
    int fdt_path_offset(const void *fdt, const char *path)

cdef extern from "<nfb/nfb.h>":
    cdef struct nfb_device:
        pass

    cdef struct nfb_comp:
        pass

    const char* nfb_default_dev_path()
    nfb_device* nfb_open(const char* path)
    void nfb_close(nfb_device* dev)

    nfb_comp* nfb_comp_open(nfb_device *dev, int fdt_offset)
    void nfb_comp_close(nfb_comp* comp)

    const void* nfb_get_fdt(nfb_device* dev)

    int nfb_comp_find(const nfb_device *dev, const char *compatible, unsigned index)
    ssize_t nfb_comp_write(const nfb_comp *comp, const void *buf, size_t nbyte, off_t offset)
    ssize_t nfb_comp_read(const nfb_comp *comp, void *buf, size_t nbyte, off_t offset)

    int nfb_comp_trylock(const nfb_comp *comp, uint32_t features, int timeout)
    int nfb_comp_lock(const nfb_comp *comp, uint32_t features)
    void nfb_comp_unlock(const nfb_comp *comp, uint32_t features)


cdef extern from "<nfb/ndp.h>":
    cdef struct nfb_device:
        pass

    cdef struct ndp_packet:
        unsigned char *data
        unsigned char *header
        unsigned data_length
        unsigned header_length
        uint16_t flags

    cdef struct ndp_queue:
        pass

    ndp_queue *ndp_open_tx_queue(nfb_device *nfb, unsigned queue_id)
    ndp_queue *ndp_open_rx_queue(nfb_device *nfb, unsigned queue_id)

    unsigned ndp_rx_burst_get(ndp_queue *queue, ndp_packet *packets, unsigned count)
    unsigned ndp_tx_burst_get(ndp_queue *queue, ndp_packet *packets, unsigned count)
    void ndp_rx_burst_put(ndp_queue *queue)
    void ndp_tx_burst_put(ndp_queue *queue)
    void ndp_tx_burst_flush(ndp_queue *queue)

    void ndp_close_rx_queue(ndp_queue *queue)
    void ndp_close_tx_queue(ndp_queue *queue)

    int ndp_queue_start(ndp_queue *queue)
    int ndp_queue_stop(ndp_queue *queue)

cdef extern from "<nfb/ext.h>":
    ctypedef ssize_t nfb_bus_read_func(void *p, void *buf, size_t nbyte, off_t offset)
    ctypedef ssize_t nfb_bus_write_func(void *p, const void *buf, size_t nbyte, off_t offset)

    ctypedef int libnfb_ext_ops_open(const char *devname, int oflag, void **priv, void **fdt)
    ctypedef void libnfb_ext_ops_close(void *priv)
    ctypedef int libnfb_ext_ops_bus_open_mi(void *dev_priv, int bus_node, int comp_node, void ** bus_priv, libnfb_bus_ext_ops* ops)
    ctypedef void libnfb_ext_ops_bus_close_mi(void *bus_priv)
    ctypedef int libnfb_ext_ops_comp_lock(const nfb_comp *comp, uint32_t features)
    ctypedef void libnfb_ext_ops_comp_unlock(const nfb_comp *comp, uint32_t features)

    ctypedef int libnfb_ext_ops_ndp_queue_open(nfb_device *dev, void *dev_priv, unsigned index, int dir, int flags, ndp_queue ** pq)
    ctypedef int libnfb_ext_ops_ndp_queue_close(ndp_queue *q)

    ctypedef unsigned ndp_rx_burst_get_t(void *priv, ndp_packet *packets, unsigned count)
    ctypedef int ndp_rx_burst_put_t(void *priv)

    ctypedef unsigned ndp_tx_burst_get_t(void *priv, ndp_packet *packets, unsigned count)
    ctypedef int ndp_tx_burst_put_t(void *priv)
    ctypedef int ndp_tx_burst_flush_t(void *priv)

    struct libnfb_ext_ops:
        libnfb_ext_ops_open open
        libnfb_ext_ops_close close
        libnfb_ext_ops_bus_open_mi bus_open_mi
        libnfb_ext_ops_bus_close_mi bus_close_mi
        libnfb_ext_ops_comp_lock comp_lock
        libnfb_ext_ops_comp_unlock comp_unlock

        libnfb_ext_ops_ndp_queue_open ndp_queue_open
        libnfb_ext_ops_ndp_queue_close ndp_queue_close

    cdef struct libnfb_bus_ext_ops:
        nfb_bus_read_func *read
        nfb_bus_write_func *write

    ctypedef struct ndp_queue_ops_burst_tx:
        ndp_tx_burst_get_t get
        ndp_tx_burst_put_t put
        ndp_tx_burst_flush_t flush
    ctypedef struct ndp_queue_ops_burst_rx:
        ndp_rx_burst_get_t get
        ndp_rx_burst_put_t put

    ctypedef union ndp_queue_ops_burst:
        ndp_queue_ops_burst_rx rx
        ndp_queue_ops_burst_tx tx

    ctypedef struct ndp_queue_ops_control:
        int (*start)(void *priv)
        int (*stop)(void *priv)

    cdef struct ndp_queue_ops:
        ndp_queue_ops_burst burst
        ndp_queue_ops_control control

    cdef ndp_queue * ndp_queue_create(nfb_device *dev, int numa, int type, int index)
    cdef void ndp_queue_destroy(ndp_queue* q)

    cdef void* ndp_queue_get_priv(ndp_queue *q)
    cdef void ndp_queue_set_priv(ndp_queue *q, void *priv)

    cdef ndp_queue_ops* ndp_queue_get_ops(ndp_queue *q)


cdef class NfbDeviceHandle:
    #cdef object __weakref__
    cdef nfb_device* _dev
    cdef list _close_cbs


cdef class Nfb:
    cdef NfbDeviceHandle _handle
    # Deprecated. Use _handle._dev
    cdef nfb_device* _dev
    cdef const void* _fdt

    cdef dict __dict__

cdef class AbstractBaseComp:
    cdef dict __dict__
