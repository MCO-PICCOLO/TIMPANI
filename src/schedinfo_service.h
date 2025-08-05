#ifndef SCHEDINFO_SERVICE_H
#define SCHEDINFO_SERVICE_H

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

class SchedInfoServiceImpl final : public SchedInfoService::Service
{
  public:
    SchedInfoServiceImpl();

    Status AddSchedInfo(ServerContext *context, const SchedInfo *request,
                        Response *reply) override;

  private:
    static const char* SchedPolicyToStr(SchedPolicy policy);
};

#endif  // SCHEDINFO_SERVICE_H
