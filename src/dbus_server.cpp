#include "tlog.h"
#include "dbus_server.h"
#include "fault_client.h"

constexpr int kNsToUs = 1000;

DBusServer& DBusServer::GetInstance()
{
    static DBusServer instance;
    return instance;
}

DBusServer::DBusServer()
    : event_source_(nullptr),
      event_(nullptr),
      event_thread_(nullptr),
      server_fd_(-1),
      running_(false),
      sched_info_server_(nullptr),
      sched_info_buf_(nullptr)
{
}

DBusServer::~DBusServer() { Stop(); }

bool DBusServer::Start(int port, SchedInfoServer* sinfo_server)
{
    if (running_.load()) {
        TLOG_WARN("DBusServer is already running");
        return false;
    }

    static trpc_server_ops_t trpc_ops = {.register_cb = RegisterCallback,
                                         .schedinfo_cb = SchedInfoCallback,
                                         .dmiss_cb = DMissCallback,
                                         .sync_cb = SyncCallback};
    int ret;

    // Do not use sd_event_default() here.
    // Use sd_event_new() for running event loop in a separate thread.
    ret = sd_event_new(&event_);
    if (ret < 0) {
        TLOG_ERROR("sd_event_new failed: ", strerror(-ret));
        return false;
    }

    ret = trpc_server_create(port, event_, &event_source_, &trpc_ops);
    if (ret < 0) {
        TLOG_ERROR("trpc_server_create failed: ", strerror(-ret));
        Stop();
        return false;
    }
    server_fd_ = ret;

    SetSchedInfoServer(sinfo_server);

    // Start the event loop in a separate thread
    event_thread_ = std::make_unique<std::thread>(&DBusServer::EventLoop, this);
    if (!event_thread_->joinable()) {
        TLOG_ERROR("DBusServer thread failed to start");
        Stop();
        return false;
    }
    running_.store(true);

    return true;
}

void DBusServer::Stop()
{
    if (running_.load()) {
        running_.store(false);

        if (event_thread_ && event_thread_->joinable()) {
            event_thread_->join();
        }
    }

    {
        std::lock_guard<std::mutex> lock(sched_info_buf_mutex_);
        if (sched_info_buf_) {
            free_serial_buf(sched_info_buf_);
            sched_info_buf_ = nullptr;
        }
    }

    SetSchedInfoServer(nullptr);

    if (event_source_) {
        event_source_ = sd_event_source_unref(event_source_);
    }
    if (event_) {
        event_ = sd_event_unref(event_);
    }
    if (server_fd_ >= 0) {
        close(server_fd_);
        server_fd_ = -1;
    }
}

void DBusServer::SetSchedInfoServer(SchedInfoServer* server)
{
    sched_info_server_ = server;
}

void DBusServer::EventLoop()
{
    while (running_.load()) {
        int ret = sd_event_run(event_, 100000);  // 100ms timeout
        if (ret < 0 && ret != -EAGAIN && ret != -EINTR) {
            TLOG_ERROR("sd_event_run failed: ", strerror(-ret));
            break;
        }
    }
}

bool DBusServer::SerializeSchedInfo(const SchedInfoMap& map)
{
    std::lock_guard<std::mutex> lock(sched_info_buf_mutex_);

    // Return true if buffer is already allocated
    if (sched_info_buf_) return true;

    // FIXME: Currently only serialize the first workload in schedule info
    const auto& node_sched_info = map.begin()->second;

    // Allocate serial buffer and pack schedule info into it
    sched_info_buf_ = new_serial_buf(1024);
    if (!sched_info_buf_) {
        TLOG_ERROR("Failed to allocate memory for schedule info buffer");
        return false;
    }

    int nr_tasks = 0;
    for (const auto& sinfo : node_sched_info) {
        // Serialize each node's schedule info
        const std::string& node_id = sinfo.first;
        const sched_info_t& sched_info = sinfo.second;

        // NOTE: This buffer format only works with Timpani-N v2.0
        for (int i = 0; i < sched_info.num_tasks; i++) {
            const sched_task_t& task = sched_info.tasks[i];
            std::string task_name = task.task_name;
            serialize_str(sched_info_buf_,
                          task_name.substr(0, 16 - 1).c_str());
            serialize_int32_t(sched_info_buf_, task.sched_priority);
            serialize_int32_t(sched_info_buf_, task.sched_policy);
            serialize_int32_t(sched_info_buf_, task.period_ns / kNsToUs);
            serialize_int32_t(sched_info_buf_, task.runtime_ns / kNsToUs);
            serialize_int32_t(sched_info_buf_, task.deadline_ns / kNsToUs);
            serialize_int32_t(sched_info_buf_, task.release_time / kNsToUs);
            serialize_int64_t(sched_info_buf_, task.cpu_affinity);
            serialize_int32_t(sched_info_buf_, task.max_dmiss);
            std::string task_assigned_node = task.assigned_node;
            serialize_str(sched_info_buf_,
                        task_assigned_node.substr(0, 64 - 1).c_str());
        }
        nr_tasks += sched_info.num_tasks;
    }
    serialize_int32_t(sched_info_buf_, nr_tasks);

    TLOG_DEBUG("Serialized sched_info_buf_: ", sched_info_buf_->pos, " bytes");

    return true;
}

void DBusServer::RegisterCallback(const char* name)
{
    // FIXME: Currently not being used by Timpani-N
    TLOG_INFO("RegisterCallback with name: ", name);
}

void DBusServer::SchedInfoCallback(const char* name, void** buf,
                                   size_t* bufsize)
{
    TLOG_INFO("SchedInfoCallback with name: ", name);

    DBusServer& instance = GetInstance();
    if (instance.sched_info_server_) {
        auto sched_info_map = instance.sched_info_server_->GetSchedInfoMap();
        if (!sched_info_map.empty() &&
            instance.SerializeSchedInfo(sched_info_map)) {
            std::lock_guard<std::mutex> lock(instance.sched_info_buf_mutex_);
            if (instance.sched_info_buf_) {
                *buf = instance.sched_info_buf_->data;
                *bufsize = instance.sched_info_buf_->pos;
                return;
            }
        }
    }

    TLOG_WARN("No schedule info available");
    if (buf && bufsize) {
        *buf = nullptr;  // No actual data to return
        *bufsize = 0;
    }
}

void DBusServer::DMissCallback(const char* name, const char* task)
{
    TLOG_INFO("DMissCallback with name: ", name, ", task: ", task);

    std::string workload_id;

    DBusServer& instance = GetInstance();
    if (instance.sched_info_server_) {
        auto map = instance.sched_info_server_->GetSchedInfoMap();
        if (map.empty()) {
            TLOG_WARN("No schedule info available for DMissCallback");
            workload_id.clear();
        } else {
            // FIXME: Currently only references the first workload
            workload_id = map.begin()->first;
        }
    }

    FaultServiceClient& client = FaultServiceClient::GetInstance();

    if (!client.NotifyFault(workload_id, name, task, FaultType::DMISS)) {
        TLOG_WARN("NotifyFault failed for ", workload_id,
                  " on node ", name, " for task ", task);
    }
}

void DBusServer::SyncCallback(const char* name, int* ack, struct timespec* ts)
{
    TLOG_INFO("SyncCallback with name: ", name);

    DBusServer& instance = GetInstance();

    // If node sync map is empty, initialize it based on schedule info
    // FIXME: This should be done per workload
    if (instance.node_sync_map_.empty()) {
        if (instance.sched_info_server_) {
            auto map = instance.sched_info_server_->GetSchedInfoMap();
            // FIXME: Currently only initializes the first workload
            const auto& node_sched_info = map.begin()->second;
            for (const auto& sinfo : node_sched_info) {
                const std::string& node_id = sinfo.first;
                instance.node_sync_map_[node_id] = false;
            }
            TLOG_DEBUG("Created node sync map with ",
                       instance.node_sync_map_.size(), " entries",
                       " for ", map.begin()->first);
        }
    }

    // Update sync status for the node
    auto it = instance.node_sync_map_.find(name);
    if (it == instance.node_sync_map_.end()) {
        TLOG_WARN("Not found in node sync map: ", name);
        if (ack) {
            *ack = 0;
        }
        return;
    }
    it->second = true;  // Mark the node as ready

    // Check if all nodes are ready
    bool all_ready = true;
    for (const auto& [node, ready] : instance.node_sync_map_) {
        if (!ready) {
            all_ready = false;
            break;
        }
    }

    if (all_ready) {
        TLOG_DEBUG("SyncCallback acked: ", name);
        if (ack) {
            *ack = 1;  // Acknowledge synchronization
        }
        if (ts) {
            // Set the sync timestamp to current time + 1 second
            clock_gettime(CLOCK_REALTIME, ts);
            ts->tv_sec += 1;
        }
    } else {
        if (ack) {
            *ack = 0;  // Not all nodes are ready
        }
    }
}
