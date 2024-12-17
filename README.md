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

## Install guide

We recommend using the Oracle Linux 8/9 operating system with Red Hat compatible kernel (RHCK), which we use for internal development.
Here is a [link to instructions on how to switch the default UEK kernel to the RHCK kernel on Oracle Linux](https://blogs.oracle.com/linux/post/changing-the-default-kernel-in-oracle-linux-its-as-simple-as-1-2-3).
For other operating systems, the installation procedure may be slightly different.

1. First, you must enable the EPEL repositories that contain the required dependencies (libfdt-devel, etc.).

    Instructions for Oracle Linux 8:

    `sudo dnf config-manager -y --set-enabled ol8_codeready_builder`

    `sudo dnf install -y oracle-epel-release-el8`

    Instructions for Oracle Linux 9:

    `sudo dnf config-manager -y --set-enabled ol9_codeready_builder`

    `sudo dnf install -y oracle-epel-release-el9`

2. Install the linux-headers package for your kernel.

    `sudo dnf install -y kernel-headers-$(uname -r)`

3. Enable our COPR repository containing the necessary packages.

    `sudo dnf copr enable -y @CESNET/nfb-framework`

4. Install the nfb-framework package, which contains NFB driver and SW tools.

    `sudo dnf install nfb-framework`

5. Optionally, you can install a package containing the NFB Python API.

    `sudo dnf install python3-nfb`


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

    `sudo modprobe spi-altera-core regmap-spi-avmm`

    `sudo insmod drivers/kernel/drivers/nfb/nfb.ko`


## License

Unless otherwise noted, the content of this repository is available under the BSD 3-Clause License. Please read [LICENSE file](LICENSE).

## Repository Maintainer

- Martin Spinler (spinler AT cesnet.cz)
