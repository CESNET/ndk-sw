/*
 * file       : client_dma_vas.hh
 * Copyright (C) 2024 CESNET z. s. p. o.b
 * description: gRPC client - plugin for virtual address space DMA
 * author     : Radek Iša <isa@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __CLIENT_DMA_VAS_HH__
#define __CLIENT_DMA_VAS_HH__

#include <mutex>
#include <queue>

#include <dma.grpc.pb.h>


namespace pb_dma = nfb::ext::protobuf::v1;

template <typename ITEM_TYPE> class fifo_lock : public std::queue<ITEM_TYPE>
{
private:
	std::mutex mux;

public:
	void lock() { mux.lock(); }

	void unlock() { mux.unlock(); }
};

namespace pb_dma = nfb::ext::protobuf::v1;

class DmaAccess : public grpc::ClientBidiReactor<pb_dma::DmaResponse, pb_dma::DmaRequest>
{
protected:
	grpc::ClientContext context;
	grpc::Status status;

	fifo_lock<pb_dma::DmaResponse> resp_que;
	bool bind;
	pb_dma::DmaRequest req;
	pb_dma::DmaResponse resp;

	void OnWriteDone(bool ok) override;
	void OnReadDone(bool ok) override;
	void OnDone(const grpc::Status &s) override;
public:
	DmaAccess(pb_dma::Dma::Stub* stub);

	grpc::Status Await();
};

class NfbDmaClient
{
public:
	DmaAccess* process;
	std::unique_ptr<pb_dma::Dma::Stub> stub;
	std::shared_ptr<grpc::Channel> channel;

public:
	NfbDmaClient(std::shared_ptr<grpc::Channel> channel) :
			channel(channel)
	{
		this->stub = pb_dma::Dma::NewStub(this->channel);
		this->process = new DmaAccess(this->stub.get());
	}
};

#endif
