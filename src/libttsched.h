#ifndef _LIBTTSCHED_H
#define _LIBTTSCHED_H

#ifdef __cplusplus
extern "C" {
#endif

// ===== TTSCHED 로깅 매크로 =====
#define TTSCHED_LOG_ERROR(fmt, ...) \
    fprintf(stderr, "[TTSCHED ERROR] " fmt "\n", ##__VA_ARGS__)

#define TTSCHED_LOG_WARNING(fmt, ...) \
    fprintf(stderr, "[TTSCHED WARNING] " fmt "\n", ##__VA_ARGS__)

#define TTSCHED_LOG_INFO(fmt, ...) \
    printf("[TTSCHED INFO] " fmt "\n", ##__VA_ARGS__)

#ifdef DEBUG
#define TTSCHED_LOG_DEBUG(fmt, ...) \
    printf("[TTSCHED DEBUG] " fmt "\n", ##__VA_ARGS__)
#else
#define TTSCHED_LOG_DEBUG(fmt, ...)
#endif

// ===== TTSCHED 에러 코드 시스템 =====
typedef enum {
    TTSCHED_SUCCESS = 0,           // 성공
    TTSCHED_ERROR_INVALID_ARGS = -1, // 잘못된 인자
    TTSCHED_ERROR_PERMISSION = -2,   // 권한 오류
    TTSCHED_ERROR_SYSTEM = -3        // 시스템 오류
} ttsched_error_t;

// 에러 메시지 함수
static inline const char* ttsched_error_string(ttsched_error_t error)
{
    switch (error) {
        case TTSCHED_SUCCESS: return "Success";
        case TTSCHED_ERROR_INVALID_ARGS: return "Invalid arguments";
        case TTSCHED_ERROR_PERMISSION: return "Permission denied";
        case TTSCHED_ERROR_SYSTEM: return "System error";
        default: return "Unknown error";
    }
}

struct sched_attr_tt {
	uint32_t size;			/* Size of this structure */
	uint32_t sched_policy;		/* Policy (SCHED_*)
					   SCHED_NORMAL            0
					   SCHED_FIFO              1
					   SCHED_RR                2
					   SCHED_BATCH             3
					   SCHED_IDLE              5
					   SCHED_DEADLINE          6 */
	uint64_t sched_flags;		/* Flags */
	int32_t  sched_nice;		/* Nice value (SCHED_OTHER,
					   SCHED_BATCH) */
	uint32_t sched_priority;	/* Static priority (SCHED_FIFO,
					   SCHED_RR) */
	/* Remaining fields are for SCHED_DEADLINE */
	uint64_t sched_runtime;
	uint64_t sched_deadline;
	uint64_t sched_period;
};

ttsched_error_t set_affinity(pid_t pid, int cpu);
ttsched_error_t set_schedattr(pid_t pid, unsigned int priority, unsigned int policy);
ttsched_error_t get_process_name_by_pid(const int pid, char name[]);
ttsched_error_t get_pid_by_name(const char *name, int *pid);

ttsched_error_t create_pidfd(pid_t pid, int *pidfd);
ttsched_error_t send_signal_pidfd(int pidfd, int signal);
ttsched_error_t is_process_alive(int pidfd, int *alive);

#ifdef __cplusplus
}
#endif

#endif	/* _LIBTTSCHED_H */
