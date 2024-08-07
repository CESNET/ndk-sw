from posix.types cimport off_t

from libc.stdint cimport uint8_t, uint16_t, uint32_t, uint64_t
from libc.stdlib cimport malloc, strtoull, free
from libc.string cimport strlen, memcpy, strncmp

from cpython.ref cimport PyObject, Py_INCREF, Py_DECREF

from libc.stdio cimport printf
cimport libc.errno

from nfb.libnfb cimport *

cdef extern from "<libfdt.h>":
    int fdt_get_path(const void *fdt, int nodeoffset, char *buf, int buflen)


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
