import subprocess, os, sys

extensions = [
    'sphinx.ext.autodoc',
    'sphinx.ext.doctest',
    'sphinx.ext.mathjax',
    'sphinx.ext.viewcode',
    'sphinx.ext.imgmath',
    'sphinx.ext.todo',
    'sphinx_rtd_theme',
    'breathe',
]

#extensions.append('kerneldoc')

subprocess.call('make clean', shell=True)
subprocess.call('cd ../libnfb/doc/; doxygen', shell=True)

breathe_projects = { "libnfb": "../libnfb/doc/xml/" }
breathe_default_project = "libnfb"

html_theme = "sphinx_rtd_theme"

kerneldoc_bin = '../../linux/scripts/kernel-doc'
kerneldoc_srctree = '../drivers/'

sys.path.append('../../linux/Documentation/sphinx')
sys.path.append('../pynfb/')


project = 'NFB Software User Guide'
copyright = '2022, CESNET z.s.p.o.'
author = 'CESNET TMC'
release = '6.16.0'

from nfb import libnfb
from nfb import libnetcope
