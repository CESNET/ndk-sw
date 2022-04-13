Common info
==================

Userspace tools provide basic configuration access to firmware and its component as well simple data transmissions.

The common arguments and principles across the tools are unified as much as possible:

- **-d** (device) is used to specify the NFB device which the tool will use.
  Device can be entered as:

  - full path: ``/dev/nfb0`` [default]
  - or its shortcut: ``0``
  - persistent path on system: ``/dev/nfb/by-pci-slot/0000:03:00.0``
  - persistent path on universe: ``/dev/nfb/by-serial-no/COMBO-400G1/15432``

- **-i** (index) is used to specify the component index.
  Index can be entered as list or range combination, e.g. ``1,3-5,8``.
  When unspecified, tool can use only the first or all matching components in firmware, this depends on the meaning of tool.

- **-q** (query) is intended for use in scripts for obtainig specific information, e.g. ``PCI_SLOT=$(nfb-info -q pci)``.
  It is possible to use more queries at once separated by comma, each queried item will be printed on a separate line.
  Query mode overrides default action of the tool and also applies the **-i** parameter.

- **-v** (verbose) is used for more detailed output. Some tools supports more levels of verbosity when used more times.

- **-h** (help) prints basic usage of the tool.
