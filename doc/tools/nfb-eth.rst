.. _nfb_eth:

nfb-eth
-------

Tool for configuring network interfaces (TX/RX MACs), displaying statistics and parameters of TX/RX lines.

It consists from three sections:

MAC section
~~~~~~~~~~~

As a default action, the packet statistics and link status are reported for both RX and TX MAC.
Specific direction (RX/TX) can be selected with the **-r** or **-t** argument.

MAC components must be enabled before use (**-e 1**), otherwise the network traffic doesn't pass through.
This is necessary especially for TX as the DMA/application core typically get stuck when the TX MAC doesn't accept the data stream (TX MAC doesn't drop them when disabled).

The tool can configure the maximum and minimum allowed packet length (**-L** and **-l** arguments)
and MAC address based filtration (**-M**: *add*, *remove*, *show*, *clear, *fill* commands operates on MAC address table, *normal* (unicast), *broadcast*, *multicast*, and *promiscuous* sets the required mode).

PMA section
~~~~~~~~~~~

The **-P** argument is intended to configure Ethernet modes.
If no command is entered, PCS/PMA status is printed, including PMA type, speed and link status.

Currently used PMA type can be configured with **-c** argument whereas the exact PMA identification string must be entered.
List of PMA types can vary with used firmware/hardware and can be obtained in verbose output (**-v** argument),
together with list of PMA features.

The PMA features that can be activated with **-c** argument as well.
However the feature can be activated and deactivated with *+* or *-* prefixes of its name.

For example:

- ``nfb-eth -Pv`` prints all supported PMA types/modes and features,
- ``nfb-eth -Pc 100GBASE-LR4`` sets the PMA to 100GBASE-LR4 type,
- ``nfb-eth -Pc "+PMA local loopback"`` enables local loopback feature on all PMAs.

Transceiver section
~~~~~~~~~~~~~~~~~~~

Prints transceiver information (if plugged) obtained from port management interface (MDIO or I2C),
including vendor, compliance, temperature, signal strength and more.
