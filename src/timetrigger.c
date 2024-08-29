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

#include "schedinfo.h"
#include "timetrigger.h"

#ifdef CONFIG_TRACE_BPF
#include "trace_bpf.h"
#endif

struct time_trigger {
	timer_t timer;
	struct task_info task;
#ifdef CONFIG_TRACE_BPF
	uint64_t sigwait_ts;
	uint8_t sigwait_enter;
#endif
	LIST_ENTRY(time_trigger) entry;
};

LIST_HEAD(listhead, time_trigger);

// example tasks for time_trigger
struct task_info ex_tasks[3] = { { 0, {}, 10000, 1000, NULL },
				{ 0, {}, 50000, 5000, NULL },
				{ 0, {}, 20000, 2000, NULL } };

static inline uint64_t timespec_to_ns(const struct timespec *ts)
{
	return ((uint64_t) ts->tv_sec * NSEC_PER_SEC) + ts->tv_nsec;
}

static inline uint64_t timespec_to_us(const struct timespec *ts)
{
	return ((uint64_t) ts->tv_sec * USEC_PER_SEC) + ts->tv_nsec / NSEC_PER_USEC;
}

static inline struct timespec ns_to_timespec(const uint64_t ns)
{
	struct timespec ts;
	ts.tv_sec = ns / NSEC_PER_SEC;
	ts.tv_nsec = ns % NSEC_PER_SEC;
	return ts;
}

static inline struct timespec us_to_timespec(const uint64_t us)
{
	struct timespec ts;
	ts.tv_sec = us / USEC_PER_SEC;
	ts.tv_nsec = (us % USEC_PER_SEC) * NSEC_PER_USEC;
	return ts;
}

// TT Handler function executed upon timer expiration based on each period
static void tt_timer(union sigval value) {
	struct time_trigger *tt_node = (struct time_trigger *)value.sival_ptr;
	struct task_info *task = (struct task_info *)&tt_node->task;
	struct timespec before, after;

	clock_gettime(CLOCK_MONOTONIC, &before);
	write_trace_marker("Timer expired: now: %lld \n", ts_ns(before));

	// If a task has its own release time, do nanosleep
	if (task->release_time) {
		struct timespec ts;
		ts.tv_sec = task->release_time / USEC_PER_SEC;
		ts.tv_nsec = task->release_time % USEC_PER_SEC * NSEC_PER_USEC;
		clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL);
	}

#ifdef CONFIG_TRACE_BPF
	/* Check whether there is a deadline miss or not */
	if (tt_node->sigwait_ts) {
		uint64_t deadline_ns = timespec_to_ns(&before);

		// Check if this task is still running
		if (!tt_node->sigwait_enter) {
			printf("!!! STILL OVERRUN %s(%d): %lu !!!\n", task->name, task->pid, deadline_ns);
		// Check if this task meets the deadline
		} else if (tt_node->sigwait_ts > deadline_ns) {
			printf("!!! DEADLINE MISS %s(%d): %lu > %lu !!!\n", task->name, task->pid, tt_node->sigwait_ts, deadline_ns);
		}
	}
#endif

	clock_gettime(CLOCK_MONOTONIC, &after);
	write_trace_marker("Send signal(%d) to %d: now: %lld, lat between timer and signal: %lld us \n",
			SIGNO_TT, task->pid, ts_ns(after), ( diff( ts_ns(after), ts_ns(before) ) / NSEC_PER_USEC ));

	// Send the signal to the target process
	kill(task->pid, SIGNO_TT);
}

static void sighan_stoptracer(int signo, siginfo_t *info, void *context) {
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	write_trace_marker("Stop Tracer: %lld \n", ts_ns(now));
	tracer_off();
	signal(signo, SIG_IGN);
}

static bool set_stoptracer_timer(int duration, timer_t *timer) {
	struct sigevent sev = {};
	struct itimerspec its = {};

	sev.sigev_notify = SIGEV_SIGNAL;
	sev.sigev_signo = SIGNO_STOPTRACER;

	its.it_interval.tv_sec = duration;
	its.it_interval.tv_nsec = 0;
	its.it_value.tv_sec = duration;
	its.it_value.tv_nsec = 0;

	if (timer_create(CLOCK_MONOTONIC, &sev, timer) == -1) {
		perror("Failed to create timer");
		return false;
	}

	if (timer_settime(*timer, 0, &its, NULL) == -1) {
		perror("Failed to set timer period");
		return false;
	}
	return true;
}

#ifdef CONFIG_TRACE_BPF
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
			tt_p->sigwait_ts = e->timestamp;
			tt_p->sigwait_enter = e->enter;
			break;
		}
	}

	return 0;
}
#endif

int main(int argc, char *argv[]) {
	struct sigevent sev;
	struct timespec starttimer_ts;
	struct itimerspec its;
	struct listhead lh;

	timer_t tracetimer;

	bool settimer = false;
	int traceduration = 10;		// trace in 10 seconds

	int cnt_ex_tasks = sizeof(ex_tasks)/sizeof(struct task_info);

	int cpu = 3;
	set_affinity(cpu);
	set_schedattr(getpid(), 81, SCHED_FIFO);

	if ((argc-1) != cnt_ex_tasks) {
		printf("Arguments should be %d, \
			because example tasks are only %d!!!\n",
			cnt_ex_tasks, cnt_ex_tasks);
		return EXIT_FAILURE;
	}

	LIST_INIT(&lh);

	settimer = set_stoptracer_timer(traceduration, &tracetimer);
#ifdef CONFIG_TRACE_BPF
	tracer_on(sigwait_bpf_callback, (void *)&lh);
#else
	tracer_on();
#endif

	clock_gettime(CLOCK_MONOTONIC, &starttimer_ts);

	for (int i = 0; i < cnt_ex_tasks; i++) {
		struct time_trigger *tt_node = calloc(1, sizeof(struct time_trigger));

		memset(&sev, 0, sizeof(sev));
		memset(&its, 0, sizeof(its));

		tt_node->task = ex_tasks[i];

		// TODO: PIDs from arguments are used for example
		// Need to change to get data from sched_info of Receiver
		tt_node->task.pid = atol(argv[i+1]);

		get_process_name_by_pid(tt_node->task.pid, tt_node->task.name);

		sev.sigev_notify = SIGEV_THREAD;
		sev.sigev_notify_function = tt_timer;

		sev.sigev_value.sival_ptr = tt_node;

		its.it_value.tv_sec = starttimer_ts.tv_sec;
		its.it_value.tv_nsec = starttimer_ts.tv_nsec + 5000000;
		its.it_interval.tv_sec = tt_node->task.period / USEC_PER_SEC;
		its.it_interval.tv_nsec = tt_node->task.period % USEC_PER_SEC * NSEC_PER_USEC;

		printf("%s(%d) period: %d starttimer_ts: %ld interval: %lds %ldns\n",
				tt_node->task.name, tt_node->task.pid,
				tt_node->task.period, ts_ns(its.it_value),
				its.it_interval.tv_sec, its.it_interval.tv_nsec);

		if (timer_create(CLOCK_MONOTONIC, &sev, &tt_node->timer)) {
			perror("Failed to create timer");
			return EXIT_FAILURE;
		}

		if (timer_settime(tt_node->timer, TIMER_ABSTIME, &its, NULL)) {
			perror("Failed to start timer");
			return EXIT_FAILURE;
		}

		LIST_INSERT_HEAD(&lh, tt_node, entry);

		tracer_add_pid(tt_node->task.pid);
	}

	struct time_trigger *tt_p;
	LIST_FOREACH(tt_p, &lh, entry)
		printf("TT will wake up Process %s(%d) with duration %d us and release_time %d\n",
				tt_p->task.name, tt_p->task.pid, tt_p->task.period, tt_p->task.release_time);

	struct sigaction sa = {};
	sa.sa_flags = SA_SIGINFO;
	sa.sa_sigaction = &sighan_stoptracer;
	if (sigaction(SIGNO_STOPTRACER, &sa, NULL) == -1) {
		perror("Failed to set up signal handler");
		return EXIT_FAILURE;
	}

	// The process will wait forever until it receives a signal from the handler
	while (1) {
		pause();
	}

	LIST_FOREACH(tt_p, &lh, entry) {
		timer_delete(tt_p->timer);
		free(tt_p);
	}

	if (settimer) {
		timer_delete(tracetimer);
	}

	return EXIT_SUCCESS;
}
