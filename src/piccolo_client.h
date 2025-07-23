#ifndef PICCOLO_CLIENT_H

#include <string>
#include <grpcpp/grpcpp.h>

#include "proto/schedinfo.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

using com::lge::timpani::v1::Empty;
using com::lge::timpani::v1::FaultInfo;
using com::lge::timpani::v1::FaultType;
using com::lge::timpani::v1::SchedInfo;
using com::lge::timpani::v1::SchedPolicy;
using com::lge::timpani::v1::TimpaniConnection;

class PiccoloClient
{
  public:
    PiccoloClient(std::string &addr, int port);

    SchedInfo GetSchedInfo();
    void NotifyDMiss(const std::string &node_id, const std::string &task_name);
    void PrintSchedInfo(const SchedInfo &info);

  private:
    std::unique_ptr<TimpaniConnection::Stub> stub_;
};

#endif // PICCOLO_CLIENT_H
