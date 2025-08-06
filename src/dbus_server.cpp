#include "tlog.h"
#include "dbus_server.h"

DBusServer::DBusServer()
        : event_source_(nullptr),
          event_(nullptr),
          server_fd_(-1),
          running_(false)
{
}

DBusServer::~DBusServer()
{
    Cleanup();
}

bool DBusServer::Start(int port)
{
    if (running_.load()) {
        TLOG_WARN("DBusServer is already running");
        return false;
    }

    static trpc_server_ops_t trpc_ops = {
        .register_cb = RegisterCallback,
        .schedinfo_cb = SchedInfoCallback,
        .dmiss_cb = DMissCallback,
        .sync_cb = SyncCallback
    };
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
        Cleanup();
        return false;
    }
    server_fd_ = ret;

    running_.store(true);
    // Start the event loop in a separate thread
    event_thread_ = std::thread(&DBusServer::EventLoop, this);
    if (!event_thread_.joinable()) {
        TLOG_ERROR("DBusServer thread failed to start");
        Cleanup();
        return false;
    }

    return true;
}

void DBusServer::Stop()
{
    if (!running_.load()) {
        return;
    }
    running_.store(false);

    if (event_thread_.joinable()) {
        event_thread_.join();
    }
}

void DBusServer::Cleanup()
{
    if (running_.load()) {
        Stop();
    }

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

void DBusServer::RegisterCallback(const char *name)
{
    TLOG_INFO("RegisterCallback with name: ", name);
}

void DBusServer::SchedInfoCallback(const char *name, void **buf,
                                   size_t *bufsize)
{
    TLOG_INFO("SchedInfoCallback with name: ", name);

    if (buf && bufsize) {
        *buf = nullptr;  // No actual data to return
        *bufsize = 0;
    }
}

void DBusServer::DMissCallback(const char *name, const char *task)
{
    TLOG_INFO("DMissCallback with name: ", name, ", task: ", task);
}

void DBusServer::SyncCallback(const char *name, int *ack,
                              struct timespec *ts)
{
    TLOG_INFO("SyncCallback with name: ", name);
    if (ack) {
        *ack = 0;  // Acknowledge the sync
    }
}
