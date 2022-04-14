## Repository content

This repository contains software framework for NDK based cards. It includes

 - Linux kernel driver
 - libnfb userspace library
 - nfb & ndp tools for basic usage

## Build instructions

1. Install all prerequisites on supported Linux distributions, download and extract the 3rdparty.tar.gz from GitHub Release page.

    `sudo ./build.sh --bootstrap`

2. Compile libary and tools to the cmake-build folder

    `./build.sh --make`

3. Compile Linux driver

    `cd drivers; ./configure; make`

4. Load Linux driver manually

    `sudo insmod kernel/drivers/nfb/nfb.ko`

## Documentation

[** NFB Software User Guide **](https://cesnet.github.io/ndk-sw) is automatically generated documentation based on the [Sphinx](https://www.sphinx-doc.org) (public GitHub Pages - built from main branch).

## Repository Maintainer

- Martin Spinler (spinler AT cesnet.cz)
