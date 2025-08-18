#include "tlog.h"
#include "dbus_server.h"

constexpr int kNsToMs = 1000000;

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

    if (sched_info_buf_) {
        free_serial_buf(sched_info_buf_);
        sched_info_buf_ = nullptr;
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
    // Return true if buffer is already allocated
    if (sched_info_buf_) return true;

    // FIXME: Currently only serialize the first workload in schedule info
    auto sched_info = map.begin()->second;
    if (sched_info.num_tasks == 0) {
        TLOG_WARN("No tasks in schedule info");
        return false;
    }

    // Allocate serial buffer and pack schedule info into it
    sched_info_buf_ = new_serial_buf(1024);
    if (!sched_info_buf_) {
        TLOG_ERROR("Failed to allocate memory for schedule info buffer");
        return false;
    }

#if 1  // with Timpani-N v2.0
    for (int i = 0; i < sched_info.num_tasks; i++) {
        const sched_task_t& task = sched_info.tasks[i];
        std::string task_name = task.task_name;
        serialize_str(sched_info_buf_,
                      task_name.substr(0, 16 - 1).c_str());
        serialize_int32_t(sched_info_buf_, task.sched_priority);
        serialize_int32_t(sched_info_buf_, task.sched_policy);
        serialize_int32_t(sched_info_buf_, task.period_ns / kNsToMs);
        serialize_int32_t(sched_info_buf_, task.runtime_ns / kNsToMs);
        serialize_int32_t(sched_info_buf_, task.deadline_ns / kNsToMs);
        serialize_int32_t(sched_info_buf_, task.release_time);
        serialize_int64_t(sched_info_buf_, task.cpu_affinity);
        serialize_int32_t(sched_info_buf_, task.max_dmiss);
        std::string task_assigned_node = task.assigned_node;
        serialize_str(sched_info_buf_,
                      task_assigned_node.substr(0, 64 - 1).c_str());
    }
    serialize_int32_t(sched_info_buf_, sched_info.num_tasks);  // nr_tasks
#else // with Timpani-N v1.0
    for (int i = 0; i < sched_info.num_tasks; i++) {
        const sched_task_t& task = sched_info.tasks[i];
        std::string task_name = task.task_name;
        serialize_str(sched_info_buf_,
                      task_name.substr(0, 16 - 1).c_str());
        serialize_int32_t(sched_info_buf_, task.sched_priority);
        serialize_int32_t(sched_info_buf_, task.sched_policy);
        serialize_int32_t(sched_info_buf_, task.period_ns / kNsToMs);  // Convert to ms
        serialize_int32_t(sched_info_buf_, task.release_time);  // release_time
        serialize_int32_t(sched_info_buf_, task.max_dmiss);  // max_dmiss
        // FIXME: introduce string-type node_id on Timpani-N
        serialize_int32_t(sched_info_buf_, task.cpu_affinity);  // Use CPU affinity as node ID for now
    }

    // Pack extra scheduling info into serial buffer
    // FIXME: remove unused items
    char container_id[64];
    memset(container_id, '0', sizeof(container_id));
    serialize_blob(sched_info_buf_, container_id, sizeof(container_id));
    serialize_int32_t(sched_info_buf_, 0);             // container_rt_runtime
    serialize_int32_t(sched_info_buf_, 0);             // container_rt_period
    serialize_int64_t(sched_info_buf_, 0);             // cpumask
    serialize_int32_t(sched_info_buf_, 0);             // container_period
    serialize_int32_t(sched_info_buf_, 0);             // pod_period
    serialize_int32_t(sched_info_buf_, sched_info.num_tasks);  // nr_tasks
#endif

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
            *buf = instance.sched_info_buf_->data;
            *bufsize = instance.sched_info_buf_->pos;
            return;
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
}

void DBusServer::SyncCallback(const char* name, int* ack, struct timespec* ts)
{
    TLOG_INFO("SyncCallback with name: ", name);
    if (ack) {
        *ack = 0;  // Acknowledge the sync
    }
}
