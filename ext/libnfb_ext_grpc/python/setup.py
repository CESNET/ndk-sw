import os

from setuptools import setup
from setuptools.command.build import build
from setuptools_grpc.build_grpc import build_grpc


# The protobuf files are provided by the nfb-framework package.
# Detect the proto directory: prefer in-tree (../) for development builds,
# fall back to the system install path (/usr/include).
def _detect_proto_path():
    candidates = [
        os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."),  # in-tree
        "/usr/include",                                                   # system-installed
    ]
    for d in candidates:
        if os.path.isfile(os.path.join(d, "nfb/ext/protobuf/v1/nfb.proto")):
            return d
    return "."


class custom_build_grpc(build_grpc):
    def finalize_options(self):
        self.proto_path = _detect_proto_path()
        super().finalize_options()


class custom_build(build):
    sub_commands = [
        ('build_grpc', None),
    ] + build.sub_commands


setup(
    cmdclass={'build': custom_build, 'build_grpc': custom_build_grpc},
)
