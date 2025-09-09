#define _GNU_SOURCE
#include "internal.h"

static tt_error_t initialize(struct context *ctx);
static tt_error_t run(struct context *ctx);

int main(int argc, char *argv[])
{
    struct context ctx = {0};
    tt_error_t ret;

    // 설정 파싱
    ret = parse_config(argc, argv, &ctx);
    if (ret != TT_SUCCESS) {
        fprintf(stderr, "Configuration error: %s\n", tt_error_string(ret));
        return EXIT_FAILURE;
    }

    // 초기화
    ret = initialize(&ctx);
    if (ret != TT_SUCCESS) {
        fprintf(stderr, "Initialization failed: %s\n", tt_error_string(ret));
        goto cleanup;
    }

    // 실행
    ret = run(&ctx);
    if (ret != TT_SUCCESS) {
        fprintf(stderr, "Runtime error: %s\n", tt_error_string(ret));
    }

cleanup:
    cleanup_context(&ctx);
    return (ret == TT_SUCCESS) ? EXIT_SUCCESS : EXIT_FAILURE;
}

static tt_error_t initialize(struct context *ctx)
{
    pid_t pid = getpid();

    // 시그널 핸들러 설정
    if (setup_signal_handlers(ctx) != TT_SUCCESS) {
        return TT_ERROR_SIGNAL;
    }

    // 프로세스 우선순위 설정
    if (ctx->config.cpu != -1) {
        set_affinity(pid, ctx->config.cpu);
    }
    if (ctx->config.prio > 0 && ctx->config.prio <= 99) {
        set_schedattr(pid, ctx->config.prio, SCHED_FIFO);
    }

    // BPF 초기화
    calibrate_bpf_time_offset();

    // TRPC 초기화 및 스케줄 정보 획득
    if (init_trpc(ctx) != TT_SUCCESS) {
        fprintf(stderr, "Failed to initialize TRPC and get schedule info\n");
        return TT_ERROR_NETWORK;
    }

    // BPF 활성화
    bpf_on(handle_sigwait_bpf_event, handle_schedstat_bpf_event, (void *)&ctx->runtime.tt_list);

    // 태스크 리스트 초기화
    if (init_task_list(ctx) != TT_SUCCESS) {
        fprintf(stderr, "Failed to initialize time trigger list\n");
        return TT_ERROR_CONFIG;
    }

    return TT_SUCCESS;
}

static tt_error_t run(struct context *ctx)
{
    timer_t tracetimer;
    bool settimer = false;

    // 타이머 동기화
    if (sync_timer_with_server(ctx) != TT_SUCCESS) {
        fprintf(stderr, "Failed to synchronize timers\n");
        return TT_ERROR_NETWORK;
    }

    // 태스크 타이머 시작
    if (start_timers(ctx) != TT_SUCCESS) {
        fprintf(stderr, "Failed to start timers\n");
        return TT_ERROR_TIMER;
    }

    // 하이퍼피리어드 타이머 시작
    if (start_hyperperiod_timer(ctx) != TT_SUCCESS) {
        fprintf(stderr, "Failed to start hyperperiod timer\n");
        return TT_ERROR_TIMER;
    }

    // 트레이싱 설정 및 활성화
    settimer = setup_trace_stop_timer(ctx, ctx->config.traceduration, &tracetimer);
    tracer_on();

#if defined(CONFIG_TRACE_EVENT) || defined(CONFIG_TRACE_BPF_EVENT)
    struct timespec now;
    clock_gettime(ctx->config.clockid, &now);
    printf("tracer_on!!!: %ld\n", ts_ns(now));
#endif

    // 메인 이벤트 루프
    tt_error_t result = epoll_loop(ctx);

    // 트레이스 타이머 정리
    if (settimer) {
        timer_delete(tracetimer);
    }

    printf("Shutdown requested, cleaning up resources...\n");

    return result;
}
