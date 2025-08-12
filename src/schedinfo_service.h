#ifndef SCHEDINFO_SERVICE_H
#define SCHEDINFO_SERVICE_H

#include <map>
#include <memory>
#include <shared_mutex>
#include <thread>
#include <grpcpp/grpcpp.h>

#include "proto/schedinfo.grpc.pb.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using schedinfo::v1::Response;
using schedinfo::v1::SchedInfo;
using schedinfo::v1::SchedInfoService;
using schedinfo::v1::SchedPolicy;
using schedinfo::v1::TaskInfo;

using SchedInfoMap = std::map<std::string, std::vector<TaskInfo>>;

/**
* @brief Implementation of the SchedInfoService gRPC service
*
* This service handles scheduling information deliveries from Piccolo to Timpani-O.
* It processes SchedInfo messages and returns a Response indicating success or failure.
*/
class SchedInfoServiceImpl final : public SchedInfoService::Service
{
  public:
    SchedInfoServiceImpl();

    Status AddSchedInfo(ServerContext* context, const SchedInfo* request,
                        Response* reply) override;

    SchedInfoMap GetSchedInfoMap() const;

  private:
    static const char* SchedPolicyToStr(SchedPolicy policy);

    // Member variable to store scheduling information
    SchedInfoMap sched_info_map_;
    // Use shared_mutex for sched_info_map_
    mutable std::shared_mutex sched_info_mutex_;
};

/**
 * @brief SchedInfoServer class for managing the SchedInfoService gRPC server
 *
 * This class encapsulates the SchedInfoService gRPC server functionality,
 * allowing it to start and stop the service, and handle incoming requests.
 */
class SchedInfoServer
{
  public:
    SchedInfoServer();
    ~SchedInfoServer();
    bool Start(int port);
    void Stop();
    SchedInfoMap GetSchedInfoMap() const;
    void DumpSchedInfo();

 private:
    SchedInfoServiceImpl service_;
    std::unique_ptr<Server> server_;
    std::unique_ptr<std::thread> server_thread_;
};

#endif  // SCHEDINFO_SERVICE_H
