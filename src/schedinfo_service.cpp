#include "tlog.h"
#include "schedinfo_service.h"

SchedInfoServiceImpl::SchedInfoServiceImpl(std::shared_ptr<NodeConfigManager> node_config_manager)
    : node_config_manager_(node_config_manager)
{
    TLOG_INFO("SchedInfoServiceImpl created with internal scheduling and node configuration");
    if (node_config_manager_ && node_config_manager_->IsLoaded()) {
        TLOG_INFO("Node configuration loaded with ", node_config_manager_->GetAllNodes().size(), " nodes");
    } else {
        TLOG_INFO("Using default node configuration");
    }
}

Status SchedInfoServiceImpl::AddSchedInfo(ServerContext* context,
                                          const SchedInfo* request,
                                          Response* reply)
{
    TLOG_INFO("Received SchedInfo: ", request->workload_id(), " with ",
              request->tasks_size(), " tasks");
    // Print detailed task information
    for (int i = 0; i < request->tasks_size(); i++) {
        const auto& task = request->tasks(i);
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

    std::unique_lock<std::shared_mutex> lock(sched_info_mutex_);

    // Check if workload_id already exists
    if (sched_info_map_.find(request->workload_id()) != sched_info_map_.end()) {
        TLOG_ERROR("Workload ID ", request->workload_id(), " already exists");
        reply->set_status(-1);  // Indicate failure
        return Status::OK;
    }

    // Store the scheduling information in sched_info_map_
    std::vector<TaskInfo> task_vector;
    task_vector.reserve(request->tasks_size());
    std::copy(request->tasks().begin(), request->tasks().end(),
              std::back_inserter(task_vector));
    sched_info_map_[request->workload_id()] = task_vector;

    lock.unlock();

    reply->set_status(0);  // Success
    return Status::OK;
}

SchedInfoMap SchedInfoServiceImpl::GetSchedInfoMap() const
{
    std::shared_lock<std::shared_mutex> lock(sched_info_mutex_);
    return sched_info_map_;
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

SchedInfoServer::SchedInfoServer(std::shared_ptr<NodeConfigManager> node_config_manager)
	: server_(nullptr), server_thread_(nullptr)
{
    TLOG_INFO("SchedInfoServer created with node configuration");
}

SchedInfoServer::~SchedInfoServer() { Stop(); }

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
    server_thread_ =
        std::make_unique<std::thread>([this]() { server_->Wait(); });

    return true;
}

void SchedInfoServer::Stop()
{
    if (server_) {
        server_->Shutdown();
    }
    if (server_thread_ && server_thread_->joinable()) {
        server_thread_->join();
    }
}

SchedInfoMap SchedInfoServer::GetSchedInfoMap() const
{
    return service_.GetSchedInfoMap();
}

void SchedInfoServer::DumpSchedInfo()
{
    const SchedInfoMap sched_info_map = service_.GetSchedInfoMap();

    if (sched_info_map.empty()) {
        // No schedule info available
        return;
    }
    TLOG_INFO("Dumping SchedInfoMap:");
    for (const auto& entry : sched_info_map) {
        const std::string& workload_id = entry.first;
        const std::vector<TaskInfo>& tasks = entry.second;
        TLOG_INFO("Workload ID: ", workload_id, " with ", tasks.size(),
                  " tasks");
        for (const auto& task : tasks) {
            TLOG_DEBUG("  Task Name: ", task.name());
            TLOG_DEBUG("    Node ID: ", task.node_id());
            TLOG_DEBUG("    Priority: ", task.priority());
            TLOG_DEBUG("    Policy: ", task.policy());
            TLOG_DEBUG("    CPU Affinity: 0x", std::hex, task.cpu_affinity(),
                       std::dec);
            TLOG_DEBUG("    Period: ", task.period());
            TLOG_DEBUG("    Release Time: ", task.release_time());
            TLOG_DEBUG("    Runtime: ", task.runtime());
            TLOG_DEBUG("    Deadline: ", task.deadline());
            TLOG_DEBUG("    Max Deadline Misses: ", task.max_dmiss());
        }
    }
}
