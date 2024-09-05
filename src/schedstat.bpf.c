#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

// Common types for bpf and user-space programs
typedef __u8	uint8_t;
typedef __u16	uint16_t;
typedef __u32	uint32_t;
typedef __u64	uint64_t;
typedef __s8	int8_t;
typedef __s16	int16_t;
typedef __s32	int32_t;
typedef __s64	int64_t;

typedef int	pid_t;
typedef __u64	size_t;

// Include common header file between bpf and user-space programs
#include "trace_bpf.h"

// Ring buffer for notification events to user-space program
struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, 64 * 4096);
} buffer SEC(".maps");

// Hash map for target pids to collect events from
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1024);
	__type(key, int);
	__type(value, uint8_t);
} pid_filter_map SEC(".maps");

// BPF program's internal data structure to store specific scheduler statistics
struct sched_time {
	uint64_t ts_wakeup;
	uint64_t ts_start;
	int pid;
	int cpu;
};

// BPF program's internal hash map to save sched_waking event info
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1024);
	__type(key, int);
	__type(value, struct sched_time);
} sched_waking_map SEC(".maps");

// BPF program's internal hash map to save sched_switch event info
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1024);
	__type(key, int);
	__type(value, struct sched_time);
} sched_switch_map SEC(".maps");

// common trace event format
struct trace_entry {
	short unsigned int type;
	unsigned char flags;
	unsigned char preempt_count;
	pid_t pid;
};

// sched_waking event format
struct trace_event_sched_waking {
	struct trace_entry entry;

	char comm[16];
	pid_t pid;
	int prio;
	int target_cpu;
};

// sched_switch event format
struct trace_event_sched_switch {
	struct trace_entry entry;

	char prev_comm[16];
	pid_t prev_pid;
	int prev_prio;
	long prev_state;
	char next_comm[16];
	pid_t next_pid;
	int next_prio;
};

SEC("tracepoint/sched/sched_waking")
int handle_sched_waking(struct trace_event_sched_waking *ctx)
{
	uint8_t *filtered;
	pid_t pid = ctx->pid;

	filtered = bpf_map_lookup_elem(&pid_filter_map, &pid);
	if (!filtered) {
		return 0;
	}

	uint64_t now = bpf_ktime_get_ns();
	struct sched_time data = {};
	data.ts_wakeup = now;
	data.pid = pid;
	data.cpu = ctx->target_cpu;
	bpf_map_update_elem(&sched_waking_map, &pid, &data, BPF_ANY);

	return 0;
}

SEC("tracepoint/sched/sched_switch")
int handle_sched_switch(struct trace_event_sched_switch *ctx)
{
	uint8_t *filtered;
	uint64_t now = bpf_ktime_get_ns();
	struct sched_time *pdata;
	pid_t pid = ctx->prev_pid;

	pdata = bpf_map_lookup_elem(&sched_switch_map, &pid);
	if (pdata) {
		struct schedstat_event *e = bpf_ringbuf_reserve(&buffer, sizeof(struct schedstat_event), 0);
		if (!e) {
			return 1;
		}

		e->pid = pid;
		e->ts_wakeup = pdata->ts_wakeup;
		e->ts_start = pdata->ts_start;
		e->ts_stop = now;
		e->cpu = pdata->cpu;
		bpf_ringbuf_submit(e, 0);

		bpf_map_delete_elem(&sched_switch_map, &pid);
	}

	pid = ctx->next_pid;
	filtered = bpf_map_lookup_elem(&pid_filter_map, &pid);
	if (filtered) {
		pdata = bpf_map_lookup_elem(&sched_waking_map, &pid);
		if (pdata) {
			struct sched_time data = {};
			data.pid = pid;
			data.cpu = pdata->cpu;
			data.ts_start = now;
			data.ts_wakeup = pdata->ts_wakeup;
			bpf_map_update_elem(&sched_switch_map, &pid, &data, BPF_ANY);

			bpf_map_delete_elem(&sched_waking_map, &pid);
		}
	}

	return 0;
}

char LICENSE[] SEC("license") = "GPL";
