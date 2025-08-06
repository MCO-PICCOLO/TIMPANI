#ifndef DBUS_SERVER_H
#define DBUS_SERVER_H

#include <thread>
#include <atomic>

#include <libtrpc.h>

/**
 * @brief DBusServer class for D-Bus communication with Timpani-N
 *
 * This class manages the D-Bus server, event loop, and callbacks for
 * scheduling information, deadline misses, and multi-node synchronization.
 * It uses libtrpc for remote procedure calls over D-Bus.
 */
class DBusServer
{
    public:
        DBusServer();
        ~DBusServer();

        bool Start(int port);
        void Stop();
        void Cleanup();
        void EventLoop();

        static void RegisterCallback(const char *name);
        static void SchedInfoCallback(const char *name, void **buf,
                                      size_t *bufsize);
        static void DMissCallback(const char *name, const char *task);
        static void SyncCallback(const char *name, int *ack,
                                 struct timespec *ts);

    private:
        sd_event_source *event_source_;
        sd_event *event_;
        std::thread event_thread_;
        int server_fd_;
        std::atomic<bool> running_;
};

#endif // DBUS_SERVER_H