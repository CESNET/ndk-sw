# The NDK software framework

This repository contains software framework for the Network Development Kit (NDK). The NDK allows users to quickly and easily develop new network applications based on FPGA acceleration cards.
You can build the FPGA firmware for this card using the [NDK-APP-Minimal application](https://github.com/CESNET/ndk-app-minimal/). The NDK-APP-Minimal is a reference FPGA application based on the NDK.
The NDK software framework is used to manage and control FPGA cards with NDK firmware. It includes:
- Linux kernel driver
- libnfb userspace library
- nfb & ndp tools for basic usage

## Documentation

The [**NDK software documentation**](https://cesnet.github.io/ndk-sw) is automatically generated documentation based on the [Sphinx](https://www.sphinx-doc.org) (public GitHub Pages - built from main branch).

## Prebuilt RPM packages

The prebuilt RPM packages can be obtained via [Copr](https://copr.fedorainfracloud.org/coprs/g/CESNET/nfb-framework/):

`sudo dnf copr enable @CESNET/nfb-framework`

`sudo dnf install nfb-framework`

## Build instructions

1. Clone the repository from GitHub.

    `git clone https://github.com/CESNET/ndk-sw.git`

    `cd ndk-sw`

2. Install all prerequisites on supported Linux distributions.

    `sudo ./build.sh --bootstrap`

3. Download and extract 3rd party code archive from the GitHub Release page.

	`./build.sh --prepare`

4. Compile library and tools to the cmake-build folder.

    `./build.sh --make`

5. Compile Linux driver.

    `cd drivers; ./configure; make; cd ..`

6. Load Linux driver manually.

    `sudo insmod drivers/kernel/drivers/nfb/nfb.ko`


## License

Unless otherwise noted, the content of this repository is available under the BSD 3-Clause License. Please read [LICENSE file](LICENSE).

## Repository Maintainer

- Martin Spinler (spinler AT cesnet.cz)
