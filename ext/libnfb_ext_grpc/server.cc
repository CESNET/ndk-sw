#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <iostream>
#include <map>
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

#include <nfb/nfb.h>

#include "nfb/ext/protobuf/v1/nfb.grpc.pb.h"

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
using nfb::ext::protobuf::v1::LockRequest;
using nfb::ext::protobuf::v1::LockResponse;

class NfbServerImpl final : public Nfb::Service
{
	struct nfb_device * m_dev;
	const void *m_fdt;
	std::string m_path;

public:
	explicit NfbServerImpl(const std::string& path) : m_path(path)
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

	/*
	 * Component locks of one client session (one opened device on the client side).
	 * The session gets its own opened device, so the driver is the only lock table:
	 * the session is a separate lock owner, also against local processes, and closing
	 * its device at the end of the session (also a disconnected client) releases its locks.
	 */
	Status LockSession(ServerContext* context, ServerReaderWriter<LockResponse, LockRequest>* stream) override
	{
		LockRequest req;
		struct nfb_device *dev = NULL;
		std::map<std::string, struct nfb_comp *> comps;

		while (stream->Read(&req)) {
			LockResponse resp;
			struct nfb_comp *comp = NULL;
			int ret;

			if (dev == NULL)
				dev = nfb_open(m_path.c_str());

			if (dev != NULL) {
				auto it = comps.find(req.path());
				if (it != comps.end()) {
					comp = it->second;
				} else {
					comp = nfb_comp_open(dev, fdt_path_offset(nfb_get_fdt(dev), req.path().c_str()));
					if (comp != NULL)
						comps[req.path()] = comp;
				}
			}

			if (comp == NULL) {
				ret = -ENODEV;
			} else if (req.op() == LockRequest::LOCK) {
				ret = nfb_comp_trylock(comp, req.features(), 0);
				if (ret == 0)
					ret = 1;
			} else {
				nfb_comp_unlock(comp, req.features());
				ret = 0;
			}

			resp.set_status(ret);
			if (!stream->Write(resp))
				break;
		}

		for (auto &c : comps)
			nfb_comp_close(c.second);
		if (dev != NULL)
			nfb_close(dev);

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
