# pynfb

Python API for control of the CESNET NDK based FPGA acceleration cards.

This package provides Python bindings (via Cython) over `libnfb` and the
Netcope library, enabling register access, packet transmission (NDP queues),
and Ethernet MAC management on NFB/NDK FPGA cards.

## Prerequisites

`pynfb` is a source distribution — it compiles Cython extensions at install
time and links against the system-installed `libnfb` and `libfdt` shared
libraries.  The **nfb-framework** package (providing `libnfb`, the NFB kernel
driver, and the required headers) must be installed first.

> **Minimum version:** pynfb 0.3.0 requires nfb-framework >= 6.28.
> The pynfb version is independent from the nfb-framework version.

### Fedora / RHEL / Oracle Linux (RPM)

Enable the COPR repository and install nfb-framework:

```bash
sudo dnf copr enable @CESNET/nfb-framework
sudo dnf install nfb-framework
```

You also need the development files for building:

```bash
sudo dnf install libfdt-devel python3-Cython gcc
```

### Build from source

See the [NDK software build instructions](https://github.com/CESNET/ndk-sw#build-instructions)
for compiling `libnfb` and the kernel driver from source.

## Installation

Once the prerequisites are in place:

```bash
pip install nfb
```

## Quick start

```python
import nfb

dev = nfb.open()          # opens /dev/nfb0
comp = dev.comp_open("netcope,my_comp", 0)
comp.write32(0x100, 0xDEADBEEF)
val = comp.read32(0x100)
```

## Documentation

- [NFB Software User Guide](https://cesnet.github.io/ndk-sw/)
- [Python quick start](https://cesnet.github.io/ndk-sw/python/quick.html)
- [Python examples](https://cesnet.github.io/ndk-sw/python/examples.html)

## License

BSD 3-Clause License. See [LICENSE](LICENSE) for details.
