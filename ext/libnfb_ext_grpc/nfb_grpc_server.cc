#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>

#include <libfdt.h>

#include <grpc/grpc.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#ifdef BAZEL_BUILD
#include "examples/protos/route_guide.grpc.pb.h"
#else
#include "nfb_grpc.grpc.pb.h"
#endif

#include <nfb/nfb.h>

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;

using nfb_grpc::Nfb;
using nfb_grpc::nfb_fdt;
using nfb_grpc::nfb_rpc_device;
using nfb_grpc::nfb_read_req;
using nfb_grpc::nfb_write_req;
using nfb_grpc::nfb_read_resp;
using nfb_grpc::nfb_write_resp;

class NfbServerImpl final : public Nfb::Service
{
public:
	explicit NfbServerImpl(const std::string& path)
	{
		m_dev = nfb_open(path.c_str());
		if (m_dev == NULL) {
			throw std::system_error(errno, std::system_category());
		}
	}

	Status Nfb_fdt_get(ServerContext* context, const nfb_rpc_device* dev, nfb_fdt* fdt) override
	{
		const void * raw_fdt = nfb_get_fdt(m_dev);
		int size = fdt_totalsize(raw_fdt);

		fdt->set_fdt(raw_fdt, size);

		return Status::OK;
	}

	Status Nfb_comp_read(ServerContext* context, const nfb_read_req* req, nfb_read_resp* resp) override
	{
		struct nfb_comp * comp = nfb_comp_open(m_dev, req->fdt_offset());
		uint8_t * data;
		int nbyte;

		if (comp == NULL) {
			throw std::system_error(errno, std::system_category());
		}

		//data = (uint8_t*) malloc(req->nbyte());
		data = new uint8_t[req->nbyte()];
		nbyte = nfb_comp_read(comp, data, req->nbyte(), req->offset());
		
		resp->set_status(nbyte);
		resp->set_data(std::string((const char*)data, nbyte));

		delete []data;
		nfb_comp_close(comp);
		
		return Status::OK;
	}

	Status Nfb_comp_write(ServerContext* context, const nfb_write_req* req, nfb_write_resp* resp) override
	{
		struct nfb_comp * comp = nfb_comp_open(m_dev, req->fdt_offset());
		int nbyte;

		if (comp == NULL) {
			throw std::system_error(errno, std::system_category());
		}

		nbyte = nfb_comp_write(comp, req->data().c_str(), req->nbyte(), req->offset());
		
		resp->set_status(nbyte);
		nfb_comp_close(comp);
		
		return Status::OK;
	}

	~NfbServerImpl()
	{
		nfb_close(m_dev);
	}

private:
	struct nfb_device * m_dev;
};

void RunServer(const std::string& path, const std::string& addr)
{
	std::string server_address(addr);
	NfbServerImpl service(path);

	ServerBuilder builder;
	builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
	builder.RegisterService(&service);
	std::unique_ptr<Server> server(builder.BuildAndStart());
	std::cout << "NFB gRPC server listening on " << server_address << std::endl;
	server->Wait();
}

int main(int argc, char** argv)
{
	const char * path = nfb_default_dev_path();
	const char * addr = "127.0.0.1:50051";

	int opt;
	while ((opt = getopt(argc, argv, "d:a:")) != -1) {
		switch (opt) {
		case 'd': path = optarg; break;
	        case 'a': addr = optarg; break;
	        default:
	            fprintf(stderr, "Usage: %s [-d nfb_device] [-a grpc_server_addr]\n", argv[0]);
	            exit(EXIT_FAILURE);
		}
        }
	RunServer(path, addr);

	return 0;
}
