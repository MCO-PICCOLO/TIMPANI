#include "internal.h"

// 임시 전역 변수들 (점진적으로 제거 예정)
struct hyperperiod_manager hp_manager;
sd_bus *trpc_dbus = NULL;
char node_id[TINFO_NODEID_MAX] = "1";
clockid_t clockid = CLOCK_REALTIME;
struct timespec starttimer_ts;
