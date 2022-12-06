
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

#include <nfb_grpc.grpc.pb.h>

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

using nfb_grpc::Nfb;
using nfb_grpc::nfb_fdt;
using nfb_grpc::nfb_rpc_device;
using nfb_grpc::nfb_read_req;
using nfb_grpc::nfb_write_req;
using nfb_grpc::nfb_read_resp;
using nfb_grpc::nfb_write_resp;


class NfbClient {
	void *m_fdt;
	public:
		NfbClient(std::shared_ptr<Channel> channel)
			: stub_(Nfb::NewStub(channel)), m_fdt(NULL) {
			}

		void* get_fdt() {
			if (m_fdt)
				return m_fdt;
			void *raw_fdt;
			nfb_rpc_device dev;
			nfb_fdt fdt;

			ClientContext context;
			Status status = stub_->Nfb_fdt_get(&context, dev, &fdt);

			if (!status.ok()) {
				std::cout << "GetFeature rpc failed." << std::endl;
				return NULL;
			}

			raw_fdt = malloc(fdt.fdt().length());
			if (raw_fdt == NULL)
				return NULL;

			memcpy(raw_fdt, fdt.fdt().c_str(), fdt.fdt().length());

			m_fdt = raw_fdt;
			return raw_fdt;
		}

		int nfb_comp_read(int fdt_offset, void *buffer, int nbyte, int offset) {
			nfb_read_req req;
			nfb_read_resp resp;

			req.set_fdt_offset(fdt_offset);
			req.set_nbyte(nbyte);
			req.set_offset(offset);

			ClientContext context;
			Status status = stub_->Nfb_comp_read(&context, req, &resp);

			if (!status.ok()) {
				std::cout << "grpc nfb_comp_read failed." << std::endl;
				return 0;
			}

			memcpy(buffer, resp.data().c_str(), resp.data().length());
			return resp.data().length();
		}

		int nfb_comp_write(int fdt_offset, const void *buffer, int nbyte, int offset) {
			nfb_write_req req;
			nfb_write_resp resp;

			req.set_fdt_offset(fdt_offset);
			req.set_nbyte(nbyte);
			req.set_offset(offset);
			req.set_data(std::string((const char*)buffer, nbyte));

			ClientContext context;
			Status status = stub_->Nfb_comp_write(&context, req, &resp);

			if (!status.ok()) {
				std::cout << "grpc nfb_comp_write failed." << std::endl;
				return 0;
			}

			return resp.status();
		}

		std::unique_ptr<Nfb::Stub> stub_;
};


extern "C" {

struct nfb_grpc_bus_priv {
	int comp_node;
	off_t base;
	NfbClient *nfb;
};

static const char *nfb_grpc_prefix = "grpc:";

static int nfb_grpc_open(const char *devname, int oflag, void **priv, void **fdt)
{

	NfbClient *nfb = new NfbClient(
			grpc::CreateChannel(devname + strlen(nfb_grpc_prefix),
			grpc::InsecureChannelCredentials())
	);

	*fdt = nfb->get_fdt();
	if (*fdt == NULL) {
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
	struct nfb_grpc_bus_priv *bus = (struct nfb_grpc_bus_priv *)bus_priv;
	return bus->nfb->nfb_comp_read(bus->comp_node, buf, nbyte, offset - bus->base);
}

static ssize_t nfb_bus_grpc_write(void *bus_priv, const void *buf, size_t nbyte, off_t offset)
{
	struct nfb_grpc_bus_priv *bus = (struct nfb_grpc_bus_priv *)bus_priv;
	return bus->nfb->nfb_comp_write(bus->comp_node, buf, nbyte, offset - bus->base);
}

static int nfb_grpc_bus_open(void *dev_priv, int bus_node, int comp_node, void ** bus_priv, struct libnfb_bus_ext_ops* ops)
{
	NfbClient* dev = (NfbClient *) dev_priv;
	struct nfb_grpc_bus_priv *bus;

	int proplen;
	const fdt32_t *prop;

	prop = (fdt32_t*) fdt_getprop(dev->get_fdt(), comp_node, "reg", &proplen);
	if (proplen != sizeof(*prop) * 2) {
		return -EBADFD;
	}

	bus = (struct nfb_grpc_bus_priv *) malloc(sizeof(*bus));
	if (bus == NULL)
		return -ENOMEM;

	bus->nfb = dev;
	bus->comp_node = comp_node;
	bus->base = fdt32_to_cpu(prop[0]);

	ops->read = nfb_bus_grpc_read;
	ops->write = nfb_bus_grpc_write;
	*bus_priv = bus;

	return 0;
}

static void nfb_grpc_bus_close(void *bus_priv)
{
	free(bus_priv);
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
