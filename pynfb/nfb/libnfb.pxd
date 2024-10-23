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
