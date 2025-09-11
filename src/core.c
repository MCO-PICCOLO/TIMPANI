#include "internal.h"

// BPF 콜백 함수들
#ifdef CONFIG_TRACE_BPF
static uint64_t bpf_ktime_off;

static tt_error_t calibrate_bpf_ktime_offset_internal(void)
{
    int i;
    struct timespec t1, t2, t3;
    uint64_t best_delta = UINT64_MAX, delta, ts;

    // 더 정확한 보정을 위해 반복 횟수 증가
    for (i = 0; i < 20; i++) {
        if (clock_gettime(CLOCK_REALTIME, &t1) < 0) {
            TT_LOG_ERROR("Failed to get CLOCK_REALTIME");
            return TT_ERROR_TIMER;
        }
        if (clock_gettime(CLOCK_MONOTONIC, &t2) < 0) {
            TT_LOG_ERROR("Failed to get CLOCK_MONOTONIC");
            return TT_ERROR_TIMER;
        }
        if (clock_gettime(CLOCK_REALTIME, &t3) < 0) {
            TT_LOG_ERROR("Failed to get CLOCK_REALTIME");
            return TT_ERROR_TIMER;
        }

        delta = tt_timespec_to_ns(&t3) - tt_timespec_to_ns(&t1);
        ts = (tt_timespec_to_ns(&t3) + tt_timespec_to_ns(&t1)) / 2;

        if (delta < best_delta) {
            best_delta = delta;
            bpf_ktime_off = ts - tt_timespec_to_ns(&t2);
        }
    }
    return TT_SUCCESS;
}

static inline uint64_t bpf_ktime_to_real(uint64_t bpf_ts)
{
    return bpf_ktime_off + bpf_ts;
}

tt_error_t handle_sigwait_bpf_event(void *ctx, void *data, size_t len)
{
    // 매개변수 검증
    if (!ctx || !data || len < sizeof(struct sigwait_event)) {
        return TT_ERROR_BPF;
    }

    struct sigwait_event *e = (struct sigwait_event *)data;
    struct context *ctx_struct = (struct context *)ctx;
    struct listhead *lh_p = (struct listhead *)&ctx_struct->runtime.tt_list;
    struct time_trigger *tt_p;

    LIST_FOREACH(tt_p, lh_p, entry) {
        if (tt_p->task.pid == e->pid) {
            tt_p->sigwait_ts = bpf_ktime_to_real(e->timestamp);
            tt_p->sigwait_enter = e->enter;
            break;
        }
    }

    return TT_SUCCESS;
}
#else
static inline tt_error_t calibrate_bpf_ktime_offset_internal(void) { return TT_SUCCESS; }
static inline uint64_t bpf_ktime_to_real(uint64_t bpf_ts) { return bpf_ts; }
tt_error_t handle_sigwait_bpf_event(void *ctx, void *data, size_t len) { return TT_SUCCESS; }
#endif

#ifdef CONFIG_TRACE_BPF_EVENT
#define SCHEDSTAT_NS_TO_US(ns) (((ns) + (1000 - 1)) / 1000)

static void write_schedstat(struct context *ctx, struct schedstat_event *e, const char *tname)
{
	static FILE *file;
	uint64_t ts_wakeup, ts_start, ts_stop;

	/* 플롯 기능이 비활성화된 경우 */
	if (!ctx->config.enable_plot) {
		if (file) {
			fclose(file);
			file = NULL;
		}
		return;
	}

	// Check if the file is not opened yet
	if (!file) {
		char fname[128];

		snprintf(fname, sizeof(fname), "%s.gpdata", ctx->config.node_id);
		file = fopen(fname, "w+");
		if (file == NULL) {
			ctx->config.enable_plot = 0;
			return;
		}
	}

	// Convert monotonic ktime to realtime
	ts_wakeup = bpf_ktime_to_real(e->ts_wakeup);
	ts_start = bpf_ktime_to_real(e->ts_start);
	ts_stop = bpf_ktime_to_real(e->ts_stop);

        // Convert ns timestamps to us timestamps in a round up manner
	ts_wakeup = SCHEDSTAT_NS_TO_US(ts_wakeup);
	ts_start = SCHEDSTAT_NS_TO_US(ts_start);
	ts_stop = SCHEDSTAT_NS_TO_US(ts_stop);

	// Column formatting:
	// task event ignored resource priority activate start stop ignored
	// NOTE: Compatible with legacy gnuplot script
	fprintf(file, "%-16s 0 0 %s-C%d 0 %lu %lu %lu 0\n",
		tname, ctx->config.node_id, e->cpu, ts_wakeup, ts_start, ts_stop);
}

tt_error_t handle_schedstat_bpf_event(void *ctx, void *data, size_t len)
{
    // 매개변수 검증
    if (!ctx || !data || len == 0) {
        return TT_ERROR_BPF;
    }

    struct schedstat_event *e = (struct schedstat_event *)data;
    struct context *ctx_struct = (struct context *)ctx;
    struct listhead *lh_p = (struct listhead *)&ctx_struct->runtime.tt_list;
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

    if (ctx_struct->config.enable_plot && tt_p != NULL)
        write_schedstat(ctx_struct, e, tt_p->task.name);

    return TT_SUCCESS;
}
#else
tt_error_t handle_schedstat_bpf_event(void *ctx, void *data, size_t len) { return TT_SUCCESS; }
#endif

tt_error_t calibrate_bpf_time_offset(void)
{
    return calibrate_bpf_ktime_offset_internal();
}

// 타이머 핸들러 함수
void timer_expired_handler(union sigval value)
{
    struct time_trigger *tt_node = (struct time_trigger *)value.sival_ptr;

    // 매개변수 검증
    if (!tt_node || !tt_node->ctx) {
        return;
    }

    struct task_info *task = (struct task_info *)&tt_node->task;
    struct context *ctx = tt_node->ctx;  // context 가져오기
    struct timespec before, after;
    uint64_t hyperperiod_position_us;

    clock_gettime(ctx->config.clockid, &before);

    // Calculate position within hyperperiod
    hyperperiod_position_us = get_hyperperiod_relative_time(&ctx->hp_manager);

    TT_LOG_DEBUG("%s: Timer expired: now: %lld, diff: %lld, hyperperiod_pos: %lu us",
            task->name, ts_ns(before), ts_diff(before, tt_node->prev_timer), hyperperiod_position_us);

    // If a task has its own release time, do nanosleep
    if (task->release_time) {
        struct timespec ts = us_ts(task->release_time);
        clock_nanosleep(ctx->config.clockid, 0, &ts, NULL);
    }

#ifdef CONFIG_TRACE_BPF
    /* Check whether there is a deadline miss or not */
    if (tt_node->sigwait_ts) {
        uint64_t deadline_ns = ts_ns(before);

        // Check if this task is still running
        if (!tt_node->sigwait_enter) {
            printf("!!! DEADLINE MISS: STILL OVERRUN %s(%d): deadline %lu !!!\n",
                task->name, task->pid, deadline_ns);
            ctx->hp_manager.total_deadline_misses++;
            ctx->hp_manager.cycle_deadline_misses++;
            if (report_deadline_miss(ctx, task->name) != TT_SUCCESS) {
                TT_LOG_WARNING("Failed to report deadline miss for task %s", task->name);
            }
        // Check if this task meets the deadline
        } else if (tt_node->sigwait_ts > deadline_ns) {
            printf("!!! DEADLINE MISS %s(%d): %lu > deadline %lu !!!\n",
                task->name, task->pid, tt_node->sigwait_ts, deadline_ns);
            TT_LOG_ERROR("%s: Deadline miss: %lu diff",
                task->name, tt_node->sigwait_ts - deadline_ns);
            ctx->hp_manager.total_deadline_misses++;
            ctx->hp_manager.cycle_deadline_misses++;
            if (report_deadline_miss(ctx, task->name) != TT_SUCCESS) {
                TT_LOG_WARNING("Failed to report deadline miss for task %s", task->name);
            }
        // Check if this task is stuck at kernel sigwait syscall handler
        } else if (tt_node->sigwait_ts == tt_node->sigwait_ts_prev) {
            printf("!!! DEADLINE MISS: STUCK AT KERNEL %s(%d): %lu & deadline %lu !!!\n",
                task->name, task->pid, tt_node->sigwait_ts, deadline_ns);
            TT_LOG_ERROR("%s: Deadline miss (stuck): %lu diff",
                task->name, tt_node->sigwait_ts - deadline_ns);
            ctx->hp_manager.total_deadline_misses++;
            ctx->hp_manager.cycle_deadline_misses++;
            if (report_deadline_miss(ctx, task->name) != TT_SUCCESS) {
                TT_LOG_WARNING("Failed to report deadline miss for task %s", task->name);
            }
        }

        tt_node->sigwait_ts_prev = tt_node->sigwait_ts;
    }
#endif

    clock_gettime(ctx->config.clockid, &after);
    TT_LOG_DEBUG("%s: Send signal(%d) to %d: now: %lld, lat between timer and signal: %lld us",
            task->name, SIGNO_TT, task->pid, ts_ns(after), ( ts_diff(after, before) / NSEC_PER_USEC ));

    // Send the signal to the target process
    ttsched_error_t signal_result = send_signal_pidfd(task->pidfd, SIGNO_TT);
    if (signal_result != TTSCHED_SUCCESS) {
        TT_LOG_ERROR("Failed to send signal via pidfd to %s (PID %d): %s",
            task->name, task->pid, ttsched_error_string(signal_result));
        // TODO: check if the process is still alive
    }

    tt_node->prev_timer = before;
}

tt_error_t start_timers(struct context *ctx)
{
    struct time_trigger *tt_p;

    if (!ctx->config.enable_sync) {
        /* No synchronization across multiple nodes */
        clock_gettime(ctx->config.clockid, &ctx->runtime.starttimer_ts);
        ctx->runtime.starttimer_ts.tv_nsec += TT_TIMER_INCREMENT_NS;
    }

    LIST_FOREACH(tt_p, &ctx->runtime.tt_list, entry) {
        struct itimerspec its;
        struct sigevent sev;

        memset(&sev, 0, sizeof(sev));
        memset(&its, 0, sizeof(its));

        sev.sigev_notify = SIGEV_THREAD;
        sev.sigev_notify_function = timer_expired_handler;
        sev.sigev_value.sival_ptr = tt_p;

        its.it_value.tv_sec = ctx->runtime.starttimer_ts.tv_sec;
        its.it_value.tv_nsec = ctx->runtime.starttimer_ts.tv_nsec;
        its.it_interval.tv_sec = tt_p->task.period / USEC_PER_SEC;
        its.it_interval.tv_nsec = tt_p->task.period % USEC_PER_SEC * NSEC_PER_USEC;

        printf("%s(%d) period: %d starttimer_ts: %ld interval: %lds %ldns\n",
                tt_p->task.name, tt_p->task.pid,
                tt_p->task.period, ts_ns(its.it_value),
                its.it_interval.tv_sec, its.it_interval.tv_nsec);

        if (timer_create(ctx->config.clockid, &sev, &tt_p->timer)) {
            perror("Failed to create timer");
            return TT_ERROR_TIMER;
        }

        if (timer_settime(tt_p->timer, TIMER_ABSTIME, &its, NULL)) {
            perror("Failed to start timer");
            return TT_ERROR_TIMER;
        }
    }

    return TT_SUCCESS;
}

tt_error_t epoll_loop(struct context *ctx)
{
    int efd;
    efd = epoll_create1(0);
    if (efd < 0) {
        perror("epoll_create failed");
        return TT_ERROR_TIMER;
    }

    struct time_trigger *tt_p;
    LIST_FOREACH(tt_p, &ctx->runtime.tt_list, entry) {
        printf("TT will wake up Process %s(%d) with duration %d us, release_time %d, allowable_deadline_misses: %d\n",
            tt_p->task.name, tt_p->task.pid, tt_p->task.period, tt_p->task.release_time, tt_p->task.allowable_deadline_misses);

        struct epoll_event event;
        event.data.fd = tt_p->task.pidfd;
        event.events = EPOLLIN;
        if (epoll_ctl(efd, EPOLL_CTL_ADD, tt_p->task.pidfd, &event) < 0) {
            perror("epoll_ctl failed");
            close(efd);
            return TT_ERROR_TIMER;
        }
    }

    // Main execution loop with graceful shutdown support
    TT_LOG_INFO("Time Trigger started. Press Ctrl+C to stop gracefully.");
    while (!ctx->runtime.shutdown_requested) {
        struct epoll_event events[1];
        int count = epoll_wait(efd, events, 1, -1);

        if (count < 0) {
            if (errno == EINTR) {
                // Signal received - just continue, shutdown_requested will be checked at loop condition
                continue;
            }
            perror("epoll_wait failed");
            close(efd);
            return TT_ERROR_TIMER;
        }

        // Handle process termination events
        struct time_trigger *tt_p;
        LIST_FOREACH(tt_p, &ctx->runtime.tt_list, entry) {
            if (tt_p->task.pidfd == events[0].data.fd) {
                // Handle task termination
                TT_LOG_INFO("Task %s(%d) terminated", tt_p->task.name, tt_p->task.pid);
                epoll_ctl(efd, EPOLL_CTL_DEL, tt_p->task.pidfd, NULL);
                // TODO: Recovery from task termination
                break;
            }
        }
    }

    close(efd);
    return TT_SUCCESS;
}
