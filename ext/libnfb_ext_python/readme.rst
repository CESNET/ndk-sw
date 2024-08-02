Installation
------------

libnfb_ext_python package requires nfb package for build.
The wanted nfb package is probably not available in ordinary way, so
local (venv) installation via pip can be done for example like this:

# current directory is root of this git repository
$ pip wheel -w ./localwhldir ./pynfb/
$ pip install --find-links ./localwhldir ./ext/libnfb_ext_python/
