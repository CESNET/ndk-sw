/*
 * file       : client_dma_vas.cc
 * Copyright (C) 2024 CESNET z. s. p. o.b
 * description: gRPC client - plugin for virtual address space DMA
 * author     : Radek Iša <isa@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

#include <grpc/grpc.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

#include "client_dma_vas.hh"


DmaAccess::DmaAccess(pb_dma::Dma::Stub* stub) : bind(true)
{
	stub->async()->RqStream(&context, this);
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

void DmaAccess::OnReadDone(bool ok)
{
	bool first_write;
	pb_dma::DmaResponse resp_act;

	if (ok) {
		StartRead(&req);

		// Memory access
		pb_dma::DmaOperation req_type = req.type();
		resp_act.set_status(0);
		if (req_type == pb_dma::DmaOperation::DMA_READ) {
			resp_act.set_data((void*)(req.addr()), req.nbyte());
		} else {
			memcpy((void*)(req.addr()), req.data().c_str(), req.data().size());
			resp_act.clear_data();
		}

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
