from Cython.Build import cythonize
from setuptools import Extension, setup, find_namespace_packages

setup(
    name="libnfb-ext-python",
    version="0.1.0",
    author="Martin Spinler",
    author_email="spinler@cesnet.cz",
    ext_package="nfb.ext.python",
    ext_modules=cythonize(
        [
            Extension(
                "shim",
                [
                    "nfb/ext/python/shim.pyx",
                    "nfb/ext/python/ext_entry.c",
                ],
                libraries=["nfb", "fdt"],
            ),
        ],
        include_path=["nfb/ext/python"],
        compiler_directives={"embedsignature": True, "binding": False},
    ),
    packages=find_namespace_packages(include=["nfb.*"]),
    install_requires=['fdt'],
)
