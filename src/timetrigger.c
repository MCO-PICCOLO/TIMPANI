#define _GNU_SOURCE
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

#include "libtttrace.h"
#include "libttsched.h"
#include <libtrpc.h>

#include "schedinfo.h"
#include "timetrigger.h"

#include "trace_bpf.h"

struct time_trigger {
	timer_t timer;
	struct task_info task;
#ifdef CONFIG_TRACE_BPF
	uint64_t sigwait_ts;
	uint64_t sigwait_ts_prev;
	uint8_t sigwait_enter;
#endif
	struct timespec prev_timer;
	LIST_ENTRY(time_trigger) entry;
};

// Hyperperiod and workload management structure
struct hyperperiod_manager {
	char workload_id[64];
	uint64_t hyperperiod_us;
	uint64_t current_cycle;
	uint64_t hyperperiod_start_time_us;

	// Hyperperiod-based timing
	timer_t hyperperiod_timer;
	struct timespec hyperperiod_start_ts;

	// Task execution tracking within hyperperiod
	uint32_t tasks_in_hyperperiod;
	struct time_trigger *tt_list;

	// Statistics
	uint64_t completed_cycles;
	uint32_t total_deadline_misses;
	uint32_t cycle_deadline_misses;
} hp_manager;

LIST_HEAD(listhead, time_trigger);

// Forward declarations
static void free_task_list(struct task_info *tasks);
static void remove_tt_node(struct time_trigger *tt_node);
static void cleanup_resources(void);
static void signal_handler(int signo);
static void setup_signal_handlers(void);

// Hyperperiod management functions
static int init_hyperperiod_manager(const char *workload_id, uint64_t hyperperiod_us);
static void hyperperiod_cycle_handler(union sigval value);
static uint64_t get_hyperperiod_relative_time_us(void);
static void log_hyperperiod_statistics(void);

static volatile sig_atomic_t shutdown_requested = 0;
static struct listhead *global_tt_list = NULL;

// Global sched_info and D-Bus variables
static struct sched_info sched_info;
static sd_event *trpc_event = NULL;
static sd_bus *trpc_dbus = NULL;

static void cleanup_resources(void)
{
	if (global_tt_list) {
		struct time_trigger *tt_p;
		while (!LIST_EMPTY(global_tt_list)) {
			tt_p = LIST_FIRST(global_tt_list);
			bpf_del_pid(tt_p->task.pid);
			if (tt_p->task.pidfd >= 0) {
				close(tt_p->task.pidfd);
			}
			remove_tt_node(tt_p);
		}
	}

	// Clean up sched_info tasks
	free_task_list(sched_info.tasks);
	sched_info.tasks = NULL;

	// Clean up D-Bus resources
	if (trpc_dbus) {
		sd_bus_unref(trpc_dbus);
		trpc_dbus = NULL;
	}
	if (trpc_event) {
		sd_event_unref(trpc_event);
		trpc_event = NULL;
	}

	// Clean up hyperperiod timer
	if (hp_manager.hyperperiod_us > 0) {
		timer_delete(hp_manager.hyperperiod_timer);
		log_hyperperiod_statistics();
	}

	// Turn off BPF and tracing
	bpf_off();
	tracer_off();
}

static void signal_handler(int signo)
{
	shutdown_requested = 1;
	write_trace_marker("Shutdown signal received: %d\n", signo);
}

static void setup_signal_handlers(void)
{
	struct sigaction sa;

	sa.sa_handler = signal_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;

	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
}

static int report_dmiss(sd_bus *dbus, char *node_id, const char *taskname);

// default option values
int cpu = -1;
int prio = -1;
int port = 7777;
const char *addr = "127.0.0.1";
char node_id[TINFO_NODEID_MAX] = "1";
int enable_sync;
int enable_plot;
clockid_t clockid = CLOCK_REALTIME;
int traceduration = 3;		// trace during 3 seconds

// start timer
#define STARTTIMER_INC_IN_NS	5000000		/* 5 ms */
struct timespec starttimer_ts;

// TT Handler function executed upon timer expiration based on each period
static void tt_timer(union sigval value) {
	struct time_trigger *tt_node = (struct time_trigger *)value.sival_ptr;
	struct task_info *task = (struct task_info *)&tt_node->task;
	struct timespec before, after;
	uint64_t hyperperiod_position_us;

	clock_gettime(clockid, &before);

	// Calculate position within hyperperiod
	hyperperiod_position_us = get_hyperperiod_relative_time_us();

	write_trace_marker("%s: Timer expired: now: %lld, diff: %lld, hyperperiod_pos: %lu us\n",
			task->name, ts_ns(before), ts_diff(before, tt_node->prev_timer), hyperperiod_position_us);

	// If a task has its own release time, do nanosleep
	if (task->release_time) {
		struct timespec ts = us_ts(task->release_time);
		clock_nanosleep(clockid, 0, &ts, NULL);
	}

#ifdef CONFIG_TRACE_BPF
	/* Check whether there is a deadline miss or not */
	if (tt_node->sigwait_ts) {
		uint64_t deadline_ns = ts_ns(before);

		// Check if this task is still running
		if (!tt_node->sigwait_enter) {
			printf("!!! DEADLINE MISS: STILL OVERRUN %s(%d): deadline %lu !!!\n",
				task->name, task->pid, deadline_ns);
			hp_manager.total_deadline_misses++;
			hp_manager.cycle_deadline_misses++;
			report_dmiss(trpc_dbus, node_id, task->name);
		// Check if this task meets the deadline
		} else if (tt_node->sigwait_ts > deadline_ns) {
			printf("!!! DEADLINE MISS %s(%d): %lu > deadline %lu !!!\n",
				task->name, task->pid, tt_node->sigwait_ts, deadline_ns);
			write_trace_marker("%s: Deadline miss: %lu diff\n",
				task->name, tt_node->sigwait_ts - deadline_ns);
			hp_manager.total_deadline_misses++;
			hp_manager.cycle_deadline_misses++;
			report_dmiss(trpc_dbus, node_id, task->name);
		// Check if this task is stuck at kernel sigwait syscall handler
		} else if (tt_node->sigwait_ts == tt_node->sigwait_ts_prev) {
			printf("!!! DEADLINE MISS: STUCK AT KERNEL %s(%d): %lu & deadline %lu !!!\n",
				task->name, task->pid, tt_node->sigwait_ts, deadline_ns);
			write_trace_marker("%s: Deadline miss: %lu diff\n",
				task->name, tt_node->sigwait_ts - deadline_ns);
			hp_manager.total_deadline_misses++;
			hp_manager.cycle_deadline_misses++;
			report_dmiss(trpc_dbus, node_id, task->name);
		}

		tt_node->sigwait_ts_prev = tt_node->sigwait_ts;
	}
#endif

	clock_gettime(clockid, &after);
	write_trace_marker("%s: Send signal(%d) to %d: now: %lld, lat between timer and signal: %lld us \n",
			task->name, SIGNO_TT, task->pid, ts_ns(after), ( ts_diff(after, before) / NSEC_PER_USEC ));

	// Send the signal to the target process
	if (send_signal_pidfd(task->pidfd, SIGNO_TT) < 0) {
		fprintf(stderr, "Failed to send signal via pidfd to %s (PID %d)\n",
			task->name, task->pid);
		// TODO: check if the process is still alive
	}

	tt_node->prev_timer = before;
}

#if defined(CONFIG_TRACE_EVENT) || defined(CONFIG_TRACE_BPF_EVENT)
static void sighan_stoptracer(int signo, siginfo_t *info, void *context) {
	struct timespec now;

	clock_gettime(clockid, &now);
	write_trace_marker("Stop Tracer: %lld \n", ts_ns(now));
	tracer_off();
	traceduration = 0;
	printf("tracer_off!!!: %ld\n", ts_ns(now));
	signal(signo, SIG_IGN);
}

static bool set_stoptracer_timer(int duration, timer_t *timer) {
	struct sigevent sev = {};
	struct itimerspec its = {};
	struct sigaction sa = {};

	sa.sa_flags = SA_SIGINFO;
	sa.sa_sigaction = &sighan_stoptracer;
	if (sigaction(SIGNO_STOPTRACER, &sa, NULL) == -1) {
		perror("Failed to set up signal handler");
		return false;
	}

	sev.sigev_notify = SIGEV_SIGNAL;
	sev.sigev_signo = SIGNO_STOPTRACER;

	its.it_value.tv_sec = starttimer_ts.tv_sec + duration;
	its.it_value.tv_nsec = starttimer_ts.tv_nsec;
	its.it_interval.tv_sec = duration;
	its.it_interval.tv_nsec = 0;

	if (timer_create(clockid, &sev, timer) == -1) {
		perror("Failed to create timer");
		return false;
	}

	if (timer_settime(*timer, TIMER_ABSTIME, &its, NULL) == -1) {
		perror("Failed to set timer period");
		return false;
	}

	return true;
}
#else
static inline bool set_stoptracer_timer(int duration, timer_t *timer) { return false; }
#endif

#ifdef CONFIG_TRACE_BPF
static uint64_t bpf_ktime_off;

static void calibrate_bpf_ktime_offset(void)
{
	int i;
	struct timespec t1, t2, t3;
	uint64_t best_delta = 0, delta, ts;

	for (i = 0; i < 10; i++) {
		clock_gettime(CLOCK_REALTIME, &t1);
		clock_gettime(CLOCK_MONOTONIC, &t2);
		clock_gettime(CLOCK_REALTIME, &t3);

		delta = ts_ns(t3) - ts_ns(t1);
		ts = (ts_ns(t3) + ts_ns(t1)) / 2;

		if (i == 0 || delta < best_delta) {
			best_delta = delta;
			bpf_ktime_off = ts - ts_ns(t2);
		}
	}
}

static inline uint64_t bpf_ktime_to_real(uint64_t bpf_ts)
{
	return bpf_ktime_off + bpf_ts;
}

static int sigwait_bpf_callback(void *ctx, void *data, size_t len)
{
	struct sigwait_event *e = (struct sigwait_event *)data;
	struct listhead *lh_p = (struct listhead *)ctx;
	struct time_trigger *tt_p;

	LIST_FOREACH(tt_p, lh_p, entry) {
		if (tt_p->task.pid == e->pid) {
#if 0
			printf("[%lu] %s(%d) sigwait %s\n",
				e->timestamp, tt_p->task.name, tt_p->task.pid,
				e->enter ? "enter" : "exit");
#endif
			tt_p->sigwait_ts = bpf_ktime_to_real(e->timestamp);
			tt_p->sigwait_enter = e->enter;
			break;
		}
	}

	return 0;
}
#else
static inline void calibrate_bpf_ktime_offset(void) {}
static inline uint64_t bpf_ktime_to_real(uint64_t bpf_ts) { return bpf_ts; }
static inline int sigwait_bpf_callback(void *ctx, void *data, size_t len) {}
#endif

#ifdef CONFIG_TRACE_BPF_EVENT
#define BPF_EVENT_TIME_DIV	1000	// divisor for time axis unit: 1 us

#define BPF_EVENT_NS_TO_UNIT(ns) \
	(((ns) + (BPF_EVENT_TIME_DIV - 1)) / BPF_EVENT_TIME_DIV)

static inline void write_plot_data(struct schedstat_event *e, const char *tname)
{
	static uint64_t ts_first;
	static FILE *gpfile;
	uint64_t ts_wakeup, ts_start, ts_stop;

	if (traceduration == 0) {
		/* trace timer expired */
		enable_plot = 0;
		fclose(gpfile);
		gpfile = NULL;
		return;
	}

	if (ts_first == 0) {
		char fname[128];

		snprintf(fname, sizeof(fname), "%s.gpdata", node_id);
		gpfile = fopen(fname, "w+");
		if (gpfile == NULL) {
			enable_plot = 0;
			return;
		}

		ts_first = ts_ns(starttimer_ts);
	}

	// convert monotonic ktime to realtime
	ts_wakeup = bpf_ktime_to_real(e->ts_wakeup);
	ts_start = bpf_ktime_to_real(e->ts_start);
	ts_stop = bpf_ktime_to_real(e->ts_stop);

#if 0
	// This is only necessary for gnuplot
        // subtract starttimer_ts from timestamps so that timestamps start at 0
	ts_wakeup -= ts_first;
	ts_start -= ts_first;
	ts_stop -= ts_first;
#endif

        // scale ns unit up to predefined time unit in round up manner
	ts_wakeup = BPF_EVENT_NS_TO_UNIT(ts_wakeup);
	ts_start = BPF_EVENT_NS_TO_UNIT(ts_start);
	ts_stop = BPF_EVENT_NS_TO_UNIT(ts_stop);

	// Column formatting:
	// task event ignored resource priority activate start stop ignored
	fprintf(gpfile, "%-16s 0 0 %s-C%d 0 %lu %lu %lu 0\n",
		tname, node_id, e->cpu, ts_wakeup, ts_start, ts_stop);
}

static int schedstat_bpf_callback(void *ctx, void *data, size_t len)
{
	struct schedstat_event *e = (struct schedstat_event *)data;
	struct listhead *lh_p = (struct listhead *)ctx;
	struct time_trigger *tt_p;
	uint64_t runtime, latency;

	runtime = (e->ts_stop - e->ts_start) / NSEC_PER_USEC;
	latency = (e->ts_start - e->ts_wakeup) / NSEC_PER_USEC;

	LIST_FOREACH(tt_p, lh_p, entry) {
		if (tt_p->task.pid == e->pid) {
			printf("%-16s(%7d): CPU%d\truntime(%8lu us)\tlatency(%lu us)\n",
				tt_p->task.name, e->pid, e->cpu, runtime, latency);
			break;
		}
	}

	if (enable_plot && tt_p != NULL) {
		write_plot_data(e, tt_p->task.name);
	}

	return 0;
}
#else
static inline int schedstat_bpf_callback(void *ctx, void *data, size_t len) {}
#endif

static int init_trpc(const char *addr, int port, sd_bus **dbus_ret, sd_event **event_ret)
{
	int ret;
	char serv_addr[128];

	ret = sd_event_default(event_ret);
	if (ret < 0) {
		return ret;
	}

	snprintf(serv_addr, sizeof(serv_addr), "tcp:host=%s,port=%u", addr, port);
	ret = trpc_client_create(serv_addr, *event_ret, dbus_ret);
	if (ret < 0) {
		*event_ret = sd_event_unref(*event_ret);
		return ret;
	}

	return 0;
}

static void free_task_list(struct task_info *tasks)
{
	struct task_info *current = tasks;
	while (current) {
		struct task_info *next = current->next;
		free(current);
		current = next;
	}
}

static int deserialize_schedinfo(serial_buf_t *sbuf, struct sched_info *sinfo)
{
	uint32_t i;
	uint32_t cid_size;

	uint64_t hyperperiod_us = 0;
	char workload_id[64] = { 0 };

	// Unpack sched_info
	if (deserialize_int32_t(sbuf, &sinfo->nr_tasks) < 0) {
		fprintf(stderr, "Failed to deserialize nr_tasks\n");
		return -1;
	}
	sinfo->tasks = NULL;

#if 0
	printf("sinfo->nr_tasks: %u\n", sinfo->nr_tasks);
#endif

	// Unpack task_info list entries
	for (i = 0; i < sinfo->nr_tasks; i++) {
		struct task_info *tinfo = malloc(sizeof(struct task_info));
		if (tinfo == NULL) {
			fprintf(stderr, "Failed to allocate memory for task_info\n");
			free_task_list(sinfo->tasks);
			sinfo->tasks = NULL;
			return -1;
		}

		if (deserialize_str(sbuf, tinfo->node_id) < 0 ||
		    deserialize_int32_t(sbuf, &tinfo->allowable_deadline_misses) < 0 ||
		    deserialize_int64_t(sbuf, &tinfo->cpu_affinity) < 0 ||
		    deserialize_int32_t(sbuf, &tinfo->deadline) < 0 ||
		    deserialize_int32_t(sbuf, &tinfo->runtime) < 0 ||
		    deserialize_int32_t(sbuf, &tinfo->release_time) < 0 ||
		    deserialize_int32_t(sbuf, &tinfo->period) < 0 ||
		    deserialize_int32_t(sbuf, &tinfo->sched_policy) < 0 ||
		    deserialize_int32_t(sbuf, &tinfo->sched_priority) < 0 ||
		    deserialize_str(sbuf, tinfo->name) < 0) {
			fprintf(stderr, "Failed to deserialize task_info fields\n");
			free(tinfo);
			free_task_list(sinfo->tasks);
			sinfo->tasks = NULL;
			return -1;
		}

		tinfo->next = sinfo->tasks;
		sinfo->tasks = tinfo;

#if 1
		printf("tinfo->name: %s\n", tinfo->name);
		printf("tinfo->sched_priority: %d\n", tinfo->sched_priority);
		printf("tinfo->sched_policy: %d\n", tinfo->sched_policy);
		printf("tinfo->period: %d\n", tinfo->period);
		printf("tinfo->release_time: %d\n", tinfo->release_time);
		printf("tinfo->runtime: %d\n", tinfo->release_time);
		printf("tinfo->deadline: %d\n", tinfo->release_time);
		printf("tinfo->cpu_affinity: 0x%lx\n", tinfo->cpu_affinity);
		printf("tinfo->allowable_deadline_misses: %d\n", tinfo->allowable_deadline_misses);
		printf("tinfo->node_id: %s\n", tinfo->node_id);
#endif
	}

	if (deserialize_str(sbuf, workload_id) < 0 ||
	    deserialize_int64_t(sbuf, &hyperperiod_us) < 0) {
		fprintf(stderr, "Failed to deserialize workload info\n");
		free_task_list(sinfo->tasks);
		sinfo->tasks = NULL;
		return -1;
	}

	printf("\n\nworkload: %s\n", workload_id);
	printf("hyperperiod: %lu us\n", hyperperiod_us);

	// Initialize hyperperiod manager with received information
	if (init_hyperperiod_manager(workload_id, hyperperiod_us) < 0) {
		fprintf(stderr, "Failed to initialize hyperperiod manager\n");
		free_task_list(sinfo->tasks);
		sinfo->tasks = NULL;
		return -1;
	}

	return 0;
}

static int get_schedinfo(sd_bus *dbus, char *node_id)
{
	int ret;
	void *buf = NULL;
	size_t bufsize;
	serial_buf_t *sbuf = NULL;

	ret = trpc_client_schedinfo(dbus, node_id, &buf, &bufsize);
	if (ret < 0) {
		return ret;
	}

	if (buf == NULL || bufsize == 0) {
		printf("Failed to get schedule info\n");
		return -1;
	}

	sbuf = make_serial_buf((void *)buf, bufsize);
	if (sbuf == NULL) {
		return -1;
	}
	buf = NULL;	// now use sbuf->data

	deserialize_schedinfo(sbuf, &sched_info);

	free_serial_buf(sbuf);

	return 0;
}

static int sync_timer(sd_bus *dbus, char *node_id, struct timespec *ts_ptr)
{
	int ret;
	int ack;

	printf("Sync");
	fflush(stdout);
	while (1) {
		ret = trpc_client_sync(dbus, node_id, &ack, ts_ptr);
		if (ret < 0) {
			return ret;
		}

		if (ack) {
			printf("\ntimestamp: %ld sec %ld nsec\n", ts_ptr->tv_sec, ts_ptr->tv_nsec);
			break;
		}

		printf(".");
		fflush(stdout);
		/* sleep 100ms to prevent busy polling */
		usleep(100000);
	}

	return 0;
}

static int init_trpc_schedinfo(const char *addr, int port,
				sd_bus **dbus_ret, sd_event **event_ret,
				char *node_id)
{
	int retry_count = 0;
	const int max_retries = 300; // 300 seconds timeout

	// Initialze trpc channel and get schedule info with retry logic
	while (retry_count < max_retries) {
		if (init_trpc(addr, port, dbus_ret, event_ret) == 0) {
			if (get_schedinfo(*dbus_ret, node_id) == 0) {
				/* Successfully retrieved schedule info */
				printf("Successfully connected and retrieved schedule info (attempt %d)\n", retry_count + 1);
				return 0;
			}
		}

		/* failed to get schedule info, retrying */
		retry_count++;
		printf("Connection attempt %d/%d failed, retrying...\n", retry_count, max_retries);
		usleep(1000000); // 1 second
	}

	fprintf(stderr, "Failed to connect to server after %d attempts\n", max_retries);
	return -1;
}

static void remove_tt_node(struct time_trigger *tt_node) {
	timer_delete(tt_node->timer);
	LIST_REMOVE(tt_node, entry);
	free(tt_node);
}

// Hyperperiod management implementation
static int init_hyperperiod_manager(const char *workload_id, uint64_t hyperperiod_us)
{
	strncpy(hp_manager.workload_id, workload_id, sizeof(hp_manager.workload_id) - 1);
	hp_manager.hyperperiod_us = hyperperiod_us;
	hp_manager.current_cycle = 0;
	hp_manager.completed_cycles = 0;
	hp_manager.total_deadline_misses = 0;
	hp_manager.cycle_deadline_misses = 0;
	hp_manager.tasks_in_hyperperiod = 0;

	// Hyperperiod start time will be set when timers actually start
	hp_manager.hyperperiod_start_time_us = 0;

	printf("Hyperperiod Manager initialized:\n");
	printf("  Workload ID: %s\n", hp_manager.workload_id);
	printf("  Hyperperiod: %lu us (%.3f ms)\n",
		hp_manager.hyperperiod_us, hp_manager.hyperperiod_us / 1000.0);
	printf("  Start time will be set when timers start\n");

	return 0;
}

static void hyperperiod_cycle_handler(union sigval value)
{
	struct timespec now;
	uint64_t cycle_time_us;

	clock_gettime(clockid, &now);
	cycle_time_us = ts_us(now);

	// Update cycle information
	hp_manager.completed_cycles++;
	hp_manager.current_cycle = (hp_manager.current_cycle + 1) %
		((hp_manager.hyperperiod_us > 0) ? 1 : 1); // Will be used for multi-cycle tracking

	write_trace_marker("Hyperperiod cycle %lu completed at %lu us, deadline misses in this cycle: %u\n",
		hp_manager.completed_cycles, cycle_time_us, hp_manager.cycle_deadline_misses);

#if HP_DEBUG
	printf("Hyperperiod cycle %lu completed (total misses: %u, cycle misses: %u)\n",
		hp_manager.completed_cycles, hp_manager.total_deadline_misses, hp_manager.cycle_deadline_misses);
#endif

	// Reset cycle-specific counters
	hp_manager.cycle_deadline_misses = 0;

	// Log statistics every 100 cycles
	if (hp_manager.completed_cycles % 100 == 0) {
		log_hyperperiod_statistics();
	}
}

static uint64_t get_hyperperiod_relative_time_us(void)
{
	struct timespec now;
	clock_gettime(clockid, &now);

	uint64_t current_time_us = ts_us(now);

	// If hyperperiod hasn't started yet, return 0
	if (hp_manager.hyperperiod_start_time_us == 0) {
		return 0;
	}

	uint64_t elapsed_us = current_time_us - hp_manager.hyperperiod_start_time_us;

	// Return position within current hyperperiod
	return elapsed_us % hp_manager.hyperperiod_us;
}

static void log_hyperperiod_statistics(void)
{
	double miss_rate = hp_manager.completed_cycles > 0 ?
		(double)hp_manager.total_deadline_misses / hp_manager.completed_cycles : 0.0;

	printf("\n=== Hyperperiod Statistics ===\n");
	printf("Workload: %s\n", hp_manager.workload_id);
	printf("Completed cycles: %lu\n", hp_manager.completed_cycles);
	printf("Hyperperiod length: %lu us\n", hp_manager.hyperperiod_us);
	printf("Total deadline misses: %u\n", hp_manager.total_deadline_misses);
	printf("Miss rate per cycle: %.4f\n", miss_rate);
	printf("Tasks in hyperperiod: %u\n", hp_manager.tasks_in_hyperperiod);
	printf("==============================\n\n");
}

static int report_dmiss(sd_bus *dbus, char *node_id, const char *taskname)
{
	int ret;

	return trpc_client_dmiss(dbus, node_id, taskname);
}

static int get_options(int argc, char *argv[])
{
	int opt;

	while ((opt = getopt(argc, argv, "hc:P:p:n:st:g")) >= 0) {
		switch (opt) {
		case 'c':
			cpu = atoi(optarg);
			break;
		case 'P':
			prio = atoi(optarg);
			break;
		case 'p':
			port = atoi(optarg);
			break;
		case 't':
			traceduration = atoi(optarg);
			break;
		case 'n':
			strncpy(node_id, optarg, sizeof(node_id) - 1);
			break;
		case 's':
			enable_sync = 1;
			break;
		case 'g':
			enable_plot = 1;
			break;
		case 'h':
		default:
			fprintf(stderr, "Usage: %s [options] [host]\n"
					"Options:\n"
					"  -c <cpu_num>\tcpu affinity for timetrigger\n"
					"  -P <prio>\tRT priority (1~99) for timetrigger\n"
					"  -p <port>\tport to connect to\n"
					"  -t <seconds>\ttrace duration in seconds\n"
					"  -n <node id>\tNode ID\n"
					"  -s\tEnable timer synchronization across multiple nodes\n"
					"  -g\tEnable saving plot data file by using BPF (<node id>.gpdata)\n"
					"  -h\tshow this help\n",
					argv[0]);
			return -1;
		}
	}

	if (optind < argc) {
		addr = argv[optind++];
	}
	return 0;
}

static int init_time_trigger_list(struct listhead *lh_ptr, char *node_id)
{
	int success_count = 0;

	LIST_INIT(lh_ptr);

	for (struct task_info *ti = sched_info.tasks; ti; ti = ti->next) {
		struct time_trigger *tt_node;
		unsigned int pid, priority, policy;

		if (strcmp(node_id, ti->node_id) != 0) {
			/* The task does not belong to this node. */
			continue;
		}

		tt_node = calloc(1, sizeof(struct time_trigger));
		if (!tt_node) {
			fprintf(stderr, "Failed to allocate memory for time_trigger\n");
			continue;
		}

		memcpy(&tt_node->task, ti, sizeof(tt_node->task));

		pid = get_pid_by_name(tt_node->task.name);
		if (pid == -1) {
			printf("%s is not running !\n", tt_node->task.name);
			free(tt_node);
			continue;
		}

		set_affinity(pid, tt_node->task.cpu_affinity);
		priority = tt_node->task.sched_priority;
		policy = tt_node->task.sched_policy;

		set_schedattr(pid, priority, policy);

		tt_node->task.pid = pid;

		// Create pidfd for the task
		tt_node->task.pidfd = create_pidfd(pid);
		if (tt_node->task.pidfd < 0) {
			fprintf(stderr, "Failed to create pidfd for task %s (PID %d)\n",
				tt_node->task.name, pid);
			free(tt_node);
			continue;
		}

		LIST_INSERT_HEAD(lh_ptr, tt_node, entry);

		if (bpf_add_pid(pid) < 0) {
			fprintf(stderr, "Failed to add PID %d to BPF monitoring\n", pid);
			// Continue anyway, monitoring is not critical for basic operation
		}

		// Count tasks for hyperperiod management
		hp_manager.tasks_in_hyperperiod++;

		success_count++;
	}

	if (success_count == 0) {
		fprintf(stderr, "No tasks were successfully initialized\n");
		return -1;
	}

	printf("Successfully initialized %d tasks\n", success_count);
	return 0;
}

static int start_tt_timer(struct listhead *lh_ptr)
{
	struct time_trigger *tt_p;

	if (!enable_sync) {
		/* No synchronization across multiple nodes */
		clock_gettime(clockid, &starttimer_ts);
		starttimer_ts.tv_nsec += STARTTIMER_INC_IN_NS;
	}

	LIST_FOREACH(tt_p, lh_ptr, entry) {
		struct itimerspec its;
		struct sigevent sev;

		memset(&sev, 0, sizeof(sev));
		memset(&its, 0, sizeof(its));

		sev.sigev_notify = SIGEV_THREAD;
		sev.sigev_notify_function = tt_timer;

		sev.sigev_value.sival_ptr = tt_p;

		its.it_value.tv_sec = starttimer_ts.tv_sec;
		its.it_value.tv_nsec = starttimer_ts.tv_nsec;
		its.it_interval.tv_sec = tt_p->task.period / USEC_PER_SEC;
		its.it_interval.tv_nsec = tt_p->task.period % USEC_PER_SEC * NSEC_PER_USEC;

		printf("%s(%d) period: %d starttimer_ts: %ld interval: %lds %ldns\n",
				tt_p->task.name, tt_p->task.pid,
				tt_p->task.period, ts_ns(its.it_value),
				its.it_interval.tv_sec, its.it_interval.tv_nsec);

		if (timer_create(clockid, &sev, &tt_p->timer)) {
			perror("Failed to create timer");
			return -1;
		}

		if (timer_settime(tt_p->timer, TIMER_ABSTIME, &its, NULL)) {
			perror("Failed to start timer");
			return -1;
		}
	}

	return 0;
}

static int epoll_loop(struct listhead *lh_ptr)
{
	int efd;
	efd = epoll_create1(0);
	if (efd < 0) {
		perror("epoll_create failed");
		return -1;
	}

	struct time_trigger *tt_p;
	LIST_FOREACH(tt_p, lh_ptr, entry) {
		printf("TT will wake up Process %s(%d) with duration %d us, release_time %d, allowable_deadline_misses: %d\n",
			tt_p->task.name, tt_p->task.pid, tt_p->task.period, tt_p->task.release_time, tt_p->task.allowable_deadline_misses);

		struct epoll_event event;
		event.data.fd = tt_p->task.pidfd;
		event.events = EPOLLIN;
		if (epoll_ctl(efd, EPOLL_CTL_ADD, tt_p->task.pidfd, &event) < 0) {
			perror("epoll_ctl failed");
			close(efd);
			return -1;
		}
	}

	// Main execution loop with graceful shutdown support
	printf("Time Trigger started. Press Ctrl+C to stop gracefully.\n");
	while (!shutdown_requested) {
		struct epoll_event events[1];
		int count = epoll_wait(efd, events, 1, -1);
		if (count < 0) {
			if (errno == EINTR) {
				// Ctrl+C pressed or a signal received
				break;
			}
			perror("epoll_wait failed");
			close(efd);
			return -1;
		}

		LIST_FOREACH(tt_p, lh_ptr, entry) {
			if (tt_p->task.pidfd == events[0].data.fd) {
				// Handle task termination
				printf("Task %s(%d) terminated\n",
					tt_p->task.name, tt_p->task.pid);
				epoll_ctl(efd, EPOLL_CTL_DEL, tt_p->task.pidfd, NULL);
				// TODO: Recovery from task termination
			}
		}
	}
	close(efd);
	return 0;
}

static int start_hyperperiod_timer(void)
{
	struct itimerspec its;
	struct sigevent sev;

	if (hp_manager.hyperperiod_us == 0) {
		printf("Warning: Hyperperiod not set, skipping hyperperiod timer\n");
		return 0;
	}

	// Set hyperperiod start time to match with task timers
	hp_manager.hyperperiod_start_ts = starttimer_ts;
	hp_manager.hyperperiod_start_time_us = ts_us(hp_manager.hyperperiod_start_ts);

	printf("Hyperperiod start time set: %lu us\n", hp_manager.hyperperiod_start_time_us);

	memset(&sev, 0, sizeof(sev));
	memset(&its, 0, sizeof(its));

	sev.sigev_notify = SIGEV_THREAD;
	sev.sigev_notify_function = hyperperiod_cycle_handler;
	sev.sigev_value.sival_ptr = &hp_manager;

	// Set hyperperiod cycle interval
	its.it_value.tv_sec = starttimer_ts.tv_sec + (hp_manager.hyperperiod_us / USEC_PER_SEC);
	its.it_value.tv_nsec = starttimer_ts.tv_nsec + (hp_manager.hyperperiod_us % USEC_PER_SEC) * NSEC_PER_USEC;
	if (its.it_value.tv_nsec >= NSEC_PER_SEC) {
		its.it_value.tv_sec++;
		its.it_value.tv_nsec -= NSEC_PER_SEC;
	}

	its.it_interval.tv_sec = hp_manager.hyperperiod_us / USEC_PER_SEC;
	its.it_interval.tv_nsec = (hp_manager.hyperperiod_us % USEC_PER_SEC) * NSEC_PER_USEC;

	printf("Starting hyperperiod timer: %lu us interval (%lds %ldns)\n",
		hp_manager.hyperperiod_us, its.it_interval.tv_sec, its.it_interval.tv_nsec);

	if (timer_create(clockid, &sev, &hp_manager.hyperperiod_timer)) {
		perror("Failed to create hyperperiod timer");
		return -1;
	}

	if (timer_settime(hp_manager.hyperperiod_timer, TIMER_ABSTIME, &its, NULL)) {
		perror("Failed to start hyperperiod timer");
		return -1;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	struct listhead lh;
	pid_t pid = getpid();

	timer_t tracetimer;

	bool settimer = false;

	if (get_options(argc, argv) < 0) {
		return EXIT_FAILURE;
	}

	if (cpu != -1) {
		set_affinity(pid, cpu);
	}
	if (prio > 0 && prio <= 99) {
		set_schedattr(pid, prio, SCHED_FIFO);
	}

	// Setup signal handlers for graceful shutdown
	setup_signal_handlers();

	// Set global reference for cleanup
	global_tt_list = &lh;

	// Calibrate BPF ktime(CLOCK_MONOTONIC) offset to CLOCK_REALTIME
	calibrate_bpf_ktime_offset();

	// Initialze trpc channel and get schedule info
	if (init_trpc_schedinfo(addr, port, &trpc_dbus, &trpc_event, node_id) < 0) {
		fprintf(stderr, "Failed to initialize TRPC and get schedule info\n");
		return EXIT_FAILURE;
	}

	// Activate BPF programs
	bpf_on(sigwait_bpf_callback, schedstat_bpf_callback, (void *)&lh);

	// Initialize time_trigger linked list
	if (init_time_trigger_list(&lh, node_id) < 0) {
		fprintf(stderr, "Failed to initialize time trigger list\n");
		cleanup_resources();
		return EXIT_FAILURE;
	}

	// Synchronize hrtimers across multiple nodes
	if (enable_sync && sync_timer(trpc_dbus, node_id, &starttimer_ts) < 0) {
		fprintf(stderr, "Failed to synchronize timers\n");
		cleanup_resources();
		return EXIT_FAILURE;
	}

	// Setup and start hrtimers for tasks
	if (start_tt_timer(&lh) < 0) {
		fprintf(stderr, "Failed to start timers\n");
		cleanup_resources();
		return EXIT_FAILURE;
	}

	// Start hyperperiod monitoring timer
	if (start_hyperperiod_timer() < 0) {
		fprintf(stderr, "Failed to start hyperperiod timer\n");
		cleanup_resources();
		return EXIT_FAILURE;
	}

	// Activate ftrace and its stop timer
	settimer = set_stoptracer_timer(traceduration, &tracetimer);
	tracer_on();

#if defined(CONFIG_TRACE_EVENT) || defined(CONFIG_TRACE_BPF_EVENT)
	struct timespec now;
	clock_gettime(clockid, &now);
	printf("tracer_on!!!: %ld\n", ts_ns(now));
#endif

	if (epoll_loop(&lh) < 0) {
		fprintf(stderr, "epoll_loop failed\n");
		cleanup_resources();
		return EXIT_FAILURE;
	}

	printf("Shutdown requested, cleaning up resources...\n");
	cleanup_resources();

	if (settimer) {
		timer_delete(tracetimer);
	}

	printf("Time Trigger shutdown completed.\n");
	return EXIT_SUCCESS;
}
