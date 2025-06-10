.. _nfb_dma:

nfb-dma
-------

This tools shows statistics and some detailed informations about DMA controllers, which represents transmission queues.

For the RX direction (C2H transfers) it is typically number of received and discarded packets and bytes.
For the TX direction (H2C transfers) it is typically only number of transmitted packets and bytes.

Verbose mode shows the extended info that includes SW/HW pointers and status information about queue.

.. code-block:: python

    {
        "rxq": [
            {
                "id": 0,
                "type": "CALYPTE",          # DMA controller type (CALYPTE, NDP, SZE)
                "pass": 0,
                "pass_bytes": 0,            # not present for type SZE
                "drop": 0,
                "drop_bytes": 0,            # not present for type SZE

                # items below present only in verbose mode
                "reg_control": 0,
                "run": False,               # controller is commanded to work
                "reg_status": 0,
                "running": False,           # controler is working

                # items below present only in verbose mode for type CALYPTE or NDP
                "sdp": 0,                   # software descriptor pointer
                "hdp": 0,                   # hardware descriptor pointer
                "mdp": 0,                   # mask for descriptor pointer
                "desc_buffer_size": 0,      # descriptor buffer size (in items)
                "desc_free": 0,             # free items in descriptor buffer
                "shp": 0,                   # software header pointer
                "hhp": 0,                   # hardware header pointer
                "mhp": 0,                   # mask for header pointer
                "hdr_buffer_size": 0,       # header buffer size (in items)
                "hdr_buffer_free": 0,       # free items in header buffer

                # items below present only in verbose mode for type NDP
                "timeout": 16384

                # items below present only in verbose mode for type SZE
                "pciep_mask": 0,
                "vfid": 0,
                "sw_ptr": 0,
                "hw_ptr": 0,
                "ptr_mask": 0,
                "buffer_size": 0,
                "timeout": 0,
                "max_request_size": 0
            }
        ],
        "txq": [
            {
                "id": 0,
                "type": "CALYPTE",
                "pass": 0,
                "pass_bytes": 0,            # not present for type SZE
                "drop": 0,                  # present only for type CALYPTE
                "drop_bytes": 0,            # present only for type CALYPTE

                # items below present only in verbose mode
                "reg_control": 0,
                "run": False,
                "reg_status": 0,
                "running": False,

                # items below present only in verbose mode for type CALYPTE or NDP
                "sdp": 0,
                "hdp": 0,
                "mdp": 8191,
                "desc_buffer_size": 8192,
                "desc_free": 0,

                # items below present only in verbose mode for type CALYPTE
                "shp": 0,
                "hhp": 0,
                "mhp": 1023,
                "hdr_buffer_size": 1024,
                "hdr_buffer_free": 1023,

                # items below present only in verbose mode for type SZE
                "pciep_mask": 0,
                "vfid": 0,
                "sw_ptr": 0,
                "hw_ptr": 0,
                "ptr_mask": 0,
                "buffer_size": 0,
                "timeout": 0,
                "max_request_size": 0
            }
        ]
    }
