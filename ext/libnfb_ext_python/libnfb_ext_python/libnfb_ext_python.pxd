from posix.types cimport off_t

from libc.stdint cimport uint8_t, uint16_t, uint32_t, uint64_t
from libc.stdlib cimport malloc

cdef extern from "<string.h>":
    void * memcpy(void * destination, const void * source, size_t num);

cdef public api:
    ssize_t nfb_ext_python_bus_read(self, const char *bus_path, const char*comp_path, void *buf, size_t nbyte, off_t offset) except *
    ssize_t nfb_ext_python_bus_write(self, const char *bus_path, const char*comp_path, const void *buf, size_t nbyte, off_t offset) except *
    const void *nfb_ext_python_get_fdt(self) except *
