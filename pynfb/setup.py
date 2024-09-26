from setuptools import Extension, setup
from Cython.Build import cythonize

import pathlib

# This setup.py uses the system default path for includes/libraries,
# so the nfb-framework package must be already installed.
# If isn't installed, define include_dirs and library_dirs as in setup.py.in
ext_extra_args = dict(
    include_dirs=[
        str(pathlib.Path(__file__).parent / pathlib.Path("../libnfb/include")),
        str(pathlib.Path(__file__).parent / pathlib.Path("../drivers/kernel/include")),
    ],
    library_dirs=[
        str(pathlib.Path(__file__).parent / pathlib.Path("../cmake-build/libnfb")),
    ],
)

libnfb_ext_extra_args = libnetcope_ext_extra_args = dict()
#libnfb_ext_extra_args = libnetcope_ext_extra_args = ext_extra_args


setup(
    name = "nfb",
    version = "0.2.0",
    author = "Martin Spinler",
    author_email = "spinler@cesnet.cz",
    ext_package = "nfb",
    ext_modules = cythonize(
        [
            Extension("libnfb", ["nfb/libnfb.pyx"], libraries=["nfb", "fdt"], **libnfb_ext_extra_args),
            Extension("libnetcope", ["nfb/libnetcope.pyx"], libraries=["nfb", "fdt"], **libnetcope_ext_extra_args),
        ],
        include_path=['nfb'],
        compiler_directives={'embedsignature': True, 'binding': False},
    ),
    py_modules=['nfb'],
    packages=['nfb'],
    package_dir={'nfb': 'nfb'},
    package_data = {
        'nfb': ['*.pxd'],
    },
    install_requires=['fdt'],
)
