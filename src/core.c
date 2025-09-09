#include "internal.h"

// BPF 콜백 함수들
#ifdef CONFIG_TRACE_BPF
static uint64_t bpf_ktime_off;

static void calibrate_bpf_ktime_offset_internal(void)
{
    int i;
    struct timespec t1, t2, t3;
    uint64_t best_delta = UINT64_MAX, delta, ts;

    // 더 정확한 보정을 위해 반복 횟수 증가
    for (i = 0; i < 20; i++) {
        clock_gettime(CLOCK_REALTIME, &t1);
        clock_gettime(CLOCK_MONOTONIC, &t2);
        clock_gettime(CLOCK_REALTIME, &t3);

        delta = tt_timespec_to_ns(&t3) - tt_timespec_to_ns(&t1);
        ts = (tt_timespec_to_ns(&t3) + tt_timespec_to_ns(&t1)) / 2;

        if (delta < best_delta) {
            best_delta = delta;
            bpf_ktime_off = ts - tt_timespec_to_ns(&t2);
        }
    }
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
    struct listhead *lh_p = (struct listhead *)ctx;
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
static inline void calibrate_bpf_ktime_offset_internal(void) {}
static inline uint64_t bpf_ktime_to_real(uint64_t bpf_ts) { return bpf_ts; }
tt_error_t handle_sigwait_bpf_event(void *ctx, void *data, size_t len) { return TT_SUCCESS; }
#endif

#ifdef CONFIG_TRACE_BPF_EVENT
tt_error_t handle_schedstat_bpf_event(void *ctx, void *data, size_t len)
{
    // 매개변수 검증
    if (!ctx || !data || len == 0) {
        return TT_ERROR_BPF;
    }

    // BPF 이벤트 콜백 구현
    return TT_SUCCESS;
}
#else
tt_error_t handle_schedstat_bpf_event(void *ctx, void *data, size_t len) { return TT_SUCCESS; }
#endif

void calibrate_bpf_time_offset(void)
{
    calibrate_bpf_ktime_offset_internal();
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

    write_trace_marker("%s: Timer expired: now: %lld, diff: %lld, hyperperiod_pos: %lu us\n",
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
            report_deadline_miss(ctx, task->name);
        // Check if this task meets the deadline
        } else if (tt_node->sigwait_ts > deadline_ns) {
            printf("!!! DEADLINE MISS %s(%d): %lu > deadline %lu !!!\n",
                task->name, task->pid, tt_node->sigwait_ts, deadline_ns);
            write_trace_marker("%s: Deadline miss: %lu diff\n",
                task->name, tt_node->sigwait_ts - deadline_ns);
            ctx->hp_manager.total_deadline_misses++;
            ctx->hp_manager.cycle_deadline_misses++;
            report_deadline_miss(ctx, task->name);
        // Check if this task is stuck at kernel sigwait syscall handler
        } else if (tt_node->sigwait_ts == tt_node->sigwait_ts_prev) {
            printf("!!! DEADLINE MISS: STUCK AT KERNEL %s(%d): %lu & deadline %lu !!!\n",
                task->name, task->pid, tt_node->sigwait_ts, deadline_ns);
            write_trace_marker("%s: Deadline miss: %lu diff\n",
                task->name, tt_node->sigwait_ts - deadline_ns);
            ctx->hp_manager.total_deadline_misses++;
            ctx->hp_manager.cycle_deadline_misses++;
            report_deadline_miss(ctx, task->name);
        }

        tt_node->sigwait_ts_prev = tt_node->sigwait_ts;
    }
#endif

    clock_gettime(ctx->config.clockid, &after);
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
    printf("Time Trigger started. Press Ctrl+C to stop gracefully.\n");
    while (!ctx->runtime.shutdown_requested) {
        struct epoll_event events[1];
        int count = epoll_wait(efd, events, 1, -1);
        if (count < 0) {
            if (errno == EINTR) {
                // Ctrl+C pressed or a signal received
                break;
            }
            perror("epoll_wait failed");
            close(efd);
            return TT_ERROR_TIMER;
        }

        LIST_FOREACH(tt_p, &ctx->runtime.tt_list, entry) {
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
    return TT_SUCCESS;
}

#if defined(CONFIG_TRACE_EVENT) || defined(CONFIG_TRACE_BPF_EVENT)
static void sighan_stoptracer(int signo, siginfo_t *info, void *context)
{
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    write_trace_marker("Stop Tracer: %lld \n", ts_ns(now));
    tracer_off();
    printf("tracer_off!!!: %ld\n", ts_ns(now));
    signal(signo, SIG_IGN);
}

tt_error_t setup_trace_stop_timer(struct context *ctx, int duration, timer_t *timer)
{
    struct sigevent sev = {};
    struct itimerspec its = {};
    struct sigaction sa = {};

    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = &sighan_stoptracer;
    if (sigaction(SIGNO_STOPTRACER, &sa, NULL) == -1) {
        perror("Failed to set up signal handler");
        return TT_ERROR_SIGNAL;
    }

    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIGNO_STOPTRACER;

    // context를 통해 starttimer_ts에 접근
    its.it_value.tv_sec = ctx->runtime.starttimer_ts.tv_sec + duration;
    its.it_value.tv_nsec = ctx->runtime.starttimer_ts.tv_nsec;
    its.it_interval.tv_sec = duration;
    its.it_interval.tv_nsec = 0;

    if (timer_create(ctx->config.clockid, &sev, timer) == -1) {
        perror("Failed to create timer");
        return TT_ERROR_TIMER;
    }

    if (timer_settime(*timer, TIMER_ABSTIME, &its, NULL) == -1) {
        perror("Failed to set timer period");
        return TT_ERROR_TIMER;
    }

    return TT_SUCCESS;
}
#else
tt_error_t setup_trace_stop_timer(struct context *ctx, int duration, timer_t *timer)
{
    return TT_SUCCESS;  // 추적 기능이 비활성화된 경우에도 성공으로 처리
}
#endif
