cdef api const char *libnfb_ext_compatible_pattern = "libnfb-ext-compat_pattern:"

import logging

_log = logging.getLogger(__name__)

# Optional hook implementing ExceptionBridgeBase (or pending/intercept duck type).
_exception_bridge = None


def set_exception_bridge(bridge):
    """Register/clear a bridge for BaseException crossing C callbacks.

    *bridge* should implement :class:`nfb.ext.python.ExceptionBridgeBase`
    (or the same ``pending`` / ``intercept`` interface).
    """
    global _exception_bridge
    _exception_bridge = bridge


def get_exception_bridge():
    return _exception_bridge


cdef bint _bridge_pending():
    return _exception_bridge is not None and <bint>bool(_exception_bridge.pending())


cdef bint _bridge_intercept(object exc):
    return _exception_bridge is not None and <bint>bool(_exception_bridge.intercept(exc))


def get_libnfb_ext_path(nfb):
    # early version of libnfb relies on a pattern "libnfb-ext-" somewhere in device string/path
    base = __file__ + ":" + libnfb_ext_compatible_pattern.decode()
    # new extension identification string must have a "libnfb-ext:" prefix (backward incompatible)
    #base = "libnfb-ext:" + __file__ + ":"
    return base + str(id(nfb))

cdef class __NfbWrapper:
    cdef char * fdt
    cdef object nfb
    cdef list bus

cdef int nfb_ext_python_open(const char *devname, int oflag, void **priv, void **fdt) noexcept with gil:
    cdef int ret
    cdef uint64_t addr
    cdef object nfb
    cdef bytes dtb
    cdef PyObject* _nfb
    cdef __NfbWrapper wrap

    addr = strtoull(devname + strlen(libnfb_ext_compatible_pattern), NULL, 10)
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

cdef void nfb_ext_python_close(void *priv) noexcept with gil:
    cdef PyObject* _wrap
    cdef __NfbWrapper wrap
    cdef object nfb
    cdef bytes dtb

    _wrap = <PyObject*> priv
    wrap = <object> _wrap

    # The libnfb is responsible for freeing fdt
    #free(wrap.fdt)
    Py_DECREF(wrap)

# Backend callbacks return to libnfb C, which does not check PyErr.
# Ordinary Python errors and unhandled BaseExceptions return a C error code.
# A registered exception bridge may intercept a BaseException to stash it
# for re-raise after the bridged call, and fail-fast while pending.

cdef ssize_t nfb_pynfb_bus_read(void *p, void *buf, size_t nbyte, off_t offset) noexcept with gil:
    cdef PyObject* _bus = <PyObject *> p
    cdef object bus = <object> _bus
    cdef bytes data
    cdef ssize_t ret

    if _bridge_pending():
        return -1

    try:
        nfb, comp_node, bus_node = bus
        data = nfb.read(bus_node, comp_node, offset, nbyte)
    except Exception:
        _log.exception("MI bus read callback failed")
        return -1
    except BaseException as e:
        _bridge_intercept(e)
        return -1

    ret = len(data)
    if ret > int(nbyte):
        ret = -1

    if ret > 0:
        memcpy(buf, <const char*>data, ret)

    return ret

cdef ssize_t nfb_pynfb_bus_write(void *p, const void *buf, size_t nbyte, off_t offset) noexcept with gil:
    cdef PyObject* _bus = <PyObject *> p
    cdef object bus = <object> _bus
    cdef const char* c_data = <const char*> buf
    cdef bytes data = c_data[:nbyte]

    if _bridge_pending():
        return -1

    try:
        nfb, comp_node, bus_node = bus
        nfb.write(bus_node, comp_node, offset, data)
    except Exception:
        _log.exception("MI bus write callback failed")
        return -1
    except BaseException as e:
        _bridge_intercept(e)
        return -1

    return nbyte

cdef int nfb_pynfb_bus_open(void *dev_priv, int bus_offset, int comp_offset, void ** bus_priv, libnfb_bus_ext_ops* ops) noexcept with gil:
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

cdef void nfb_pynfb_bus_close(void *priv) noexcept with gil:
    cdef PyObject* _bus = <PyObject *> priv
    cdef object bus = <object> _bus

    Py_DECREF(bus)

# NDP functions

cdef int pyndp_start(void *priv) noexcept with gil:
    cdef object t = <object>priv

    if _bridge_pending():
        return -1

    try:
        queue, temp = t
        queue.start()
    except Exception:
        _log.exception("NDP queue start callback failed")
        return -1
    except BaseException as e:
        _bridge_intercept(e)
        return -1

    return 0

cdef int pyndp_stop(void *priv) noexcept with gil:
    cdef object t = <object>priv

    if _bridge_pending():
        return -1

    try:
        queue, temp = t
        queue.stop()
    except Exception:
        _log.exception("NDP queue stop callback failed")
        return -1
    except BaseException as e:
        _bridge_intercept(e)
        return -1

    return 0

cdef unsigned pyndp_rx_burst_get(void *priv, ndp_packet *packets, unsigned count) noexcept with gil:
    cdef object t = <object>priv

    cdef uint8_t* c_data
    cdef uint8_t* c_hdr

    cdef object queue
    cdef object pkts
    cdef unsigned cnt
    cdef unsigned i

    if _bridge_pending():
        return 0

    try:
        queue, temp = t
        pkts = queue.burst_get(count)
    except Exception:
        _log.exception("NDP RX burst_get callback failed")
        return 0
    except BaseException as e:
        _bridge_intercept(e)
        return 0

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

cdef int pyndp_rx_burst_put(void *priv) noexcept with gil:
    cdef object t = <object>priv

    if _bridge_pending():
        return -1

    try:
        queue, temp = t
        queue.burst_put()
    except Exception:
        _log.exception("NDP RX burst_put callback failed")
        return -1
    except BaseException as e:
        _bridge_intercept(e)
        return -1

    return 0

cdef unsigned pyndp_tx_burst_get(void *priv, ndp_packet *packets, unsigned count) noexcept with gil:
    cdef object t = <object>priv
    cdef object queue
    cdef object pkts
    cdef object prep
    cdef unsigned i

    if _bridge_pending():
        return 0

    try:
        queue, temp = t
        pkts = []
        for i in range(count):
            pkts.append((
                int(packets[i].data_length),
                int(packets[i].header_length),
                int(packets[i].flags),
            ))
        prep = queue.burst_get(pkts)
    except Exception:
        _log.exception("NDP TX burst_get callback failed")
        return 0
    except BaseException as e:
        _bridge_intercept(e)
        return 0

    for i in range(len(prep)):
        packets[i].data = <unsigned char*>prep[i][0]
        packets[i].header = <unsigned char*>prep[i][1]

    return len(prep)

cdef int pyndp_tx_burst_put(void *priv) noexcept with gil:
    cdef object t = <object>priv

    if _bridge_pending():
        return -1

    try:
        queue, temp = t
        queue.burst_put()
    except Exception:
        _log.exception("NDP TX burst_put callback failed")
        return -1
    except BaseException as e:
        _bridge_intercept(e)
        return -1

    return 0

cdef int pyndp_tx_burst_flush(void *priv) noexcept with gil:
    cdef object t = <object>priv

    if _bridge_pending():
        return -1

    try:
        queue, temp = t
        queue.burst_flush()
    except Exception:
        _log.exception("NDP TX burst_flush callback failed")
        return -1
    except BaseException as e:
        _bridge_intercept(e)
        return -1

    return 0


cdef int nfb_pyndp_queue_open(nfb_device *dev, void *dev_priv, unsigned index, int dir, int flags, ndp_queue ** pq) noexcept with gil:
    cdef PyObject* _wrap = <PyObject*> dev_priv
    cdef __NfbWrapper wrap = <object> _wrap

    cdef int ret
    cdef ndp_queue *q
    cdef ndp_queue_ops* ops

    cdef object t

    try:
        queue = wrap.nfb.queue_open(index, dir, flags)
        assert queue is not None
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

cdef int nfb_pyndp_queue_close(ndp_queue *q) noexcept with gil:
    cdef object t = <object>ndp_queue_get_priv(q)

    queue, temp = t
    Py_DECREF(t)
    return 0

cdef int nfb_pynfb_comp_lock(const nfb_comp *comp, uint32_t features) noexcept with gil:
    # TODO
    return 1;

cdef void nfb_pynfb_comp_unlock(const nfb_comp *comp, uint32_t features) noexcept with gil:
    pass

cdef int pynfb_ext_get_ops(const char *devname, libnfb_ext_ops* ops) noexcept:
    ops.open = nfb_ext_python_open
    ops.close = nfb_ext_python_close
    ops.bus_open_mi = nfb_pynfb_bus_open
    ops.bus_close_mi = nfb_pynfb_bus_close
    ops.comp_lock = nfb_pynfb_comp_lock
    ops.comp_unlock = nfb_pynfb_comp_unlock
    ops.ndp_queue_open = nfb_pyndp_queue_open
    ops.ndp_queue_close = nfb_pyndp_queue_close
    return 1
