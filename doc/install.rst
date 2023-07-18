Installation
============

From prebuilt RPM packages
--------------------------

The prebuilt RPM packages can be obtained via [Copr](https://copr.fedorainfracloud.org/coprs/g/CESNET/nfb-framework/):

.. code-block:: sh

    sudo dnf copr enable @CESNET/nfb-framework
    sudo dnf install nfb-framework

From source code
----------------------

1. Clone the repository from GitHub.

.. code-block:: sh

    git clone https://github.com/CESNET/ndk-sw.git
    cd ndk-sw

2. Install all prerequisites on supported Linux distributions.

.. code-block:: sh

    sudo ./build.sh --bootstrap

3. Download and extract 3rd party code archive from the GitHub Release page.

.. code-block:: sh

	./build.sh --prepare

4. Compile library and tools to the cmake-build folder.

.. code-block:: sh

    ./build.sh --make

5. Compile Linux driver.

.. code-block:: sh

    (cd drivers; ./configure; make)

6. Load Linux driver manually.

.. code-block:: sh

    sudo insmod drivers/kernel/drivers/nfb/nfb.ko

7. Install library and tools to system.

.. code-block:: sh

    sudo make -C cmake-build install

cmake typical instalation prefix is ``/usr/local/``
