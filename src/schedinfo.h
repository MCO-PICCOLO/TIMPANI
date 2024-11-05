#ifndef _SCHEDINFO_H_
#define _SCHEDINFO_H_

#ifdef __cplusplus
extern "C" {
#endif

#define SINFO_NODE_MAX		16

struct task_info {
	uint32_t pid;
	char name[16];
	uint32_t sched_priority;
	uint32_t sched_policy;
	uint32_t period;
	uint32_t release_time;
	uint32_t allowable_deadline_misses;
	uint32_t node_id;
	struct task_info *next;
};

struct sched_info {
	char container_id[64];
	int32_t container_rt_runtime;
	int32_t container_rt_period;
	uint64_t cpumask;

	int32_t container_period;
	int32_t pod_period;

	uint32_t nr_tasks;

	struct task_info *tasks;

	uint32_t nr_nodes;
	uint32_t node_ids[SINFO_NODE_MAX];
};

#ifdef __cplusplus
}
#endif

#endif	/* _SCHEDINFO_H_ */
