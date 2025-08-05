#include <iostream>
#include <string>
#include <memory>
#include <thread>
#include <chrono>
#include <getopt.h>

#include "tlog.h"
#include "schedinfo_service.h"
#include "fault_client.h"

bool RunSchedInfoServer(int port, std::unique_ptr<Server>& server,
                        std::unique_ptr<std::thread>& server_thread)
{
    std::string server_addr = "0.0.0.0";
    server_addr += ":" + std::to_string(port);

    static SchedInfoServiceImpl service;

    ServerBuilder builder;
    builder.AddListeningPort(server_addr, grpc::InsecureServerCredentials());
    builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);
    builder.RegisterService(&service);

    server = builder.BuildAndStart();
    if (!server) {
        TLOG_ERROR("Failed to start SchedInfoService on ", server_addr);
        return false;
    }
    TLOG_INFO("SchedInfoService listening on ", server_addr);

    // Start the server in a separate thread
    server_thread = std::make_unique<std::thread>([&server]() {
        server->Wait();
    });
    server_thread->detach();

    return true;
}

bool InitFaultClient(const std::string& addr, int port)
{
    std::string piccolo_addr = addr + ":" + std::to_string(port);

    FaultServiceClient& client = FaultServiceClient::GetInstance();
    return client.Initialize(piccolo_addr);
}

bool NotifyFaultDemo()
{
    FaultServiceClient& client = FaultServiceClient::GetInstance();

    return client.NotifyFault("workload_demo", "node_demo", "task_demo", FaultType::DMISS);
}

bool GetOptions(int argc, char *argv[], int& sinfo_port,
                std::string& fault_addr, int& fault_port, bool& notify_fault)
{
    const char *short_opts = "hs:a:p:n";
    const struct option long_opts[] = {
        {"help", no_argument, nullptr, 'h'},
        {"sinfoport", required_argument, nullptr, 's'},
        {"faulthost", required_argument, nullptr, 'f'},
        {"faultport", required_argument, nullptr, 'p'},
        {"notifyfault", no_argument, nullptr, 'n'},
        {nullptr, 0, nullptr, 0}
    };
    int opt;

    while ((opt = getopt_long(argc, argv, short_opts, long_opts, nullptr)) >= 0) {
		switch (opt) {
		    case 's':
			    sinfo_port = std::stoi(optarg);
			break;
		    case 'f':
			    fault_addr = optarg;
			    break;
		    case 'p':
			    fault_port = std::stoi(optarg);
			    break;
            case 'n':
                // FIXME: NotifyFault option for testing
                notify_fault = true;
                break;
		    case 'h':
		    default:
                std::cerr << "Usage: " << argv[0] << " [options] [host]\n"
                          << "Options:\n"
                          << "  -s <port>\tPort for SchedInfoService (default: 50052)\n"
                          << "  -f <address>\tFaultService host address (default: localhost)\n"
                          << "  -p <port>\tPort for FaultService (default: 50053)\n"
                          << "  -n\t\tEnable NotifyFault demo (default: false)\n"
                          << "  -h\t\tShow this help message\n";
                std::cerr << "Example: " << argv[0] << " -s 50052 -f localhost -p 50053\n";
                return false;
		}
	}

	return true;
}

int main(int argc, char **argv)
{
    int sinfo_port = 50052;
    std::string fault_addr = "localhost";
    int fault_port = 50053;
    bool notify_fault = false; // Flag for NotifyFault method demo

    if (!GetOptions(argc, argv, sinfo_port, fault_addr, fault_port, notify_fault)) {
        exit(EXIT_FAILURE);
    }

    // Initialize the logger
    TLOG_SET_LOG_LEVEL(LogLevel::DEBUG);
    TLOG_SET_PRINT_FILENAME(false);
    TLOG_SET_FULL_TIMESTAMP(false);

    // Initialize and run the gRPC SchedInfoService server
    std::unique_ptr<Server> sinfo_server;
    std::unique_ptr<std::thread> sinfo_server_thread;
    if (!RunSchedInfoServer(sinfo_port, sinfo_server, sinfo_server_thread)) {
        exit(EXIT_FAILURE);
    }

    // Initialize the gRPC FaultServiceClient
    if (!InitFaultClient(fault_addr, fault_port)) {
        exit(EXIT_FAILURE);
    }

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        if (notify_fault) {
            if (NotifyFaultDemo()) {
                notify_fault = false; // Reset the flag after notification
            }
        }
    }

    // Cleanly shutdown SchedInfoService and join thread
    sinfo_server->Shutdown();
    sinfo_server_thread->join();

    exit(EXIT_SUCCESS);
}
