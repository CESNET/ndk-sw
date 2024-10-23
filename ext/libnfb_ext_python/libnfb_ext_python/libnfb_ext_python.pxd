from posix.types cimport off_t

from libc.stdint cimport uint8_t, uint16_t, uint32_t, uint64_t
from libc.stdlib cimport malloc, strtoull, free
from libc.string cimport strlen, memcpy, strncmp

from cpython.ref cimport PyObject, Py_INCREF, Py_DECREF

from libc.stdio cimport printf
cimport libc.errno


cdef extern from "<libfdt.h>":
    int fdt_get_path(const void *fdt, int nodeoffset, char *buf, int buflen)

cdef extern from "<nfb/ndp.h>":
    cdef struct nfb_device:
        pass

    cdef struct nfb_comp:
        pass

    cdef struct ndp_packet:
        unsigned char *data
        unsigned char *header
        unsigned data_length
        unsigned header_length
        uint16_t flags

    cdef struct ndp_queue:
        pass

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


cdef api const char *nfb_pynfb_prefix

cdef public api:
    int pynfb_ext_get_ops(const char *devname, libnfb_ext_ops* ops) noexcept

    int nfb_ext_python_open(const char *devname, int oflag, void **priv, void **fdt) noexcept
    void nfb_ext_python_close(void *priv) noexcept

    int nfb_pynfb_bus_open(void *dev_priv, int bus_node, int comp_node, void ** bus_priv, libnfb_bus_ext_ops* ops) noexcept
    void nfb_pynfb_bus_close(void *bus_priv) noexcept

    ssize_t nfb_pynfb_bus_read(void *bus_priv, void *buf, size_t nbyte, off_t offset) noexcept
    ssize_t nfb_pynfb_bus_write(void *bus_priv, const void *buf, size_t nbyte, off_t offset) noexcept

    cdef int nfb_pyndp_queue_open(nfb_device *dev, void *dev_priv, unsigned index, int dir, int flags, ndp_queue ** pq) noexcept
    cdef int nfb_pyndp_queue_close(ndp_queue *q) noexcept

    cdef int pyndp_start(void *priv)
    cdef int pyndp_stop(void *priv)

    cdef unsigned pyndp_rx_burst_get(void *priv, ndp_packet *packets, unsigned count)
    cdef int pyndp_rx_burst_put(void *priv)

    cdef unsigned pyndp_tx_burst_get(void *priv, ndp_packet *packets, unsigned count)
    cdef int pyndp_tx_burst_put(void *priv)
    cdef int pyndp_tx_burst_flush(void *priv)
