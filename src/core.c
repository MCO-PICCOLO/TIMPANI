#include "internal.h"

// 전역 변수들 (리팩토링 후 제거 예정)
extern struct hyperperiod_manager hp_manager;
extern sd_bus *trpc_dbus;
extern char node_id[TINFO_NODEID_MAX];
extern clockid_t clockid;

// BPF 콜백 함수들
#ifdef CONFIG_TRACE_BPF
static uint64_t bpf_ktime_off;

static void calibrate_bpf_ktime_offset_internal(void)
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

int sigwait_bpf_callback(void *ctx, void *data, size_t len)
{
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

    return 0;
}
#else
static inline void calibrate_bpf_ktime_offset_internal(void) {}
static inline uint64_t bpf_ktime_to_real(uint64_t bpf_ts) { return bpf_ts; }
int sigwait_bpf_callback(void *ctx, void *data, size_t len) { return 0; }
#endif

#ifdef CONFIG_TRACE_BPF_EVENT
int schedstat_bpf_callback(void *ctx, void *data, size_t len)
{
    // BPF 이벤트 콜백 구현
    return 0;
}
#else
int schedstat_bpf_callback(void *ctx, void *data, size_t len) { return 0; }
#endif

void calibrate_bpf_ktime_offset(void)
{
    calibrate_bpf_ktime_offset_internal();
}

// 타이머 핸들러 함수
void timer_handler(union sigval value)
{
    struct time_trigger *tt_node = (struct time_trigger *)value.sival_ptr;
    struct task_info *task = (struct task_info *)&tt_node->task;
    struct context *ctx = tt_node->ctx;  // context 가져오기
    struct timespec before, after;
    uint64_t hyperperiod_position_us;

    clock_gettime(ctx->config.clockid, &before);

    // Calculate position within hyperperiod
    hyperperiod_position_us = hyperperiod_get_relative_time_us(&ctx->hp_manager);

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
            report_dmiss(ctx->comm.dbus, ctx->config.node_id, task->name);
        // Check if this task meets the deadline
        } else if (tt_node->sigwait_ts > deadline_ns) {
            printf("!!! DEADLINE MISS %s(%d): %lu > deadline %lu !!!\n",
                task->name, task->pid, tt_node->sigwait_ts, deadline_ns);
            write_trace_marker("%s: Deadline miss: %lu diff\n",
                task->name, tt_node->sigwait_ts - deadline_ns);
            ctx->hp_manager.total_deadline_misses++;
            ctx->hp_manager.cycle_deadline_misses++;
            report_dmiss(ctx->comm.dbus, ctx->config.node_id, task->name);
        // Check if this task is stuck at kernel sigwait syscall handler
        } else if (tt_node->sigwait_ts == tt_node->sigwait_ts_prev) {
            printf("!!! DEADLINE MISS: STUCK AT KERNEL %s(%d): %lu & deadline %lu !!!\n",
                task->name, task->pid, tt_node->sigwait_ts, deadline_ns);
            write_trace_marker("%s: Deadline miss: %lu diff\n",
                task->name, tt_node->sigwait_ts - deadline_ns);
            ctx->hp_manager.total_deadline_misses++;
            ctx->hp_manager.cycle_deadline_misses++;
            report_dmiss(ctx->comm.dbus, ctx->config.node_id, task->name);
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
        ctx->runtime.starttimer_ts.tv_nsec += TIMER_INCREMENT_NS;
    }

    LIST_FOREACH(tt_p, &ctx->runtime.tt_list, entry) {
        struct itimerspec its;
        struct sigevent sev;

        memset(&sev, 0, sizeof(sev));
        memset(&its, 0, sizeof(its));

        sev.sigev_notify = SIGEV_THREAD;
        sev.sigev_notify_function = timer_handler;
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

bool set_stoptracer_timer(int duration, timer_t *timer)
{
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

    // 임시로 전역 변수 사용 (나중에 context로 변경)
    extern struct timespec starttimer_ts;
    its.it_value.tv_sec = starttimer_ts.tv_sec + duration;
    its.it_value.tv_nsec = starttimer_ts.tv_nsec;
    its.it_interval.tv_sec = duration;
    its.it_interval.tv_nsec = 0;

    if (timer_create(CLOCK_REALTIME, &sev, timer) == -1) {
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
bool set_stoptracer_timer(int duration, timer_t *timer)
{
    return false;
}
#endif
