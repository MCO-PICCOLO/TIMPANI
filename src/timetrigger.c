#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#include <sched.h>
#include <time.h>
#include <sys/queue.h>

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
	uint8_t sigwait_enter;
#endif
	struct timespec prev_timer;
	LIST_ENTRY(time_trigger) entry;
};

LIST_HEAD(listhead, time_trigger);

static struct sched_info sched_info;

// libtrpc D-Bus variables
static sd_event *trpc_event;
static sd_bus *trpc_dbus;

static void remove_tt_node(struct time_trigger *tt_node);
static int report_dmiss(sd_bus *dbus, int node_id, const char *taskname);

// default option values
int cpu = -1;
int prio = -1;
int port = 7777;
const char *addr = "localhost";
int node_id = 1;
int enable_sync;
int enable_gnuplot;
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

	clock_gettime(clockid, &before);
	write_trace_marker("%s: Timer expired: now: %lld, diff: %lld\n",
			task->name, ts_ns(before), ts_diff(before, tt_node->prev_timer));

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
			printf("!!! STILL OVERRUN %s(%d): %lu !!!\n", task->name, task->pid, deadline_ns);
			report_dmiss(trpc_dbus, node_id, task->name);
		// Check if this task meets the deadline
		} else if (tt_node->sigwait_ts > deadline_ns) {
			printf("!!! DEADLINE MISS %s(%d): %lu > %lu !!!\n",
				task->name, task->pid, tt_node->sigwait_ts, deadline_ns);
			write_trace_marker("%s: Deadline miss: %lu diff\n",
				task->name, tt_node->sigwait_ts - deadline_ns);
			report_dmiss(trpc_dbus, node_id, task->name);
		}
	}
#endif

	clock_gettime(clockid, &after);
	write_trace_marker("%s: Send signal(%d) to %d: now: %lld, lat between timer and signal: %lld us \n",
			task->name, SIGNO_TT, task->pid, ts_ns(after), ( ts_diff(after, before) / NSEC_PER_USEC ));

	// Send the signal to the target process
	kill(task->pid, SIGNO_TT);

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
static inline void write_gnuplot_data(struct schedstat_event *e, const char *tname)
{
	static uint64_t ts_first;
	static FILE *gpfile;
	uint64_t ts_wakeup, ts_start, ts_stop;

	if (traceduration == 0) {
		/* trace timer expired */
		enable_gnuplot = 0;
		fclose(gpfile);
		gpfile = NULL;
		return;
	}

	if (ts_first == 0) {
		char fname[32];

		snprintf(fname, sizeof(fname), "node%d.gpdata", node_id);
		gpfile = fopen(fname, "w+");
		if (gpfile == NULL) {
			enable_gnuplot = 0;
			return;
		}

		ts_first = ts_ns(starttimer_ts);
	}

	// convert monotonic ktime to realtime
	ts_wakeup = bpf_ktime_to_real(e->ts_wakeup);
	ts_start = bpf_ktime_to_real(e->ts_start);
	ts_stop = bpf_ktime_to_real(e->ts_stop);

        // subtract starttimer_ts from timestamps so that timestamps start at 0
	ts_wakeup -= ts_first;
	ts_start -= ts_first;
	ts_stop -= ts_first;

        // convert ns to ms
	ts_wakeup /= 1000000;
	ts_start /= 1000000;
	ts_stop /= 1000000;

	// Column formatting:
	// task event ignored resource priority activate start stop ignored
	fprintf(gpfile, "%-16s 0 0 N%dC%d 0 %lu %lu %lu 0\n",
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

	if (enable_gnuplot && tt_p != NULL) {
		write_gnuplot_data(e, tt_p->task.name);
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

	return ret;
}

static int deserialize_schedinfo(serial_buf_t *sbuf, struct sched_info *sinfo)
{
	uint32_t i;
	uint32_t cid_size;

	// Unpack sched_info
	deserialize_int32_t(sbuf, &sinfo->nr_tasks);
	deserialize_int32_t(sbuf, &sinfo->pod_period);
	deserialize_int32_t(sbuf, &sinfo->container_period);
	deserialize_int64_t(sbuf, &sinfo->cpumask);
	deserialize_int32_t(sbuf, &sinfo->container_rt_period);
	deserialize_int32_t(sbuf, &sinfo->container_rt_runtime);
	cid_size = sizeof(sinfo->container_id);
	deserialize_blob(sbuf, sinfo->container_id, &cid_size);

	sinfo->tasks = NULL;

#if 0
	printf("sinfo->container_id: %.*s\n", cid_size, sinfo->container_id);
	printf("sinfo->container_rt_runtime: %u\n", sinfo->container_rt_runtime);
	printf("sinfo->container_rt_period: %u\n", sinfo->container_rt_period);
	printf("sinfo->cpumask: %"PRIx64"\n", sinfo->cpumask);
	printf("sinfo->container_period: %u\n", sinfo->container_period);
	printf("sinfo->pod_period: %u\n", sinfo->pod_period);
	printf("sinfo->nr_tasks: %u\n", sinfo->nr_tasks);
#endif

	// Unpack task_info list entries
	for (i = 0; i < sinfo->nr_tasks; i++) {
		struct task_info *tinfo = malloc(sizeof(struct task_info));
		if (tinfo == NULL) {
			// out of memory
			return -1;
		}

		deserialize_int32_t(sbuf, &tinfo->node_id);
		deserialize_int32_t(sbuf, &tinfo->allowable_deadline_misses);
		deserialize_int32_t(sbuf, &tinfo->release_time);
		deserialize_int32_t(sbuf, &tinfo->period);
		deserialize_int32_t(sbuf, &tinfo->sched_policy);
		deserialize_int32_t(sbuf, &tinfo->sched_priority);
		deserialize_str(sbuf, tinfo->name);
		deserialize_int32_t(sbuf, &tinfo->pid);

		tinfo->next = sinfo->tasks;
		sinfo->tasks = tinfo;

#if 0
		printf("tinfo->pid: %u\n", tinfo->pid);
		printf("tinfo->name: %s\n", tinfo->name);
		printf("tinfo->sched_priority: %d\n", tinfo->sched_priority);
		printf("tinfo->sched_policy: %d\n", tinfo->sched_policy);
		printf("tinfo->period: %d\n", tinfo->period);
		printf("tinfo->release_time: %d\n", tinfo->release_time);
		printf("tinfo->allowable_deadline_misses: %d\n", tinfo->allowable_deadline_misses);
		printf("tinfo->node_id: %u\n", tinfo->node_id);
#endif
	}

	return 0;
}

static int get_schedinfo(sd_bus *dbus, int node_id)
{
	int ret;
	void *buf = NULL;
	size_t bufsize;
	serial_buf_t *sbuf = NULL;
	char node_str[4];

	snprintf(node_str, sizeof(node_str), "%u", node_id);

	ret = trpc_client_schedinfo(dbus, node_str, &buf, &bufsize);
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

static int sync_timer(sd_bus *dbus, int node_id, struct timespec *ts_ptr)
{
	int ret;
	int ack;
	char node_str[4];

	snprintf(node_str, sizeof(node_str), "%u", node_id);

	printf("Sync");
	fflush(stdout);
	while (1) {
		ret = trpc_client_sync(dbus, node_str, &ack, ts_ptr);
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

static void remove_tt_node(struct time_trigger *tt_node) {
	timer_delete(tt_node->timer);
	LIST_REMOVE(tt_node, entry);
	free(tt_node);
}

static int report_dmiss(sd_bus *dbus, int node_id, const char *taskname)
{
	int ret;
	char node_str[4];

	snprintf(node_str, sizeof(node_str), "%u", node_id);

	return trpc_client_dmiss(dbus, node_str, taskname);
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
			node_id = atoi(optarg);
			break;
		case 's':
			enable_sync = 1;
			break;
		case 'g':
			enable_gnuplot = 1;
			break;
		case 'h':
		default:
			fprintf(stderr, "Usage: %s [options] [host]\n"
					"Options:\n"
					"  -c <cpu_num>\tcpu affinity for timetrigger\n"
					"  -P <prio>\tRT priority (1~99) for timetrigger\n"
					"  -p <port>\tport to connect to\n"
					"  -t <seconds>\ttrace duration in seconds\n"
					"  -n <node id>\tNode ID number\n"
					"  -s\tEnable timer synchronization across multiple nodes\n"
					"  -g\tEnable saving gnuplot data file by using BPF (node<id>.gpdata)\n"
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

static void init_time_trigger_list(struct listhead *lh_ptr, int node_id)
{
	LIST_INIT(lh_ptr);

	for (struct task_info *ti = sched_info.tasks; ti; ti = ti->next) {
		struct time_trigger *tt_node;
		unsigned int pid, priority, policy;

		if (node_id != ti->node_id) {
			/* The task does not belong to this node. */
			continue;
		}

		tt_node = calloc(1, sizeof(struct time_trigger));
		memcpy(&tt_node->task, ti, sizeof(tt_node->task));

		pid = get_pid_by_name(tt_node->task.name);
		if (pid == -1) {
			printf("%s is not running !\n", tt_node->task.name);
			free(tt_node);
			continue;
		}
		priority = tt_node->task.sched_priority;
		policy = tt_node->task.sched_policy;

		set_schedattr(pid, priority, policy);

		tt_node->task.pid = pid;

		LIST_INSERT_HEAD(lh_ptr, tt_node, entry);

		bpf_add_pid(pid);
	}
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

int main(int argc, char *argv[])
{
	struct listhead lh;

	timer_t tracetimer;

	bool settimer = false;

	if (get_options(argc, argv) < 0) {
		return EXIT_FAILURE;
	}

	if (cpu != -1) {
		set_affinity(cpu);
	}
	if (prio > 0 && prio <= 99) {
		set_schedattr(getpid(), prio, SCHED_FIFO);
	}

	// Calibrate BPF ktime(CLOCK_MONOTONIC) offset to CLOCK_REALTIME
	calibrate_bpf_ktime_offset();

	// Initialze TRPC channel
	if (init_trpc(addr, port, &trpc_dbus, &trpc_event) < 0) {
		return EXIT_FAILURE;
	}

	// Get Schedule Info
	if (get_schedinfo(trpc_dbus, node_id) < 0) {
		return EXIT_FAILURE;
	}

	// Activate BPF programs
	bpf_on(sigwait_bpf_callback, schedstat_bpf_callback, (void *)&lh);

	// Initialize time_trigger linked list
	init_time_trigger_list(&lh, node_id);

	// Synchronize hrtimers across multiple nodes
	if (enable_sync && sync_timer(trpc_dbus, node_id, &starttimer_ts) < 0) {
		return EXIT_FAILURE;
	}

	// Setup and start hrtimers for tasks
	if (start_tt_timer(&lh) < 0) {
		return EXIT_FAILURE;
	}

	// Activate ftrace and its stop timer
	settimer = set_stoptracer_timer(traceduration, &tracetimer);
	tracer_on();

	struct timespec now;
	clock_gettime(clockid, &now);
	printf("tracer_on!!!: %ld\n", ts_ns(now));

	struct time_trigger *tt_p;
	LIST_FOREACH(tt_p, &lh, entry)
		printf("TT will wake up Process %s(%d) with duration %d us, release_time %d, allowable_deadline_misses: %d\n",
				tt_p->task.name, tt_p->task.pid, tt_p->task.period, tt_p->task.release_time, tt_p->task.allowable_deadline_misses);

	// The process will wait forever until it receives a signal from the handler
	while (1) {
		pause();
	}

	LIST_FOREACH(tt_p, &lh, entry) {
		bpf_del_pid(tt_p->task.pid);
		remove_tt_node(tt_p);
	}

	if (settimer) {
		timer_delete(tracetimer);
	}

	return EXIT_SUCCESS;
}
