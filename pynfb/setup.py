from setuptools import Extension, setup
from Cython.Build import cythonize

# This setup.py uses the system default path for includes/libraries,
# so the nfb-framework package must be already installed.
# If isn't installed, define include_dirs and library_dirs as in setup.py.in

setup(
    name = "nfb",
    version = "0.1.0",
    author = "Martin Spinler",
    author_email = "spinler@cesnet.cz",
    ext_package = "nfb",
    ext_modules = cythonize(
        [
            Extension("libnfb", ["nfb/libnfb.pyx"], libraries=["nfb", "fdt"],
#                include_dirs=[str(pathlib.Path(__file__).parent / pathlib.Path("../libnfb/include"))],
#                library_dirs=[str(pathlib.Path(__file__).parent / pathlib.Path("../cmake-build/libnfb"))],
            ),
            Extension("libnetcope", ["nfb/libnetcope.pyx"], libraries=["nfb", "fdt"],
#                include_dirs=[str(pathlib.Path(__file__).parent / pathlib.Path("../libnfb/include"))],
#                library_dirs=[str(pathlib.Path(__file__).parent / pathlib.Path("../cmake-build/libnfb"))],
            ),
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
)
