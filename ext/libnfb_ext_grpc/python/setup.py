from setuptools import setup
from setuptools.command.build import build

class custom_build(build):
    sub_commands = [
        ('build_grpc', None),
    ] + build.sub_commands

setup(cmdclass={'build': custom_build})
