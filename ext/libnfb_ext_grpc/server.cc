#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>

extern "C" {
#include <libfdt.h>
}

#include <grpc/grpc.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include "nfb.grpc.pb.h"

#include <nfb/nfb.h>

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;

using google::protobuf::Empty;

using nfb::ext::protobuf::v1::Nfb;
using nfb::ext::protobuf::v1::FdtResponse;
using nfb::ext::protobuf::v1::ReadCompRequest;
using nfb::ext::protobuf::v1::WriteCompRequest;
using nfb::ext::protobuf::v1::ReadCompResponse;
using nfb::ext::protobuf::v1::WriteCompResponse;

class NfbServerImpl final : public Nfb::Service
{
	struct nfb_device * m_dev;
	const void *m_fdt;

public:
	explicit NfbServerImpl(const std::string& path)
	{
		m_dev = nfb_open(path.c_str());
		if (m_dev == NULL) {
			throw std::system_error(errno, std::system_category());
		}
		m_fdt = nfb_get_fdt(m_dev);
	}

	Status GetFdt(ServerContext* context, const Empty* req, FdtResponse* res) override
	{
		res->set_fdt(m_fdt, fdt_totalsize(m_fdt));
		return Status::OK;
	}

	Status ReadComp(ServerContext* context, const ReadCompRequest* req, ReadCompResponse* resp) override
	{
		struct nfb_comp * comp;
		uint8_t * data;
		int nbyte;

		comp = nfb_comp_open(m_dev, fdt_path_offset(m_fdt, req->path().c_str()));
		if (comp == NULL) {
			throw std::system_error(errno, std::system_category());
		}

		data = new uint8_t[req->nbyte()];
		nbyte = nfb_comp_read(comp, data, req->nbyte(), req->offset());
		
		resp->set_status(nbyte);
		resp->set_data(std::string((const char*)data, nbyte));

		delete []data;
		nfb_comp_close(comp);
		
		return Status::OK;
	}

	Status WriteComp(ServerContext* context, const WriteCompRequest* req, WriteCompResponse* resp) override
	{
		struct nfb_comp * comp;
		int nbyte;

		comp = nfb_comp_open(m_dev, fdt_path_offset(m_fdt, req->path().c_str()));
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
};

void run_server(const std::string& path, const std::string& addr)
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
	run_server(path, addr);

	return 0;
}
