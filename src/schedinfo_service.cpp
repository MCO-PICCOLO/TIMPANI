#include <iostream>

#include "tlog.h"
#include "schedinfo_service.h"

SchedInfoServiceImpl::SchedInfoServiceImpl()
{
}

Status SchedInfoServiceImpl::AddSchedInfo(ServerContext *context,
                                          const SchedInfo *request,
                                          Response *reply)
{
    TLOG_INFO("Received SchedInfo: ", request->workload_id(), " with ",
              request->tasks_size(), " tasks");
    // Print detailed task information
    for (int i = 0; i < request->tasks_size(); i++) {
        const auto &task = request->tasks(i);
        TLOG_DEBUG("Task ", i, ": ", task.name());
        TLOG_DEBUG("  Priority: ", task.priority());
        TLOG_DEBUG("  Policy: ", SchedPolicyToStr(task.policy()));
        TLOG_DEBUG("  CPU Affinity: 0x", std::hex, task.cpu_affinity(),
                   std::dec);
        TLOG_DEBUG("  Period: ", task.period());
        TLOG_DEBUG("  Release Time: ", task.release_time());
        TLOG_DEBUG("  Runtime: ", task.runtime());
        TLOG_DEBUG("  Deadline: ", task.deadline());
        TLOG_DEBUG("  Max Deadline Misses: ", task.max_dmiss());
        TLOG_DEBUG("  Node ID: ", task.node_id());
    }

    reply->set_status(0);  // Success
    return Status::OK;
}

const char* SchedInfoServiceImpl::SchedPolicyToStr(SchedPolicy policy)
{
    switch (policy) {
        case SchedPolicy::NORMAL:
            return "NORMAL";
        case SchedPolicy::FIFO:
            return "FIFO";
        case SchedPolicy::RR:
            return "RR";
        default:
            return "UNKNOWN";
    }
}

SchedInfoServer::SchedInfoServer()
    : server_(nullptr),
      server_thread_(nullptr)
{
}

SchedInfoServer::~SchedInfoServer()
{
    Stop();
}

bool SchedInfoServer::Start(int port)
{
    std::string server_addr = "0.0.0.0";
    server_addr += ":" + std::to_string(port);

    ServerBuilder builder;
    builder.AddListeningPort(server_addr, grpc::InsecureServerCredentials());
    builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);
    builder.RegisterService(&service_);

    server_ = builder.BuildAndStart();
    if (!server_) {
        TLOG_ERROR("Failed to start SchedInfoService on ", server_addr);
        return false;
    }

    // Start the server in a separate thread
    server_thread_ = std::make_unique<std::thread>([this]() {
        server_->Wait();
    });
    server_thread_->detach();

    return true;
}

void SchedInfoServer::Stop()
{
    if (server_) {
        server_->Shutdown();
    }
    if(server_thread_ && server_thread_->joinable()) {
        server_thread_->join();
    }
}
