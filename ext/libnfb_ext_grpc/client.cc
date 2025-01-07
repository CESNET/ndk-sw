
#include <chrono>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <thread>

#include <grpc/grpc.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

#include <nfb.grpc.pb.h>

extern "C" {
#include <libfdt.h>
}

#include <nfb/nfb.h>
#include <nfb/ext.h>

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using grpc::Status;

using google::protobuf::Empty;

using nfb::ext::protobuf::v1::Nfb;
using nfb::ext::protobuf::v1::FdtResponse;
using nfb::ext::protobuf::v1::ReadCompRequest;
using nfb::ext::protobuf::v1::WriteCompRequest;
using nfb::ext::protobuf::v1::ReadCompResponse;
using nfb::ext::protobuf::v1::WriteCompResponse;


class NfbClient
{
	void *m_fdt;
	std::string m_path;

public:
	NfbClient(std::string path, std::shared_ptr<Channel> channel) :
			stub_(Nfb::NewStub(channel)),
			m_fdt(NULL),
			m_path(path)
	{
	}

	void *get_fdt()
	{
		if (m_fdt)
			return m_fdt;

		static const int EXTRA_LEN = 16384;
		int node;
		size_t len;
		Empty fdt_req;
		FdtResponse fdt;

		ClientContext context;
		Status status = stub_->GetFdt(&context, fdt_req, &fdt);

		if (!status.ok()) {
			throw std::runtime_error("gRPC client: GetFdt failed");
		}

		len = fdt.fdt().length();

		m_fdt = malloc(len + EXTRA_LEN);
		if (m_fdt == NULL)
			throw std::bad_alloc();

		memcpy(m_fdt, fdt.fdt().c_str(), len);
		fdt_set_totalsize(m_fdt, len + EXTRA_LEN);

		/* Create node with extension metadata */
		node = fdt_path_offset(m_fdt, "/drivers");
		if (node < 0) {
			node = fdt_path_offset(m_fdt, "/");
			node = fdt_add_subnode(m_fdt, node, "drivers");
		}

		node = fdt_add_subnode(m_fdt, node, "libnfb-ext");
		fdt_setprop_string(m_fdt, node, "devname", m_path.c_str());

		return m_fdt;
	}

	std::unique_ptr<Nfb::Stub> stub_;
};

class NfbBus
{
	int comp_node;
	off_t m_base;
	NfbClient *m_dev;

	std::string m_comp_path;
public:

	NfbBus(NfbClient *dev, int bus_node, int comp_node) :
			m_dev(dev)
	{
		void *fdt;
		static const int MAX_PATH_LEN = 512;
		char path[MAX_PATH_LEN];
		int proplen;
		const fdt32_t *prop;

		fdt = dev->get_fdt();
		prop = (fdt32_t*) fdt_getprop(fdt, comp_node, "reg", &proplen);
		if (proplen != sizeof(*prop) * 2) {
			throw std::length_error("wrong size of reg property");
		}
		m_base = fdt32_to_cpu(prop[0]);

		if (fdt_get_path(fdt, comp_node, path, MAX_PATH_LEN) != 0) {
			throw std::length_error("DT Path is over maximum length");
	        }
		m_comp_path = std::string(path);
	}

	ssize_t comp_read(void *buffer, int nbyte, int offset)
	{
		ReadCompRequest req;
		ReadCompResponse resp;

		req.set_path(m_comp_path);
		req.set_nbyte(nbyte);
		req.set_offset(offset - m_base);

		ClientContext context;
		Status status = m_dev->stub_->ReadComp(&context, req, &resp);

		if (!status.ok()) {
			return -1;
		}

		memcpy(buffer, resp.data().c_str(), resp.data().length());
		return resp.data().length();
	}

	ssize_t comp_write(const void *buffer, int nbyte, int offset) {
		WriteCompRequest req;
		WriteCompResponse resp;

		req.set_path(m_comp_path);
		req.set_nbyte(nbyte);
		req.set_offset(offset - m_base);
		req.set_data(std::string((const char*)buffer, nbyte));

		ClientContext context;
		Status status = m_dev->stub_->WriteComp(&context, req, &resp);

		if (!status.ok()) {
			return -1;
		}

		return nbyte;
	}
};


extern "C" {


static const char *nfb_grpc_prefix = "grpc:";

static int nfb_grpc_open(const char *devname, int oflag, void **priv, void **fdt)
{
	NfbClient *nfb;

	devname += strlen(nfb_grpc_prefix);
	try {
		nfb = new NfbClient(
			devname,
			grpc::CreateChannel(
				devname,
				grpc::InsecureChannelCredentials()
			)
		);
	} catch(...) {
		return -ENODEV;
	}

	try {
		*fdt = nfb->get_fdt();
	} catch(...) {
		delete nfb;
		return -ENODEV;
	}

	*priv = nfb;
	return 0;
}

static void nfb_grpc_close(void *dev_priv)
{
	NfbClient *nfb = (NfbClient *)dev_priv;
	delete nfb;
}

static ssize_t nfb_bus_grpc_read(void *bus_priv, void *buf, size_t nbyte, off_t offset)
{
	NfbBus *bus = (NfbBus *) bus_priv;
	return bus->comp_read(buf, nbyte, offset);
}

static ssize_t nfb_bus_grpc_write(void *bus_priv, const void *buf, size_t nbyte, off_t offset)
{
	NfbBus *bus = (NfbBus *) bus_priv;
	return bus->comp_write(buf, nbyte, offset);
}

static int nfb_grpc_bus_open(void *dev_priv, int bus_node, int comp_node, void ** bus_priv, struct libnfb_bus_ext_ops* ops)
{
	NfbClient* dev = (NfbClient *) dev_priv;
	NfbBus *bus;
	try {
		bus = new NfbBus(dev, bus_node, comp_node);
	} catch (...) {
		return -EBADF;
	}

	ops->read = nfb_bus_grpc_read;
	ops->write = nfb_bus_grpc_write;
	*bus_priv = bus;
	return 0;
}

static void nfb_grpc_bus_close(void *bus_priv)
{
	NfbBus *bus = (NfbBus *) bus_priv;
	delete bus;
}

static int nfb_grpc_comp_lock(const struct nfb_comp *comp, uint32_t features)
{
	/* TODO */
	return 1;
}

static void nfb_grpc_comp_unlock(const struct nfb_comp *comp, uint32_t features)
{
	/* TODO */
}

struct libnfb_ext_abi_version libnfb_ext_abi_version = libnfb_ext_abi_version_current;

static struct libnfb_ext_ops nfb_grpc_ops = {
	.open = nfb_grpc_open,
	.close = nfb_grpc_close,
	.bus_open_mi = nfb_grpc_bus_open,
	.bus_close_mi = nfb_grpc_bus_close,
	.comp_lock = nfb_grpc_comp_lock,
	.comp_unlock = nfb_grpc_comp_unlock,
};

int libnfb_ext_get_ops(const char *devname, struct libnfb_ext_ops *ops)
{
	if (strncmp(devname, nfb_grpc_prefix, strlen(nfb_grpc_prefix)) == 0) {
		*ops = nfb_grpc_ops;
		return 1;
	} else {
		return 0;
	}
}

}
