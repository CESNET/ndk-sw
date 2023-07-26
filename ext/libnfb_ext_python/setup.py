import Cython
from Cython.Build import cythonize
from setuptools import Extension, setup, find_namespace_packages

setup(
    name="libnfb_ext_python",
    version="0.1.0",
    author="Martin Spinler",
    author_email="spinler@cesnet.cz",
    ext_package="libnfb-ext-python",
    ext_modules=cythonize(
        [
            Extension(
                "libnfb_ext_python",
                [
                    "libnfb_ext_python/libnfb_ext_python.pyx",
                    "libnfb_ext_python/libnfb_ext_python_shim.c",
                ],
                libraries=["nfb", "fdt"],
            ),
        ],
        include_path=["libnfb_ext_python"],
        compiler_directives={"embedsignature": True, "binding": False, **({} if Cython.__version__ <= '0.29' else {"legacy_implicit_noexcept": True})},
    ),
    py_modules=["libnfb_ext_python"],
    packages=find_namespace_packages(include=["nfb.*"]),
)
