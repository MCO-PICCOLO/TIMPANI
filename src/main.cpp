#include <iostream>
#include <string>
#include <memory>

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
    std::cout << "Timpani-O SchedInfoService listening on " << server_address << std::endl;
    server->Wait();
}

int main(int argc, char **argv)
{
    // Run the server for SchedInfoService (blocking)
    RunSchedInfoServer(50052);

    return 0;
}
