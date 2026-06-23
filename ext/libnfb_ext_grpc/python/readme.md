# libnfb_ext_grpc

Python wrapper for the libnfb gRPC extension.

This package provides a gRPC server that exposes NFB device operations
(register access, FDT retrieval) over the network, allowing remote control
of NFB/NDK FPGA cards.

## Prerequisites

The **nfb-framework** package (providing `libnfb`, the NFB kernel driver,
and the required headers) must be installed first. See the
[NDK software documentation](https://cesnet.github.io/ndk-sw/) for details.

## Installation

```bash
pip install libnfb_ext_grpc
```

## Usage

Start the gRPC server:

```bash
nfb-python-grpc-server
```

## License

BSD 3-Clause License. See [LICENSE](LICENSE) for details.
