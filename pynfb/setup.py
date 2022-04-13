#!/usr/bin/env python

from distutils.core import setup, Extension

nfb_module = Extension('_nfb',
	sources=['pynfb.i', 'pynfb.cxx'],
	swig_opts=['-c++'],
	extra_compile_args=['-std=c++11'],
	libraries=['fdt', 'nfb'],
)

setup (name = 'python2-pynfb',
	version = '0.2.1',
	author      = "spinler@cesnet.cz",
	description = """NFB python module""",
	ext_modules = [nfb_module],
	py_modules = ["nfb"],
)
