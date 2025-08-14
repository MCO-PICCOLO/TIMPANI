#include "tlog.h"
#include "schedinfo_service.h"

SchedInfoServiceImpl::SchedInfoServiceImpl(std::shared_ptr<NodeConfigManager> node_config_manager)
    : node_config_manager_(node_config_manager)
{
    TLOG_INFO("SchedInfoServiceImpl created with GlobalScheduler integration");

    // Create GlobalScheduler instance
    global_scheduler_ = std::make_shared<GlobalScheduler>(node_config_manager_);

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

    // Convert gRPC TaskInfo to internal Task structures
    std::vector<Task> tasks = ConvertTaskInfoToTasks(request);

    // Use GlobalScheduler to process tasks
    global_scheduler_->clear();
    global_scheduler_->set_tasks(tasks);

    // Execute scheduling algorithm
    bool scheduling_success = global_scheduler_->schedule("best_fit_decreasing");

    if (scheduling_success) {
        // Get scheduled results from GlobalScheduler
        const auto& scheduled_map = global_scheduler_->get_sched_info_map();

        // Store in our sched_info_map_ (copy the results)
        for (const auto& pair : scheduled_map) {
            sched_info_map_[pair.first] = pair.second;
        }

        TLOG_INFO("Successfully scheduled ", global_scheduler_->get_total_scheduled_tasks(),
                  " tasks across ", scheduled_map.size(), " nodes");
        reply->set_status(0);  // Success
    } else {
        TLOG_ERROR("Scheduling failed for workload: ", request->workload_id());
        reply->set_status(-1);  // Indicate failure
    }

    lock.unlock();
    return Status::OK;
}

std::vector<Task> SchedInfoServiceImpl::ConvertTaskInfoToTasks(const SchedInfo* request)
{
    std::vector<Task> tasks;
    tasks.reserve(request->tasks_size());

    for (int i = 0; i < request->tasks_size(); i++) {
        const auto& grpc_task = request->tasks(i);

        Task task;
        task.name = grpc_task.name();
        task.priority = grpc_task.priority();
        task.period_us = grpc_task.period();
        task.runtime_us = grpc_task.runtime();
        task.deadline_us = grpc_task.deadline();
        task.target_node = grpc_task.node_id();

        // Convert CPU affinity from hex to string
        if (grpc_task.cpu_affinity() == 0xFFFFFFFF || grpc_task.cpu_affinity() == 0) {
            task.affinity = "any";
        } else {
            // Find the first set bit (assuming single CPU affinity for now)
            int cpu_id = 0;
            uint32_t affinity = grpc_task.cpu_affinity();
            while (affinity > 1) {
                affinity >>= 1;
                cpu_id++;
            }
            task.affinity = std::to_string(cpu_id);
        }

        // Set reasonable defaults for other fields
        task.memory_mb = 256;  // Default memory requirement
        task.assigned_node = "";  // Will be set by scheduler
        task.assigned_cpu = -1;   // Will be set by scheduler

        tasks.push_back(task);
    }

    return tasks;
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
	: service_(node_config_manager), server_(nullptr), server_thread_(nullptr)
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
        TLOG_INFO("No schedule info available");
        return;
    }

    TLOG_INFO("Dumping SchedInfoMap:");
    for (const auto& entry : sched_info_map) {
        const std::string& node_id = entry.first;
        const sched_info_t& schedule_info = entry.second;

        TLOG_INFO("Node ID: ", node_id, " with ", schedule_info.num_tasks, " tasks");

        for (int i = 0; i < schedule_info.num_tasks; i++) {
            const sched_task_t& task = schedule_info.tasks[i];
            TLOG_DEBUG("  Task Name: ", task.task_name);
            TLOG_DEBUG("    CPU Affinity: ", task.cpu_affinity);
            TLOG_DEBUG("    Priority: ", task.sched_priority);
            TLOG_DEBUG("    Policy: ", task.sched_policy);
            TLOG_DEBUG("    Period: ", task.period_ns / 1000000, "ms");
            TLOG_DEBUG("    Runtime: ", task.runtime_ns / 1000000, "ms");
            TLOG_DEBUG("    Deadline: ", task.deadline_ns / 1000000, "ms");
        }
    }
}
