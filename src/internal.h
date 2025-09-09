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

// ===== TT 시스템 상수 정의 =====
// Time Trigger 시스템에서 사용하는 모든 상수들을 TT_ 네임스페이스로 관리

// 타이머 관련 상수
#define TT_TIMER_INCREMENT_NS        (5 * 1000 * 1000)   // 5ms - 타이머 정밀도 조정 값

// 네트워크 통신 상수
#define TT_POLLING_INTERVAL_US       (100 * 1000)        // 100ms - 폴링 간격
#define TT_RETRY_INTERVAL_US         (1000 * 1000)       // 1s - 재시도 간격
#define TT_MAX_CONNECTION_RETRIES    300                 // 최대 연결 재시도 횟수

// 로깅 및 통계 상수
#define TT_STATISTICS_LOG_INTERVAL   100                 // 통계 로그 출력 주기 (하이퍼피리어드 사이클 기준)

// 에러 로깅 매크로
#define TT_LOG_ERROR(fmt, ...) \
    fprintf(stderr, "[ERROR] %s:%d: " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)

#define TT_LOG_WARNING(fmt, ...) \
    fprintf(stderr, "[WARNING] %s:%d: " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)

#define TT_CHECK_ERROR(expr, error_code, fmt, ...) \
    do { \
        if (unlikely(!(expr))) { \
            TT_LOG_ERROR(fmt, ##__VA_ARGS__); \
            return error_code; \
        } \
    } while(0)

// 메모리 관리 매크로
#define TT_MALLOC(ptr, type) \
    do { \
        (ptr) = malloc(sizeof(type)); \
        if (unlikely(!(ptr))) { \
            TT_LOG_ERROR("Failed to allocate memory for " #type); \
            return TT_ERROR_MEMORY; \
        } \
        memset((ptr), 0, sizeof(type)); \
    } while(0)

#define TT_CALLOC(ptr, count, type) \
    do { \
        (ptr) = calloc((count), sizeof(type)); \
        if (unlikely(!(ptr))) { \
            TT_LOG_ERROR("Failed to allocate memory for %zu " #type " items", (size_t)(count)); \
            return TT_ERROR_MEMORY; \
        } \
    } while(0)

#define TT_FREE(ptr) \
    do { \
        if (likely((ptr))) { \
            free((ptr)); \
            (ptr) = NULL; \
        } \
    } while(0)

#define TT_SAFE_FREE(ptr) \
    do { \
        free((ptr)); \
        (ptr) = NULL; \
    } while(0)

// 컴파일러 힌트 매크로
#ifndef likely
#define likely(x)   __builtin_expect(!!(x), 1)
#endif
#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

// ===== TT 에러 코드 시스템 =====
// 모든 함수는 통일된 tt_error_t 타입을 반환하여 일관된 에러 처리 제공
typedef enum {
    TT_SUCCESS = 0,              // 성공
    TT_ERROR_MEMORY = -1,        // 메모리 할당 실패
    TT_ERROR_TIMER = -2,         // 타이머 관련 오류
    TT_ERROR_SIGNAL = -3,        // 시그널 처리 오류
    TT_ERROR_NETWORK = -4,       // 네트워크 통신 오류
    TT_ERROR_CONFIG = -5,        // 설정 관련 오류
    TT_ERROR_BPF = -6           // BPF 프로그램 오류
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

// 시간 처리 유틸리티 함수 (timetrigger.h의 통합 API 사용)
static inline void tt_timespec_add_us(struct timespec *ts, uint64_t us)
{
    uint64_t total_ns = tt_timespec_to_ns(ts) + (us * TT_NSEC_PER_USEC);
    *ts = tt_ns_to_timespec(total_ns);
}

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

// ===== TT 시스템 컨텍스트 구조체 =====
// 전역 변수를 대체하는 중앙화된 컨텍스트 관리
// 모든 모듈에서 필요한 상태와 설정을 하나의 구조체로 통합
struct context {
    // 시스템 설정 (config.c에서 초기화)
    struct {
        int cpu;                        // CPU 바인딩 번호
        int prio;                       // 스케줄링 우선순위
        int port;                       // 네트워크 포트
        const char *addr;               // 서버 주소
        char node_id[TINFO_NODEID_MAX]; // 노드 식별자
        bool enable_sync;               // 타이머 동기화 활성화
        bool enable_plot;               // 플롯 기능 활성화
        clockid_t clockid;              // 사용할 클록 타입
        int traceduration;              // 트레이스 지속 시간
    } config;

    // 런타임 상태 (실행 중 변경되는 동적 상태)
    struct {
        struct listhead tt_list;        // 시간 트리거 태스크 목록
        struct sched_info sched_info;   // 스케줄링 정보
        volatile sig_atomic_t shutdown_requested; // 종료 요청 플래그
        struct timespec starttimer_ts;  // 시작 타이머 타임스탬프
    } runtime;

    // 통신 관련 (D-Bus, 이벤트 루프)
    struct {
        sd_event *event;                // systemd 이벤트 루프
        sd_bus *dbus;                   // D-Bus 연결
    } comm;

    // 하이퍼피리어드 관리자 (hyperperiod.c에서 관리)
    struct hyperperiod_manager hp_manager;
};

// ===== TT 시스템 함수 선언 =====
// 모듈별로 체계적으로 정리된 함수 인터페이스

// ===== 설정 관리 (config.c) =====
tt_error_t parse_config(int argc, char *argv[], struct context *ctx);
tt_error_t validate_config(const struct context *ctx);

// ===== 코어 엔진 (core.c) =====
void timer_expired_handler(union sigval value);
tt_error_t start_timers(struct context *ctx);
tt_error_t epoll_loop(struct context *ctx);
tt_error_t handle_sigwait_bpf_event(void *ctx, void *data, size_t len);
tt_error_t handle_schedstat_bpf_event(void *ctx, void *data, size_t len);

// ===== 하이퍼피리어드 관리 (hyperperiod.c) =====
tt_error_t init_hyperperiod(struct context *ctx, const char *workload_id, uint64_t hyperperiod_us, struct hyperperiod_manager *hp_mgr);
void hyperperiod_cycle_handler(union sigval value);
uint64_t get_hyperperiod_relative_time(const struct hyperperiod_manager *hp_mgr);
void log_hyperperiod_statistics(const struct hyperperiod_manager *hp_mgr);
tt_error_t start_hyperperiod_timer(struct context *ctx);

// ===== 태스크 관리 (task.c) =====
tt_error_t init_task_list(struct context *ctx);
void destroy_task_info_list(struct task_info *tasks);

// ===== 네트워크 통신 (trpc.c) =====
tt_error_t init_trpc(struct context *ctx);
tt_error_t sync_timer_with_server(struct context *ctx);
tt_error_t deserialize_sched_info(struct context *ctx, serial_buf_t *sbuf, struct sched_info *sinfo);
tt_error_t report_deadline_miss(struct context *ctx, const char *taskname);

// ===== 시그널 처리 (signal.c) =====
tt_error_t setup_signal_handlers(struct context *ctx);

// ===== 리소스 정리 (cleanup.c) =====
void cleanup_context(struct context *ctx);

// ===== 유틸리티 함수들 =====
tt_error_t calibrate_bpf_time_offset(void);
tt_error_t setup_trace_stop_timer(struct context *ctx, int duration, timer_t *timer);

#endif /* _INTERNAL_H */
