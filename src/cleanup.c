#include "internal.h"

static void cleanup_tasks(struct context *ctx);
static void cleanup_communication(struct context *ctx);
static void cleanup_hyperperiod(struct context *ctx);
static void cleanup_bpf_trace(void);

void cleanup_all(struct context *ctx)
{
    if (!ctx) return;

    printf("Cleaning up resources...\n");

    cleanup_tasks(ctx);
    cleanup_communication(ctx);
    cleanup_hyperperiod(ctx);
    cleanup_bpf_trace();

    printf("Time Trigger shutdown completed.\n");
}

static void cleanup_tasks(struct context *ctx)
{
    struct time_trigger *tt_p;

    while (!LIST_EMPTY(&ctx->runtime.tt_list)) {
        tt_p = LIST_FIRST(&ctx->runtime.tt_list);

        // BPF에서 PID 제거
        bpf_del_pid(tt_p->task.pid);

        // pidfd 닫기
        if (tt_p->task.pidfd >= 0) {
            close(tt_p->task.pidfd);
        }

        // 타이머 삭제
        timer_delete(tt_p->timer);

        // 리스트에서 제거 및 메모리 해제
        LIST_REMOVE(tt_p, entry);
        free(tt_p);
    }

    // 스케줄 정보의 태스크 리스트 정리
    free_task_list(ctx->runtime.sched_info.tasks);
    ctx->runtime.sched_info.tasks = NULL;
}

static void cleanup_communication(struct context *ctx)
{
    if (ctx->comm.dbus) {
        sd_bus_unref(ctx->comm.dbus);
        ctx->comm.dbus = NULL;
    }

    if (ctx->comm.event) {
        sd_event_unref(ctx->comm.event);
        ctx->comm.event = NULL;
    }
}

static void cleanup_hyperperiod(struct context *ctx)
{
    if (ctx->hp_manager.hyperperiod_us > 0) {
        timer_delete(ctx->hp_manager.hyperperiod_timer);
        hyperperiod_log_statistics(&ctx->hp_manager);
    }
}

static void cleanup_bpf_trace(void)
{
    bpf_off();
    tracer_off();
}
