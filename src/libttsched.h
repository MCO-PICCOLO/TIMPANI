#ifndef _LIBTTSCHED_H
#define _LIBTTSCHED_H

#ifdef __cplusplus
extern "C" {
#endif

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

int set_affinity(pid_t pid, int cpu);
void set_schedattr(pid_t pid, unsigned int priority, unsigned int policy);
void get_process_name_by_pid(const int pid, char name[]);
int get_pid_by_name(const char *name);

int create_pidfd(pid_t pid);
int send_signal_pidfd(int pidfd, int signal);
int is_process_alive(int pidfd);

#ifdef __cplusplus
}
#endif

#endif	/* _LIBTTSCHED_H */
