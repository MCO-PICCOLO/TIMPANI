#include <iostream>
#include <string>
#include <memory>

#include "tlog.h"
#include "schedinfo_service.h"

void RunSchedInfoServer(int port)
{
    std::string server_address = "0.0.0.0";
    server_address += ":" + std::to_string(port);

    SchedInfoServiceImpl service;

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<Server> server(builder.BuildAndStart());
    TLOG_INFO("Timpani-O SchedInfoService listening on ", server_address);
    server->Wait();
}

int main(int argc, char **argv)
{
    // Initialize the logger
    TLOG_SET_LOG_LEVEL(LogLevel::DEBUG);
    TLOG_SET_PRINT_FILENAME(false);
    TLOG_SET_FULL_TIMESTAMP(false);

    // Run the server for SchedInfoService (blocking)
    RunSchedInfoServer(50052);

    return 0;
}
