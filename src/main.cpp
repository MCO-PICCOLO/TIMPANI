#include <iostream>
#include <string>

#include "piccolo_client.h"

static int GetSchedInfo(std::string addr, int port)
{
    PiccoloClient client(addr, port);

    SchedInfo sched_info = client.GetSchedInfo();
    client.PrintSchedInfo(sched_info);

    return 0;
}

int main(int argc, char **argv)
{
    GetSchedInfo("localhost", 50051);

    return 0;
}
