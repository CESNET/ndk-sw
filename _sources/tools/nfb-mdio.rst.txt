.. _nfb_mdio:

nfb-mdio
--------

The MDIO tool can be used, if the informations from the nfb-eth aren't sufficient.

It allows to read and write specific MDIO registers in the PCS/PMA/PMD.

Addressing scheme corresponds to conventions in the IEEE802.3 clause 45.
For example turn on the PMA local loopback is done by ``nfb-mdio 1.0 1``.

