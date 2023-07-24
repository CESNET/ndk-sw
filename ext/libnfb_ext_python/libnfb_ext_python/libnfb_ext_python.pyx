
cdef public api ssize_t nfb_ext_python_bus_read(self, const char *bus_path, const char *comp_path, void *buf, size_t nbyte, off_t offset):
    cdef bytes bbus_path = bus_path
    cdef bytes bcomp_path = comp_path
    cdef bytes data
    cdef char* c_data

    try:
        bus_node = self._nfb_ext_python_fdt.get_node(bbus_path.decode())
        comp_node = self._nfb_ext_python_fdt.get_node(bcomp_path.decode())

        data = self.read(bus_node, comp_node, offset, nbyte)
        assert len(data) == nbyte
        c_data = data
        memcpy(buf, c_data, nbyte)
    except Exception as e:
        print(e)
        return 0
    return nbyte

cdef public api ssize_t nfb_ext_python_bus_write(self, const char *bus_path, const char *comp_path, const void *buf, size_t nbyte, off_t offset):
    cdef bytes bbus_path = bus_path
    cdef bytes bcomp_path = comp_path
    cdef const char* c_data = <const char*> buf
    cdef bytes data

    data = c_data[:nbyte]
    try:
        bus_node = self._nfb_ext_python_fdt.get_node(bbus_path.decode())
        comp_node = self._nfb_ext_python_fdt.get_node(bcomp_path.decode())
        ret = self.write(bus_node, comp_node, offset, data)
    except:
        return 0
    return nbyte

cdef public api const void *nfb_ext_python_get_fdt(self):
    cdef bytes py_bytes = self._nfb_ext_python_dtb
    cdef char* c_string = py_bytes
    return c_string
