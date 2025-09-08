#ifndef _INTERNAL_H
#define _INTERNAL_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#include <sched.h>
#include <time.h>
#include <getopt.h>
#include <sys/queue.h>
#include <errno.h>
#include <sys/time.h>

#include "timetrigger.h"
#include "schedinfo.h"
#include "libttsched.h"
#include "libtttrace.h"
#include <libtrpc.h>
#include "trace_bpf.h"

// 상수 정의
#define TIMER_INCREMENT_NS        (5 * 1000 * 1000)   // 5ms
#define POLLING_INTERVAL_US       (100 * 1000)        // 100ms
#define RETRY_INTERVAL_US         (1000 * 1000)       // 1s
#define MAX_CONNECTION_RETRIES    300
#define STATISTICS_LOG_INTERVAL   100

// 컴파일러 힌트 매크로
#ifndef likely
#define likely(x)   __builtin_expect(!!(x), 1)
#endif
#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

// 에러 코드
typedef enum {
    TT_SUCCESS = 0,
    TT_ERROR_MEMORY = -1,
    TT_ERROR_TIMER = -2,
    TT_ERROR_SIGNAL = -3,
    TT_ERROR_NETWORK = -4,
    TT_ERROR_CONFIG = -5,
    TT_ERROR_BPF = -6
} tt_error_t;

// 에러 메시지 함수
static inline const char* tt_error_string(tt_error_t error)
{
    switch (error) {
        case TT_SUCCESS: return "Success";
        case TT_ERROR_MEMORY: return "Memory allocation failed";
        case TT_ERROR_TIMER: return "Timer operation failed";
        case TT_ERROR_SIGNAL: return "Signal handling failed";
        case TT_ERROR_NETWORK: return "Network operation failed";
        case TT_ERROR_CONFIG: return "Configuration error";
        case TT_ERROR_BPF: return "BPF operation failed";
        default: return "Unknown error";
    }
}

// 성능 최적화 인라인 함수들
static inline uint64_t fast_ts_ns(const struct timespec *ts)
{
    return ((uint64_t)ts->tv_sec * NSEC_PER_SEC) + ts->tv_nsec;
}

static inline uint64_t fast_ts_us(const struct timespec *ts)
{
    return ((uint64_t)ts->tv_sec * USEC_PER_SEC) + (ts->tv_nsec / NSEC_PER_USEC);
}

static inline void fast_timespec_add_us(struct timespec *ts, uint64_t us)
{
    uint64_t total_ns = fast_ts_ns(ts) + (us * NSEC_PER_USEC);
    ts->tv_sec = total_ns / NSEC_PER_SEC;
    ts->tv_nsec = total_ns % NSEC_PER_SEC;
}

static inline int fast_timespec_cmp(const struct timespec *a, const struct timespec *b)
{
    if (a->tv_sec != b->tv_sec) {
        return (a->tv_sec > b->tv_sec) ? 1 : -1;
    }
    return (a->tv_nsec > b->tv_nsec) ? 1 : ((a->tv_nsec < b->tv_nsec) ? -1 : 0);
}

// 시그널 정의
#define SIGNO_TT            __SIGRTMIN+2
#define SIGNO_STOPTRACER    __SIGRTMIN+3

// Forward declaration
struct context;

// Time trigger 구조체
struct time_trigger {
    timer_t timer;
    struct task_info task;
#ifdef CONFIG_TRACE_BPF
    uint64_t sigwait_ts;
    uint64_t sigwait_ts_prev;
    uint8_t sigwait_enter;
#endif
    struct timespec prev_timer;
    struct context *ctx;  // context 포인터 추가
    LIST_ENTRY(time_trigger) entry;
};

// Hyperperiod 관리 구조체 (메모리 정렬 최적화)
struct hyperperiod_manager {
    // 자주 접근하는 필드들을 앞으로
    uint64_t hyperperiod_us;
    uint64_t current_cycle;
    uint64_t hyperperiod_start_time_us;
    uint64_t completed_cycles;

    // 포인터들
    struct time_trigger *tt_list;
    struct context *ctx;

    // 타이머 관련
    timer_t hyperperiod_timer;
    struct timespec hyperperiod_start_ts;

    // 통계 (32비트)
    uint32_t tasks_in_hyperperiod;
    uint32_t total_deadline_misses;
    uint32_t cycle_deadline_misses;
    uint32_t _padding;  // 8바이트 정렬을 위한 패딩

    // 문자열 (마지막에 배치)
    char workload_id[64];
} __attribute__((packed, aligned(8)));

LIST_HEAD(listhead, time_trigger);

// 통합 컨텍스트 구조체
struct context {
    // 설정
    struct {
        int cpu;
        int prio;
        int port;
        const char *addr;
        char node_id[TINFO_NODEID_MAX];
        bool enable_sync;
        bool enable_plot;
        clockid_t clockid;
        int traceduration;
    } config;

    // 런타임 상태
    struct {
        struct listhead tt_list;
        struct sched_info sched_info;
        volatile sig_atomic_t shutdown_requested;
        struct timespec starttimer_ts;
    } runtime;

    // 통신
    struct {
        sd_event *event;
        sd_bus *dbus;
    } comm;

    // 하이퍼피리어드 관리
    struct hyperperiod_manager hp_manager;
};

// 각 모듈의 함수 선언들
// config.c
tt_error_t config_parse(int argc, char *argv[], struct context *ctx);
tt_error_t config_validate(const struct context *ctx);

// core.c
void timer_handler(union sigval value);
tt_error_t start_timers(struct context *ctx);
tt_error_t epoll_loop(struct context *ctx);
int sigwait_bpf_callback(void *ctx, void *data, size_t len);
int schedstat_bpf_callback(void *ctx, void *data, size_t len);

// hyperperiod.c
tt_error_t hyperperiod_init(struct hyperperiod_manager *hp_mgr, const char *workload_id, uint64_t hyperperiod_us, struct context *ctx);
void hyperperiod_cycle_handler(union sigval value);
uint64_t hyperperiod_get_relative_time_us(const struct hyperperiod_manager *hp_mgr);
void hyperperiod_log_statistics(const struct hyperperiod_manager *hp_mgr);
tt_error_t hyperperiod_start_timer(struct context *ctx);

// task.c
tt_error_t task_list_init(struct context *ctx);
void free_task_list(struct task_info *tasks);

// trpc.c
tt_error_t trpc_init(struct context *ctx);
tt_error_t trpc_sync_timer(struct context *ctx);
int deserialize_schedinfo(serial_buf_t *sbuf, struct sched_info *sinfo, struct context *ctx);
int report_dmiss(sd_bus *dbus, char *node_id, const char *taskname);

// signal.c
tt_error_t signal_setup(struct context *ctx);

// cleanup.c
void cleanup_all(struct context *ctx);

// 유틸리티 함수들
void calibrate_bpf_ktime_offset(void);
bool set_stoptracer_timer(struct context *ctx, int duration, timer_t *timer);

#endif /* _INTERNAL_H */
