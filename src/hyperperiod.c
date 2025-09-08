#include "internal.h"

tt_error_t hyperperiod_init(struct hyperperiod_manager *hp_mgr, const char *workload_id, uint64_t hyperperiod_us, struct context *ctx)
{
    if (!hp_mgr || !workload_id || !ctx) {
        return TT_ERROR_CONFIG;
    }

    strncpy(hp_mgr->workload_id, workload_id, sizeof(hp_mgr->workload_id) - 1);
    hp_mgr->hyperperiod_us = hyperperiod_us;
    hp_mgr->current_cycle = 0;
    hp_mgr->completed_cycles = 0;
    hp_mgr->total_deadline_misses = 0;
    hp_mgr->cycle_deadline_misses = 0;
    hp_mgr->tasks_in_hyperperiod = 0;
    hp_mgr->ctx = ctx;  // Context 포인터 설정

    // Hyperperiod start time will be set when timers actually start
    hp_mgr->hyperperiod_start_time_us = 0;

    printf("Hyperperiod Manager initialized:\n");
    printf("  Workload ID: %s\n", hp_mgr->workload_id);
    printf("  Hyperperiod: %lu us (%.3f ms)\n",
        hp_mgr->hyperperiod_us, hp_mgr->hyperperiod_us / 1000.0);
    printf("  Start time will be set when timers start\n");

    return TT_SUCCESS;
}

void hyperperiod_cycle_handler(union sigval value)
{
    struct hyperperiod_manager *hp_mgr = (struct hyperperiod_manager *)value.sival_ptr;
    struct timespec now;
    uint64_t cycle_time_us;

    clock_gettime(hp_mgr->ctx->config.clockid, &now);
    cycle_time_us = ts_us(now);

    // Update cycle information
    hp_mgr->completed_cycles++;
    hp_mgr->current_cycle = (hp_mgr->current_cycle + 1) %
        ((hp_mgr->hyperperiod_us > 0) ? 1 : 1); // Will be used for multi-cycle tracking

    write_trace_marker("Hyperperiod cycle %lu completed at %lu us, deadline misses in this cycle: %u\n",
        hp_mgr->completed_cycles, cycle_time_us, hp_mgr->cycle_deadline_misses);

#ifdef HP_DEBUG
    printf("Hyperperiod cycle %lu completed (total misses: %u, cycle misses: %u)\n",
        hp_mgr->completed_cycles, hp_mgr->total_deadline_misses, hp_mgr->cycle_deadline_misses);
#endif

    // Reset cycle-specific counters
    hp_mgr->cycle_deadline_misses = 0;

    // Log statistics every interval
    if (hp_mgr->completed_cycles % STATISTICS_LOG_INTERVAL == 0) {
        hyperperiod_log_statistics(hp_mgr);
    }
}

uint64_t hyperperiod_get_relative_time_us(const struct hyperperiod_manager *hp_mgr)
{
    struct timespec now;
    clock_gettime(hp_mgr->ctx->config.clockid, &now);

    uint64_t current_time_us = ts_us(now);

    // If hyperperiod hasn't started yet, return 0
    if (hp_mgr->hyperperiod_start_time_us == 0) {
        return 0;
    }

    uint64_t elapsed_us = current_time_us - hp_mgr->hyperperiod_start_time_us;

    // Return position within current hyperperiod
    return elapsed_us % hp_mgr->hyperperiod_us;
}

void hyperperiod_log_statistics(const struct hyperperiod_manager *hp_mgr)
{
    double miss_rate = hp_mgr->completed_cycles > 0 ?
        (double)hp_mgr->total_deadline_misses / hp_mgr->completed_cycles : 0.0;

    printf("\n=== Hyperperiod Statistics ===\n");
    printf("Workload: %s\n", hp_mgr->workload_id);
    printf("Completed cycles: %lu\n", hp_mgr->completed_cycles);
    printf("Hyperperiod length: %lu us\n", hp_mgr->hyperperiod_us);
    printf("Total deadline misses: %u\n", hp_mgr->total_deadline_misses);
    printf("Miss rate per cycle: %.4f\n", miss_rate);
    printf("Tasks in hyperperiod: %u\n", hp_mgr->tasks_in_hyperperiod);
    printf("==============================\n\n");
}

tt_error_t hyperperiod_start_timer(struct context *ctx)
{
    struct itimerspec its;
    struct sigevent sev;

    if (ctx->hp_manager.hyperperiod_us == 0) {
        printf("Warning: Hyperperiod not set, skipping hyperperiod timer\n");
        return TT_SUCCESS;
    }

    // Set hyperperiod start time to match with task timers
    ctx->hp_manager.hyperperiod_start_ts = ctx->runtime.starttimer_ts;
    ctx->hp_manager.hyperperiod_start_time_us = ts_us(ctx->hp_manager.hyperperiod_start_ts);

    printf("Hyperperiod start time set: %lu us\n", ctx->hp_manager.hyperperiod_start_time_us);

    memset(&sev, 0, sizeof(sev));
    memset(&its, 0, sizeof(its));

    sev.sigev_notify = SIGEV_THREAD;
    sev.sigev_notify_function = hyperperiod_cycle_handler;
    sev.sigev_value.sival_ptr = &ctx->hp_manager;

    // Set hyperperiod cycle interval
    its.it_value.tv_sec = ctx->runtime.starttimer_ts.tv_sec + (ctx->hp_manager.hyperperiod_us / USEC_PER_SEC);
    its.it_value.tv_nsec = ctx->runtime.starttimer_ts.tv_nsec + (ctx->hp_manager.hyperperiod_us % USEC_PER_SEC) * NSEC_PER_USEC;
    if (its.it_value.tv_nsec >= NSEC_PER_SEC) {
        its.it_value.tv_sec++;
        its.it_value.tv_nsec -= NSEC_PER_SEC;
    }

    its.it_interval.tv_sec = ctx->hp_manager.hyperperiod_us / USEC_PER_SEC;
    its.it_interval.tv_nsec = (ctx->hp_manager.hyperperiod_us % USEC_PER_SEC) * NSEC_PER_USEC;

    printf("Starting hyperperiod timer: %lu us interval (%lds %ldns)\n",
        ctx->hp_manager.hyperperiod_us, its.it_interval.tv_sec, its.it_interval.tv_nsec);

    if (timer_create(ctx->config.clockid, &sev, &ctx->hp_manager.hyperperiod_timer)) {
        perror("Failed to create hyperperiod timer");
        return TT_ERROR_TIMER;
    }

    if (timer_settime(ctx->hp_manager.hyperperiod_timer, TIMER_ABSTIME, &its, NULL)) {
        perror("Failed to start hyperperiod timer");
        return TT_ERROR_TIMER;
    }

    return TT_SUCCESS;
}
