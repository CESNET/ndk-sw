/*
 * file       : client_dma_vas.cc
 * Copyright (C) 2024 CESNET z. s. p. o.b
 * description: gRPC client - plugin for virtual address space DMA
 * author     : Radek Iša <isa@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include <sys/uio.h>
#include <unistd.h>

#include <grpc/grpc.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

#include "client_dma_vas.hh"


DmaAccess::DmaAccess(pb_dma::Dma::Stub* stub) : bind(true)
{
#ifdef GRPC_ASYNC_EXPERIMENTAL
	stub->experimental_async()->RqStream(&context, this);
#else
	stub->async()->RqStream(&context, this);
#endif
	StartRead(&req);
	StartCall();
}

void DmaAccess::OnWriteDone(bool ok)
{
	if (ok) {
		bool next_write;
		resp_que.lock();
		resp_que.pop();
		next_write = (resp_que.size() > 0);
		if (next_write) {
			resp = resp_que.front();
		}
		resp_que.unlock();

		if (next_write) {
			StartWrite(&resp);
		}
	}
}

/*
 * Access own memory through process_vm_readv/writev instead of a direct memcpy:
 * an invalid address in the DMA request (e.g. memory of another process using the same
 * simulated device) then fails with EFAULT instead of crashing this process.
 */
static int vas_access(bool write, void *local, uint64_t addr, int32_t nbyte)
{
	struct iovec liov = {local, (size_t) nbyte};
	struct iovec riov = {(void *) addr, (size_t) nbyte};
	ssize_t ret;

	if (nbyte < 0)
		return -EINVAL;

	if (write)
		ret = process_vm_writev(getpid(), &liov, 1, &riov, 1, 0);
	else
		ret = process_vm_readv(getpid(), &liov, 1, &riov, 1, 0);

	if (ret < 0)
		return -errno;
	return (ret == nbyte) ? 0 : -EFAULT;
}

void DmaAccess::OnReadDone(bool ok)
{
	bool first_write;
	int ret;
	pb_dma::DmaResponse resp_act;

	if (ok) {
		// Memory access; req is reused by the next StartRead, so process it first
		pb_dma::DmaOperation req_type = req.type();
		if (req_type == pb_dma::DmaOperation::DMA_READ) {
			std::string data(req.nbyte() > 0 ? req.nbyte() : 0, '\0');
			ret = vas_access(false, data.empty() ? nullptr : &data[0], req.addr(), req.nbyte());
			if (ret == 0)
				resp_act.set_data(std::move(data));
		} else {
			// local buffer is only read by process_vm_writev
			const std::string &data = req.data();
			ret = vas_access(true, const_cast<char *>(data.data()), req.addr(), data.size());
			resp_act.clear_data();
		}

		resp_act.set_status(ret);
		if (ret) {
			std::cerr << "libnfb-ext-grpc: DMA " << (req_type == pb_dma::DmaOperation::DMA_READ ? "read" : "write")
				<< " of " << req.nbyte() << " B at 0x" << std::hex << req.addr() << std::dec
				<< " failed: " << strerror(-ret) << std::endl;
		}

		StartRead(&req);

		// push to queue
		resp_que.lock();
		first_write = (resp_que.size() == 0);
		resp_que.push(resp_act);
		resp_que.unlock();

		if (first_write) {
			resp = resp_act;
			StartWrite(&resp);
		}
	}
}

void DmaAccess::OnDone(const grpc::Status& s)
{
	status = s;
	bind = false;
}

grpc::Status DmaAccess::Await()
{
	while (bind) {
		std::this_thread::sleep_for(std::chrono::milliseconds(300));
	}
	return status;
}
