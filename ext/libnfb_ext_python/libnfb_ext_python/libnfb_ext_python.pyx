cdef api const char *nfb_pynfb_prefix = "pynfb:"

cdef class __NfbWrapper:
    cdef char * fdt
    cdef object nfb
    cdef list bus

cdef int nfb_ext_python_open(const char *devname, int oflag, void **priv, void **fdt):
    cdef int ret
    cdef uint64_t addr
    cdef object nfb
    cdef bytes dtb
    cdef PyObject* _nfb
    cdef __NfbWrapper wrap

    addr = strtoull(devname + strlen(nfb_pynfb_prefix), NULL, 10)
    _nfb = <PyObject*> <void*> addr
    nfb = <object> _nfb

    wrap = __NfbWrapper()
    Py_INCREF(wrap)
    wrap.nfb = <object> _nfb

    dtb = nfb._nfb_ext_python_dtb
    ret = len(dtb)

    wrap.fdt = <char*> malloc(ret)
    if wrap.fdt == NULL:
        Py_DECREF(wrap)
        return -libc.errno.ENOMEM

    memcpy(wrap.fdt, <const char*>dtb, ret)

    fdt[0] = wrap.fdt
    priv[0] = <void*><PyObject*> wrap

    return 0

cdef void nfb_ext_python_close(void *priv):
    cdef PyObject* _wrap
    cdef __NfbWrapper wrap
    cdef object nfb
    cdef bytes dtb

    _wrap = <PyObject*> priv
    wrap = <object> _wrap

    free(wrap.fdt)
    Py_DECREF(wrap)

cdef ssize_t nfb_pynfb_bus_read(void *p, void *buf, size_t nbyte, off_t offset):
    cdef PyObject* _bus = <PyObject *> p
    cdef object bus = <object> _bus
    cdef bytes data

    nfb, comp_node, bus_node = bus
    data = nfb.read(bus_node, comp_node, offset, nbyte)
    assert len(data) == int(nbyte)
    memcpy(buf, <const char*>data, nbyte)

    return nbyte

cdef ssize_t nfb_pynfb_bus_write(void *p, const void *buf, size_t nbyte, off_t offset):
    cdef PyObject* _bus = <PyObject *> p
    cdef object bus = <object> _bus
    cdef const char* c_data = <const char*> buf
    cdef bytes data = c_data[:nbyte]

    nfb, comp_node, bus_node = bus
    nfb.write(bus_node, comp_node, offset, data)

    return nbyte

cdef int nfb_pynfb_bus_open(void *dev_priv, int bus_offset, int comp_offset, void ** bus_priv, libnfb_bus_ext_ops* ops):
    cdef PyObject* _wrap = <PyObject*> dev_priv
    cdef __NfbWrapper wrap = <object> _wrap

    cdef int ret
    cdef char * path
    cdef object bus
    cdef bytes bpath

    path = <char*> malloc(1024)
    if path == NULL:
        return -libc.errno.ENOMEM

    fdt = wrap.nfb._nfb_ext_python_fdt

    ret = fdt_get_path(wrap.fdt, comp_offset, path, 1024)
    comp_node = fdt.get_node((<bytes>path).decode())
    ret = fdt_get_path(wrap.fdt, bus_offset, path, 1024)
    bus_node = fdt.get_node((<bytes>path).decode())

    free(path)

    ops.read = <nfb_bus_read_func*> nfb_pynfb_bus_read
    ops.write = <nfb_bus_write_func*> nfb_pynfb_bus_write

    bus = (wrap.nfb, comp_node, bus_node)

    Py_INCREF(bus)
    bus_priv[0] = <PyObject*>bus

    return 0

cdef void nfb_pynfb_bus_close(void *priv):
    cdef PyObject* _bus = <PyObject *> priv
    cdef object bus = <object> _bus

    Py_DECREF(bus)

# NDP functions

cdef int pyndp_start(void *priv):
    cdef object t = <object>priv

    queue, temp = t
    queue.start()

    return 0

cdef int pyndp_stop(void *priv):
    cdef object t = <object>priv

    queue, temp = t
    queue.stop()

    return 0

cdef unsigned pyndp_rx_burst_get(void *priv, ndp_packet *packets, unsigned count):
    cdef object t = <object>priv

    cdef uint8_t* c_data
    cdef uint8_t* c_hdr

    queue, temp = t

    pkts = queue.burst_get(count)
    cnt = min(len(pkts), count)
    for i in range(cnt):
        pkt, hdr, flags = pkts[i]

        c_data = pkt
        c_hdr = hdr
        packets[i].data_length = len(pkt)
        packets[i].header_length = len(hdr)
        packets[i].data = c_data
        packets[i].header = c_hdr
        packets[i].flags = flags

    return cnt

cdef int pyndp_rx_burst_put(void *priv):
    cdef object t = <object>priv

    queue, temp = t
    queue.burst_put()

    return 0

cdef unsigned pyndp_tx_burst_get(void *priv, ndp_packet *packets, unsigned count):
    cdef object t = <object>priv
    queue, temp = t

    pkts = []
    for i in range(count):
        pkts.append((
            int(packets[i].data_length),
            int(packets[i].header_length),
            int(packets[i].flags),
        ))
    prep = queue.burst_get(pkts)

    for i in range(len(prep)):
        packets[i].data = <unsigned char*>prep[i][0]
        packets[i].header = <unsigned char*>prep[i][1]

    return len(prep)

cdef int pyndp_tx_burst_put(void *priv):
    cdef object t = <object>priv

    queue, temp = t
    queue.burst_put()

    return 0

cdef int pyndp_tx_burst_flush(void *priv):
    cdef object t = <object>priv

    queue, temp = t
    queue.burst_flush()
    return 0


cdef int nfb_pyndp_queue_open(nfb_device *dev, void *dev_priv, unsigned index, int dir, int flags, ndp_queue ** pq):
    cdef PyObject* _wrap = <PyObject*> dev_priv
    cdef __NfbWrapper wrap = <object> _wrap

    cdef int ret
    cdef ndp_queue *q
    cdef ndp_queue_ops* ops

    cdef object t

    try:
        _ndp = wrap.nfb.ndp
        _d = _ndp.rx if dir == 0 else _ndp.tx
        queue = _d[index]
    except:
        return -libc.errno.ENODEV

    q = ndp_queue_create(dev, -1, dir, index)
    if q == NULL:
        return -libc.errno.ENOMEM

    t = (queue, [])
    Py_INCREF(t)

    ndp_queue_set_priv(q, <PyObject*>t);

    ops = ndp_queue_get_ops(q)
    ops.control.start = pyndp_start
    ops.control.stop = pyndp_stop
    if dir == 0:
        ops.burst.rx.get = pyndp_rx_burst_get
        ops.burst.rx.put = pyndp_rx_burst_put
    else:
        ops.burst.tx.get = pyndp_tx_burst_get
        ops.burst.tx.put = pyndp_tx_burst_put
        ops.burst.tx.flush = pyndp_tx_burst_flush

    pq[0] = q

    return 0

cdef int nfb_pyndp_queue_close(ndp_queue *q):
    cdef object t = <object>ndp_queue_get_priv(q)

    queue, temp = t
    Py_DECREF(t)

cdef int nfb_pynfb_comp_lock(const nfb_comp *comp, uint32_t features):
    # TODO
    return 1;

cdef void nfb_pynfb_comp_unlock(const nfb_comp *comp, uint32_t features):
    pass

cdef int pynfb_ext_get_ops(const char *devname, libnfb_ext_ops* ops):
    if strncmp(devname, nfb_pynfb_prefix, strlen(nfb_pynfb_prefix)) != 0:
        return 0

    ops.open = nfb_ext_python_open
    ops.close = nfb_ext_python_close
    ops.bus_open_mi = nfb_pynfb_bus_open
    ops.bus_close_mi = nfb_pynfb_bus_close
    ops.comp_lock = nfb_pynfb_comp_lock
    ops.comp_unlock = nfb_pynfb_comp_unlock
    ops.ndp_queue_open = nfb_pyndp_queue_open
    ops.ndp_queue_close = nfb_pyndp_queue_close
    return 1
