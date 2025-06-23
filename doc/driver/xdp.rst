XDP submodule
==============
The NFB driver supports XDP as an alternative to NDP API and DPDK. The XDP submodule is under development, and the NFB driver doesn't compile with it by default.

How to enable
=============
In order to compile with the XDP submodule, the variable  ``CONFIG_NFB_XDP = y`` has to be set in ``drivers/Makefile.conf``.

.. code-block:: Makefile
	:emphasize-lines: 6

	# drivers/Makefile.conf
	CONFIG_MODULES      = y
	CONFIG_NFB          = m
	CONFIG_NFB_XVC      = m
	# uncomment to enable XDP functionality
	CONFIG_NFB_XDP      = y

Next, the driver needs to be configure-d and make-d

.. code-block:: shell

	$ cd drivers; ./configure; make; cd ..

Then, you enable the driver by setting the module parametr ``xdp_enable=yes`` when inserting the module.

.. code-block:: shell

	$ sudo rmmod nfb
	$ sudo insmod drivers/kernel/drivers/nfb/nfb.ko xdp_enable=yes

In order to see if the driver successfully initialized, check the system log.
The ``nfb_xdp: Successfully attached`` message should be present. In case of errors, you can try rebooting the desing, by running ``nfb-boot -F<design slot number>``.

.. code-block:: shell
	:emphasize-lines: 9

	$ dmesg
	...
	[ 4933.960744] nfb 0000:03:00.0: FDT loaded, size: 16384, allocated buffer size: 65536
	[ 4933.961000] nfb 0000:03:00.0: unable to enable MSI
	[ 4933.961311] nfb 0000:03:00.0: nfb_mi: MI0 on PCI0 map: successfull
	[ 4933.964620] nfb 0000:03:00.0: nfb_boot: Attached successfully
	[ 4934.024800] nfb 0000:03:00.0: nfb_ndp: attached successfully
	[ 4934.024884] nfb 0000:03:00.0: nfb_qdr: Attached successfully (0 QDR controllers)
	[ 4934.028473] nfb 0000:03:00.0: nfb_xdp: Successfully attached
	[ 4934.028478] nfb 0000:03:00.0: successfully initialized
	[ 4934.028685] pcieport 0000:00:02.0: restoring errors on PCI bridge
	[ 4934.028688] pcieport 0000:00:02.0: firmware reload done
	...

Lastly, you add the add the XDP netdev with the help of ``nfb-dma`` tool.

To add a single XDP netdevice with all queues open

.. code-block:: shell

	$ sudo nfb-dma -N nfb_xdp add,id=1

How to test
===========



Basic XDP vs AF_XDP / XDP zero-copy mode overview
=================================================
Basic XDP
---------
When the XDP driver is inserted, and the XDP network interfaces are created via ``nfb-dma``, this starts up the queues on those network interfaces. 

In this mode, the driver acts as a usual network driver. When the XDP program is loaded onto the interface, the driver executes it on each received packet and handles the result accordingly.

The driver in this mode allocates its own memory for the packet buffers using Page Pool API.

**This mode only expands the basic network driver functionality by running the XDP program.**

AF_XDP / XDP zero-copy
----------------------
AF_XDP mode requires a specialized userspace app. This app allocates the memory block used for XDP buffers called UMEM and four XDP descriptor rings to go with it. 
The app then registers those objects with the system through system calls. The app opens the AF_XDP socket.

For an AF_XDP application to work, the XDP program has to be loaded into the kernel. This is usually done from the C code as a part of the app initialization process. The XDP program will work in tandem with the app in the userspace. 
The main purpose of this XDP program is to return an XDP_REDIRECT action onto the AF_XDP socket opened by the userspace application.
The reference to the socket is passed to the XDP program from the userspace by using an eBPF map of type BPF_MAP_TYPE_XSKMAP.

For a more complete explanation, please refer to the docs: https://docs.kernel.org/networking/af_xdp.html

- These steps are usually done indirectly by using some helper library like ``libxdp``.

The driver then has to switch the network channel (AF_XDP works with RX/TX queue pairs) chosen by the userspace application to AF_XDP mode. 
This is done on the fly, and it basically means stopping the DMA channel, mapping the memory from the userspace, and starting the DMA channel again.

In this mode, the driver works with memory allocated by userspace, and the userspace app has exclusive control over the network channel it works with.

When the AF_XDP socket is closed, the channel is switched back to the normal XDP mode.

**While running in this mode, the true zero-copy operation is achieved as long as the XDP program passes the packets to the userspace through AF_XDP socket. 
XDP actions other than XDP_REDIRECT to userspace will be handled by copying.**

XDP quickstart
==============
This section tells the bare minimum someone needs to know to compile and load an XDP program onto the interface.

XDP return actions
------------------
The XDP program returns an XDP action for each received packet. Those actions are:

- XDP_PASS - passes the packet to the network stack
	- Copy for XDP and AF_XDP 
- XDP_TX - transmits the packet from the same interface it was received on
	- zero-copy with XDP
	- Copy for AF_XDP (the correct way to do this is through redirect to the userspace app)
- XDP_DROP - drops the packet
	- Recycles the buffer for XDP and AF_XDP
- XDP_REDIRECT - redirects the packet
	- Onto network interface - copy
	- To a different CPU - for load balancing purposes
	- **To userspace - this is AF_XDP zero-copy operation**

XDP attach modes
----------------
XDP program can be attached in different ways, **this has performance implications**.

- Generic XDP (Generic mode, SKB mode) - The XDP program is run **after** driver passes the packet into the network stack. 
	This makes the XDP execution indifferent to driver support, but it means there will be little to no benefit to performance.
	- This mode should be used only for testing purposes

- Native XDP (Native mode) - The XDP program is run as a first thing on the receive path, and the driver handles the XDP actions. 
	- This is what should be used and is the main goal of this XDP kernel module

- Offloaded XDP - The XDP program is loaded and executed on the network card.
	- This is not supported by the NDK


Prerequisites
-------------
.. code-block:: shell
	
	$ sudo dnf install clang libbpf libbpf-devel libxdp libxdp-devel xdp-tools

``clang`` eBPF compiler is implemented as LLVM backend - clang is used on the front

``libbpf`` Library for writing eBPF (XDP is a subset of eBPF) programs in C; https://github.com/libbpf/libbpf

``libxdp`` Library that simplifies writing AF_XDP programs and contains helpers for attaching XDP programs to the interface; https://github.com/xdp-project/xdp-tools/blob/main/lib/libxdp/README.org

``xdp-tools`` Collection of tools for loading and testing XDP programs


The most simple XDP program
---------------------------
A program that drops all packets

.. code-block:: C

	#include <linux/bpf.h>
	#include <bpf/bpf_helpers.h>

	SEC("xdp_drop")
	int xdp_drop_prog(struct xdp_md *ctx)
	{
		return XDP_DROP;
	}

Compiling the XDP program
-------------------------
.. code-block:: shell
	
	$ clang -O2 -g -Wall -target bpf -c xdp_drop.c -o xdp_drop.o

Loading the XDP program
-----------------------
- Easiest way is to use ``xdp-loader`` program, which comes as a part of ``xdp-tools``. 
- ``libxdp`` contains helper functions for loading XDP programs from C code and interacting with eBPF maps.

Listing the interfaces and loaded programs:
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
.. code-block:: shell
	
	$ sudo xdp-loader status

Loading the program in native mode:
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
.. code-block:: shell
	
	$ sudo xdp-loader load IFNAME xdp_drop.o

Loading the program in generic mode:
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
.. code-block:: shell
	
	$ sudo xdp-loader load -m skb IFNAME xdp_drop.o

Unloading the program:
^^^^^^^^^^^^^^^^^^^^^^
.. code-block:: shell
	
	$ sudo xdp-loader unload IFNAME --all

AF_XDP quickstart
=================
This section explains the basics of writing AF_XDP program using ``libxdp`` in a form of short code snippets.

For an example how to work with the NDK look at the ``ndp-tool`` application located in the swbase repository: 

https://github.com/CESNET/ndk-sw/tree/main/tools/ndptool

The interensting files are ``xdp_read.c`` for RX - this is the simplest example of ``libxdp`` application. 
And ``xdp_common.c`` for the NDK FPGA ``sysfs`` API, that can be used to easily map the firmware queue indexes to netdev channel indexes.

The XDP program
---------------

The ``libxdp`` library ships default XDP program for AF_XDP operation, 
this program is loaded and the reference to the AF_XDP socket is placed to the ``BPF_MAP_TYPE_XSKMAP`` automatically, 
unless ``XSK_LIBBPF_FLAGS__INHIBIT_PROG_LOAD`` flag is set.
This simplifies the setup. If you would like to create your own XDP program for AF_XDP operation, 
for an example to preprocess packets before redirecting them to userspace, please follow the ``libxdp`` README.

You can check the default program here: https://github.com/xdp-project/xdp-tools/blob/main/lib/libxdp/xsk_def_xdp_prog.c

The userspace application
-------------------------

Loading the program, putting the socket reference to the ``BPF_MAP_TYPE_XSKMAP``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

This is taken care of by ``libxdp``, if you want to use custom XDP program, please follow the ``libxdp`` README.

Creating UMEM
^^^^^^^^^^^^^
The userspace has to allocate a large block of memory called UMEM. 

It is standard that one frame (here corresponding to one packet) is of the size PAGE_SIZE.

Function posix_memalign() assures that each frame is located on its own page.

It is possible to allocate DPDK style hugepages via ``mmap`` and flag ``MAP_HUGETLB``, 
but this is not used in the example and requires to reserve the hugepages beforehand.

The UMEM configuration can be skipped by passing ``NULL`` as the last argument of the ``xsk_umem__create()`` function, 
but the default ring size is 2048 descriptors, which can lead to performance issues, 
from testing it is good to have as much descriptors as there are UMEM frames.

.. code-block:: C

	// UMEM configuration
	struct umem_info *uinfo = &params->queue_data_arr[i].umem_info;
	uinfo->size = NUM_FRAMES * pagesize;
	uinfo->umem_cfg.comp_size = NUM_FRAMES;
	uinfo->umem_cfg.fill_size = NUM_FRAMES;
	uinfo->umem_cfg.flags = 0;
	uinfo->umem_cfg.frame_headroom = 0;
	uinfo->umem_cfg.frame_size = pagesize;
	// UMEM memory area
	if(posix_memalign(&uinfo->umem_area, pagesize, uinfo->size)) {
		fprintf(stderr, "Failed to get allocate umem buff for queue %d\n", params->queue_data_arr[i].eth_qid);
		...
	}
	// Registering UMEM
	if((ret = xsk_umem__create(&uinfo->umem, uinfo->umem_area, uinfo->size, &uinfo->fill_ring, &uinfo->comp_ring, &uinfo->umem_cfg))) {
		fprintf(stderr, "Failed to create umem for queue %d; ret: %d\n", params->queue_data_arr[i].eth_qid, ret);
		...
	}

Address stack
^^^^^^^^^^^^^

The userspace app needs to keep track of which frames are in its possession.
The address in this contex means **offset into the UMEM**.

In this example, keeping track of free frames is done through a stack of addresses.

The full code is located in ``xdp_common.h``.

.. code-block:: C

	struct addr_stack {
		uint64_t addresses[NUM_FRAMES];
		unsigned addr_cnt;
	};

	// Get address from stack
	static inline uint64_t alloc_addr(struct addr_stack *stack) {
		if (stack->addr_cnt == 0) {
			fprintf(stderr, "BUG out of adresses\n");
			exit(1);
		}
		uint64_t addr = stack->addresses[--stack->addr_cnt];
		stack->addresses[stack->addr_cnt] = 0;
		return addr;
	}

	// Put adress to stack
	static inline void free_addr(struct addr_stack *stack, uint64_t address) {
		if (stack->addr_cnt == NUM_FRAMES) {
			fprintf(stderr, "BUG counting adresses\n");
			exit(1);
		}
		stack->addresses[stack->addr_cnt++] = address; 
	}

	// Fill the stack with addresses into the umem
	static inline void init_addr(struct addr_stack *stack,	unsigned frame_size) {
		for(unsigned i = 0; i < NUM_FRAMES; i++) {
			stack->addresses[i] = i * frame_size;
		}
		stack->addr_cnt = NUM_FRAMES;
	}

NDK FPGA sysfs interface
^^^^^^^^^^^^^^^^^^^^^^^^

To use XDP driver you first need to add the XDP interface via ``nfb-dma`` tool. 
This creates an issue when writing applications, because the firmware DMA queues can be mapped to any number of XDP interfaces.
The AF_XDP socket needs to know the name of the interface and the index of the queue in the context of the interface.
For this purpose the XDP driver makes this information available via the ``sysfs`` interface. 
When the XDP driver is loaded the ``/sys/class/nfb/nfb#/nfb_xdp/`` directory is made available.
Inside this directory:

``channel_total`` - tells you how many XDP channels there are, this information is parsed from the device tree at driver startup.

``eth_count`` - tells you how many physical interfaces there are, this information is parsed from the device tree at driver startup.

``channel#`` - directories corespond to the XDP channels - These contain files corresponding to the state of the channel.

	``├── status`` - 1 if the queue was assigned to interface via ``nfb-dma`` else 0

	``├── ifname`` - Name of the interface the channel was mapped to via ``nfb-dma``, else 'NOT_OPEN'
	
	``└── index``  - Index in the context of the interface the channel was mapped to via ``nfb-dma``, else -1

Example: 

We have ``NDK-MINIMAL`` firmware with 16 DMA queue pairs. We add one XDP interface which uses firmware DMA queue pairs 5, 6.
The ``dmesg`` command says ``nfb 0000:21:00.0 enp33s0: renamed from nfb0x1`` because system predictible interface naming is enabled:

.. code-block:: shell

	$ sudo nfb-dma -N nfb_xdp add,id=1 -i5-6

Then the simplified structure looks like this:

.. code-block:: shell

	$ tree
	.
	├── channel0
	│   ├── ifname		NOT_OPEN
	│   ├── index		-1
	│   └── status		0
	...
	├── channel5
	│   ├── ifname		enp33s0
	│   ├── index		0
	│   └── status		1
	├── channel6
	│   ├── ifname		enp33s0
	│   ├── index		1
	│   └── status		1
	├── channel7
	│   ├── ifname		NOT_OPEN		
	│   ├── index		-1
	│   └── status		0
	...
	├── channel_total	16
	├── cmd
	└── eth_count 		2

By reading the files we can get the correct values for opening the AF_XDP socket.

The DMA queue pair 5 is mapped to enp33s0 as the first channel (index 0).

``xsk_socket__create(..., "enp33s0", 0, ..., ..., ..., ...)``

To delete the XDP interface:

.. code-block:: shell

	$ sudo nfb-dma -N nfb_xdp del,id=1

Creating socket
^^^^^^^^^^^^^^^

The ifname and queue_id in the context of the XDP interface can be acquired via sysfs.
The XSK configuration can be skipped by passing ``NULL`` as the last argument of the ``xsk_socket__create()`` function, 
but the default ring size is 2048 descriptors, which can lead to performance issues, 
from testing it is good to have as much descriptors as there are UMEM frames.

.. code-block:: C

	// XSK configuration
	struct xsk_info *xinfo = &params->queue_data_arr[i].xsk_info;
	xinfo->xsk_cfg.rx_size = NUM_FRAMES;
	xinfo->xsk_cfg.tx_size = NUM_FRAMES;
	xinfo->xsk_cfg.bind_flags = XDP_ZEROCOPY;
	// The name of the interface together with the queue_id (in context of the interface)
	// Can be best acquired via the sysfs interface of NDK platform
	strcpy(xinfo->ifname, params->queue_data_arr[i].ifname);
	xinfo->queue_id = params->queue_data_arr[i].eth_qid;
	if((ret = xsk_socket__create(&xinfo->xsk, xinfo->ifname, xinfo->queue_id, uinfo->umem, &xinfo->rx_ring, &xinfo->tx_ring, &xinfo->xsk_cfg))) {
		fprintf(stderr, "Failed to create xsocket for queue %d; ret: %d\n", params->queue_data_arr[i].eth_qid, ret);
		...
	}

The main loop
^^^^^^^^^^^^^
The fill part (from userspace to kernel):

``xsk_ring_prod__reserve()`` reserves slots on the fill ring

``xsk_ring_prod__fill_addr()`` is used to place the frames into the ring

``xsk_ring_prod__submit()`` signals, that the filling is done, and kernel now owns the frames

The recieve part (from kernel to userspace):

``xsk_ring_cons__peek()`` returns the number of packets ready to be processed and gives ownership to userspace

``xsk_ring_cons__rx_desc()`` holds the information about packet (its length and adress of data start)

``xsk_ring_cons__release()`` returns the RX ring slots back to the kernel so they can be filled with new packets

.. code-block:: C

	// The receive loop
	while (run) {
		...
		// Fill rx
		unsigned rx_idx = 0;
		unsigned fill_idx = 0;
		unsigned reservable = xsk_prod_nb_free(fill_ring, stack.addr_cnt);
		reservable = reservable < stack.addr_cnt ? reservable : stack.addr_cnt;
		if (reservable > burst_size) {
			unsigned reserved = xsk_ring_prod__reserve(fill_ring, reservable, &fill_idx);
			for(unsigned i = 0; i < reserved; i++) {
				*xsk_ring_prod__fill_addr(fill_ring, fill_idx++) = alloc_addr(&stack);
			}
			xsk_ring_prod__submit(fill_ring, reserved);
		}
		...
		// Receive packets
		cnt = xsk_ring_cons__peek(rx_ring, burst_size, &rx_idx);
		// Process packets
		for(unsigned i = 0; i < cnt; i++) {
			struct xdp_desc const *desc = xsk_ring_cons__rx_desc(rx_ring, rx_idx++);	
			packets[i].data = xsk_umem__get_data(xsk_data->umem_info.umem_area, desc->addr);
			packets[i].data_length = desc->len;
			free_addr(&stack, desc->addr);
		}
		// Mark done
		xsk_ring_cons__release(rx_ring, cnt);
	}

Common issues
^^^^^^^^^^^^^

No traffic:

Make sure the firmware interfaces are enabled: ``nfb-eth -e1``

Useful links
------------
Useful AF_XDP enabled apps
^^^^^^^^^^^^^^^^^^^^^^^^^^
- tcpreplay - since release v4.5.1; https://github.com/appneta/tcpreplay

Libxdp README
^^^^^^^^^^^^^
For ``libxdp`` API refer to: https://github.com/xdp-project/xdp-tools/tree/main/lib/libxdp