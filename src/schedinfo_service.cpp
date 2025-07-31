#include <iostream>

#include "schedinfo_service.h"

SchedInfoServiceImpl::SchedInfoServiceImpl()
{
}

Status SchedInfoServiceImpl::AddSchedInfo(ServerContext *context,
                                          const SchedInfo *request,
                                          Response *reply)
{
    std::cout << "Timpani: Received SchedInfo for workload: "
              << request->workload_id() << std::endl;
    std::cout << "Timpani: Number of tasks: " << request->tasks_size()
              << std::endl;

    // Print task information
    std::cout << "\n-- Tasks --" << std::endl;
    for (int i = 0; i < request->tasks_size(); i++) {
        const auto &task = request->tasks(i);
        std::cout << "Task: " << task.name() << std::endl;
        std::cout << "  Priority: " << task.priority() << std::endl;

        std::cout << "  Policy: ";
        switch (task.policy()) {
            case SchedPolicy::NORMAL:
                std::cout << "NORMAL";
                break;
            case SchedPolicy::FIFO:
                std::cout << "FIFO";
                break;
            case SchedPolicy::RR:
                std::cout << "RR";
                break;
            default:
                std::cout << "UNKNOWN";
                break;
        }
        std::cout << std::endl;

        std::cout << "  CPU Affinity: 0x" << std::hex << task.cpu_affinity()
                  << std::dec << std::endl;
        std::cout << "  Period: " << task.period() << std::endl;
        std::cout << "  Release Time: " << task.release_time() << std::endl;
        std::cout << "  Runtime: " << task.runtime() << std::endl;
        std::cout << "  Deadline: " << task.deadline() << std::endl;
        std::cout << "  Max Deadline Misses: " << task.max_dmiss() << std::endl;
        std::cout << "  Node ID: " << task.node_id() << std::endl;
        std::cout << std::endl;
    }

    std::cout << "===========================" << std::endl;

    reply->set_status(0);  // Success
    return Status::OK;
}
