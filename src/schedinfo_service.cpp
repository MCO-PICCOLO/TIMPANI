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
        TLOG_DEBUG("  Policy: ", task.policy());
        TLOG_DEBUG("  CPU Affinity: 0x", std::hex, task.cpu_affinity(),
                   std::dec);
        TLOG_DEBUG("  Period: ", task.period());
        TLOG_DEBUG("  Runtime: ", task.runtime());
        TLOG_DEBUG("  Deadline: ", task.deadline());
        TLOG_DEBUG("  Release Time: ", task.release_time());
        TLOG_DEBUG("  Max Deadline Misses: ", task.max_dmiss());
        TLOG_DEBUG("  Node ID: ", task.node_id());
    }

    std::unique_lock<std::shared_mutex> lock(sched_info_mutex_);

    // Currently only supports one workload at a time
    // Clear previous workload if exists to support workload replacement
    if (!sched_info_map_.empty()) {
        std::string prev_workload = sched_info_map_.begin()->first;
        TLOG_WARN("Replacing existing workload '", prev_workload, "' with new workload '", request->workload_id(), "'");
        sched_info_map_.clear();
        // Clear global scheduler state when replacing workload
        global_scheduler_->clear();
    }

    // Convert gRPC TaskInfo to internal Task structures
    std::vector<Task> tasks = ConvertTaskInfoToTasks(request);

    // Use GlobalScheduler to process tasks
    global_scheduler_->clear();
    global_scheduler_->set_tasks(tasks);

    // Validate workload distribution across nodes
    std::map<std::string, int> node_task_counts;
    for (const auto& task : tasks) {
        node_task_counts[task.target_node]++;
    }
    
    TLOG_INFO("Workload '", request->workload_id(), "' distributes tasks across ", 
              node_task_counts.size(), " nodes:");
    for (const auto& entry : node_task_counts) {
        TLOG_INFO("  Node '", entry.first, "': ", entry.second, " tasks");
    }

    // Execute scheduling algorithm with new target node priority logic
    bool scheduling_success = global_scheduler_->schedule("target_node_priority");
    if (!scheduling_success) {
        TLOG_ERROR("Scheduling failed for workload: ", request->workload_id());
        reply->set_status(-1);  // Indicate failure
        return Status::OK;
    }

    // Get scheduled results from GlobalScheduler
    const NodeSchedInfoMap& node_sched_map = global_scheduler_->get_sched_info_map();

    // Verify the structure: NodeSchedInfoMap is std::map<std::string, sched_info_t>
    // where key is node_id and value is sched_info_t containing tasks for that node
    TLOG_INFO("Generated schedules for ", node_sched_map.size(), " nodes:");
    for (const auto& entry : node_sched_map) {
        const std::string& node_id = entry.first;
        const sched_info_t& sched_info = entry.second;
        TLOG_INFO("  Node '", node_id, "': ", sched_info.num_tasks, " tasks");
    }

    // Store in our sched_info_map_ (copy the results)
    // sched_info_map_[workload_id] = NodeSchedInfoMap (node_id -> sched_info_t)
    sched_info_map_[request->workload_id()] = node_sched_map;

    TLOG_INFO("Successfully scheduled ", global_scheduler_->get_total_scheduled_tasks(),
            " tasks across ", node_sched_map.size(), " nodes");
    reply->set_status(0);  // Success
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
        task.workload_id = request->workload_id();  // Set workload_id from request
        task.policy = SchedPolicyToInt(static_cast<SchedPolicy>(grpc_task.policy()));
        task.priority = grpc_task.priority();
        task.cpu_affinity = grpc_task.cpu_affinity();
        task.period_us = grpc_task.period();
        task.runtime_us = grpc_task.runtime();
        task.deadline_us = grpc_task.deadline();
        task.release_time = grpc_task.release_time();
        task.max_dmiss = grpc_task.max_dmiss();
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

int SchedInfoServiceImpl::SchedPolicyToInt(SchedPolicy policy)
{
    switch (policy) {
        case SchedPolicy::NORMAL:
            return 0;
        case SchedPolicy::FIFO:
            return 1;
        case SchedPolicy::RR:
            return 2;
        default:
            return -1;
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
        const std::string& workload_id = entry.first;
        const auto& node_sched_info = entry.second;

        TLOG_INFO("Workload ID: ", workload_id, " with ", node_sched_info.size(), " nodes");

        for (const auto& node : node_sched_info) {
            const std::string& node_id = node.first;
            const sched_info_t& schedule_info = node.second;

            TLOG_INFO("Node ID: ", node_id, " with ", schedule_info.num_tasks, " tasks");

            for (int i = 0; i < schedule_info.num_tasks; i++) {
                const sched_task_t& task = schedule_info.tasks[i];
                TLOG_DEBUG("  Task Name: ", task.task_name);
                TLOG_DEBUG("    Assigned Node: ", task.assigned_node);
                TLOG_DEBUG("    CPU Affinity: ", task.cpu_affinity);
                TLOG_DEBUG("    Priority: ", task.sched_priority);
                TLOG_DEBUG("    Policy: ", task.sched_policy);
                TLOG_DEBUG("    Period: ", task.period_ns / 1000000, "ms");
                TLOG_DEBUG("    Runtime: ", task.runtime_ns / 1000000, "ms");
                TLOG_DEBUG("    Deadline: ", task.deadline_ns / 1000000, "ms");
            }
        }
    }
}
