Base API
========

The libnfb is userspace library, which allows to do the basic operations over NFB cards.

Init and deinit functions
-------------------------

.. doxygenfunction:: nfb_open
   :project: libnfb

.. doxygenfunction:: nfb_close

Device tree functions
-------------------------

.. doxygenfunction:: nfb_get_fdt

.. doxygenfunction:: nfb_comp_count

.. doxygenfunction:: nfb_comp_find

.. doxygenfunction:: nfb_comp_find_in_parent


Component functions
-------------------------

.. doxygenfunction:: nfb_comp_open

.. doxygenfunction:: nfb_comp_close

.. doxygenfunction:: nfb_comp_read

.. doxygenfunction:: nfb_comp_write


.. c:function:: uint32_t nfb_comp_read32(struct nfb_comp *comp, off_t offset)

   Read a 32 bit value from component.

   :param comp: component
   :param offset: offset inside component to read from
   :rtype: Readen 32 bit value

.. warning::

   It will not be able to find out, if the operation fails (e.g. when the offset is outside address space). For such cases use :c:func:`nfb_comp_read`

.. note::

   There are similar nfb_comp_readN functions for 8, 16 and 64 bit read access.

.. c:function:: void nfb_comp_write32(struct nfb_comp *comp, off_t offset, uint32_t val)

   Write a 32 bit value to component.

   :param comp: component
   :param offset: offset inside component to read from
   :param val: 32 bit value to write

.. warning::

   It will not be able to find out, if the operation fails (e.g. when the offset is outside address space). For such cases use :c:func:`nfb_comp_write`

.. note::

   There are similar nfb_comp_writeN functions for 8, 16 and 64 bit write access.

.. doxygenfunction:: nfb_comp_lock

.. doxygenfunction:: nfb_comp_unlock
