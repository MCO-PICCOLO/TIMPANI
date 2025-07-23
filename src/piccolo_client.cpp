#include <iostream>
#include <memory>
#include <string>

#include "piccolo_client.h"

PiccoloClient::PiccoloClient(std::string &addr, int port)
{
    grpc::string target = addr + ":" + std::to_string(port);
    std::shared_ptr<Channel> channel =
        grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
    stub_ = TimpaniConnection::NewStub(channel);
}

SchedInfo PiccoloClient::GetSchedInfo()
{
    Empty request;
    SchedInfo reply;
    ClientContext context;

    Status status = stub_->GetSchedInfo(&context, request, &reply);

    if (status.ok())
    {
        return reply;
    }
    else
    {
        std::cerr << "GetSchedInfo failed: " << status.error_code() << ": " << status.error_message() << std::endl;
        return SchedInfo();
    }
}

void PiccoloClient::NotifyDMiss(const std::string &node_id, const std::string &task_name)
{
    FaultInfo request;
    request.set_node_id(node_id);
    request.set_task_name(task_name);
    request.set_type(FaultType::DMISS);
    Empty reply;
    ClientContext context;

    Status status = stub_->NotifyFault(&context, request, &reply);

    if (!status.ok())
    {
        std::cerr << "NotifyFault failed: " << status.error_code() << ": " << status.error_message() << std::endl;
    }
}

void PiccoloClient::PrintSchedInfo(const SchedInfo &info)
{
    std::cout << "=== Scheduling Information ===" << std::endl;

    // Print node information
    std::cout << "\n-- Nodes --" << std::endl;
    for (int i = 0; i < info.nodes_size(); i++)
    {
        const auto &node = info.nodes(i);
        std::cout << "Node ID: " << node.id() << std::endl;
    }

    // Print task information
    std::cout << "\n-- Tasks --" << std::endl;
    for (int i = 0; i < info.tasks_size(); i++)
    {
        const auto &task = info.tasks(i);
        std::cout << "Task: " << task.name() << std::endl;
        std::cout << "  Priority: " << task.priority() << std::endl;

        std::cout << "  Policy: ";
        switch (task.policy())
        {
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

        std::cout << "  CPU Affinity: 0x" << std::hex << task.cpu_affinity() << std::dec << std::endl;
        std::cout << "  Period: " << task.period() << std::endl;
        std::cout << "  Release Time: " << task.release_time() << std::endl;
        std::cout << "  Runtime: " << task.runtime() << std::endl;
        std::cout << "  Deadline: " << task.deadline() << std::endl;
        std::cout << "  Max Deadline Misses: " << task.max_dmiss() << std::endl;
        std::cout << "  Node ID: " << task.node_id() << std::endl;
        std::cout << std::endl;
    }

    std::cout << "===========================" << std::endl;
}
