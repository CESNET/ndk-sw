from libc.stdint cimport uint8_t, uint16_t, uint32_t, uint64_t
from libc.string cimport memcpy

from posix.types cimport off_t


cdef extern from "cnfb.h":
    int c_fdt_totalsize(const void *fdt)

cdef extern from "<nfb/nfb.h>":
    cdef struct nfb_device:
        pass

    cdef struct nfb_comp:
        pass

    nfb_device* nfb_open(const char* path)
    void nfb_close(nfb_device* dev)

    nfb_comp* nfb_comp_open(nfb_device *dev, int fdt_offset)
    void nfb_comp_close(nfb_comp* comp)

    const void* nfb_get_fdt(nfb_device* dev)

    int nfb_comp_find(const nfb_device *dev, const char *compatible, unsigned index)
    ssize_t nfb_comp_write(const nfb_comp *comp, const void *buf, size_t nbyte, off_t offset)
    ssize_t nfb_comp_read(const nfb_comp *comp, void *buf, size_t nbyte, off_t offset)

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

cdef extern from "<libfdt.h>":
    int fdt_path_offset(const void *fdt, const char *path)


cdef class Nfb:
    cdef nfb_device* _dev
    cdef const void* _fdt
    cdef const char* _fdtc
    cdef public bytes _dtb

    cdef dict __dict__
