import grpc
from concurrent import futures

import nfb
from nfb.libnfb import Nfb
from nfb.ext.protobuf.v1 import nfb_pb2_grpc
from nfb.ext.protobuf.v1 import nfb_pb2


class Servicer(nfb_pb2_grpc.NfbServicer):
    def __init__(self, dev: Nfb):
        self._dev = dev

    def GetFdt(self, request, context):
        return nfb_pb2.FdtResponse(fdt=self._dev.fdt.to_dtb())

    def ReadComp(self, request, context):
        comp = self._dev.comp_open(self._dev.fdt.get_node(request.path))
        data = comp.read(request.offset, request.nbyte)
        return nfb_pb2.ReadCompResponse(data=bytes(data), status=0)

    def WriteComp(self, request, context):
        comp = self._dev.comp_open(self._dev.fdt.get_node(request.path))
        comp.write(request.offset, bytes(request.data))
        return nfb_pb2.WriteCompResponse(status=0)


def Server(servicer, addr='127.0.0.1', port=50051):
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=4))
    nfb_pb2_grpc.add_NfbServicer_to_server(servicer, server)
    server.add_insecure_port(f'{addr}:{port}')
    server.start()

    server.__path = f'libnfb-ext-grpc.so:grpc:{addr}:{port}'
    server.path = lambda : server.__path
    return server


def run_server(servicer=None, **kwargs):
    if servicer is None:
        servicer = Servicer
    svc = servicer(nfb.open())
    server = Server(svc, **kwargs)
    # server.shutdown()
    # server.close()
