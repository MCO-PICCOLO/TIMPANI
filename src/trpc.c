#include "internal.h"

static int init_trpc_connection(const char *addr, int port, sd_bus **dbus_ret, sd_event **event_ret)
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

static int get_schedinfo(sd_bus *dbus, char *node_id, struct sched_info *sinfo, struct context *ctx)
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
    buf = NULL;  // now use sbuf->data

    ret = deserialize_sched_info(sbuf, sinfo, ctx);

    free_serial_buf(sbuf);

    return ret;
}

tt_error_t deserialize_sched_info(serial_buf_t *sbuf, struct sched_info *sinfo, struct context *ctx)
{
    uint32_t i;
    uint64_t hyperperiod_us = 0;
    char workload_id[64] = { 0 };

    // Unpack sched_info
    if (deserialize_int32_t(sbuf, &sinfo->nr_tasks) < 0) {
        fprintf(stderr, "Failed to deserialize nr_tasks\n");
        return TT_ERROR_NETWORK;
    }
    sinfo->tasks = NULL;

    // Unpack task_info list entries
    for (i = 0; i < sinfo->nr_tasks; i++) {
        struct task_info *tinfo = malloc(sizeof(struct task_info));
        if (tinfo == NULL) {
            fprintf(stderr, "Failed to allocate memory for task_info\n");
            destroy_task_list(sinfo->tasks);
            sinfo->tasks = NULL;
            return TT_ERROR_MEMORY;
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
            destroy_task_list(sinfo->tasks);
            sinfo->tasks = NULL;
            return TT_ERROR_NETWORK;
        }

        tinfo->next = sinfo->tasks;
        sinfo->tasks = tinfo;

        printf("Task info - name: %s, priority: %d, policy: %d, period: %d\n",
               tinfo->name, tinfo->sched_priority, tinfo->sched_policy, tinfo->period);
        printf("  release_time: %d, runtime: %d, deadline: %d\n",
               tinfo->release_time, tinfo->runtime, tinfo->deadline);
        printf("  cpu_affinity: 0x%lx, allowable_deadline_misses: %d, node_id: %s\n",
               tinfo->cpu_affinity, tinfo->allowable_deadline_misses, tinfo->node_id);
    }

    if (deserialize_str(sbuf, workload_id) < 0 ||
        deserialize_int64_t(sbuf, &hyperperiod_us) < 0) {
        fprintf(stderr, "Failed to deserialize workload info\n");
        destroy_task_list(sinfo->tasks);
        sinfo->tasks = NULL;
        return TT_ERROR_NETWORK;
    }

    printf("\nWorkload: %s\n", workload_id);
    printf("Hyperperiod: %lu us\n", hyperperiod_us);

    // context의 hp_manager에 초기화 (수정된 부분)
    if (init_hyperperiod(&ctx->hp_manager, workload_id, hyperperiod_us, ctx) != TT_SUCCESS) {
        fprintf(stderr, "Failed to initialize hyperperiod manager\n");
        destroy_task_list(sinfo->tasks);
        sinfo->tasks = NULL;
        return TT_ERROR_CONFIG;
    }

    return TT_SUCCESS;
}

static int sync_timer_internal(sd_bus *dbus, char *node_id, struct timespec *ts_ptr)
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
        usleep(POLLING_INTERVAL_US);
    }

    return 0;
}

tt_error_t init_trpc(struct context *ctx)
{
    int retry_count = 0;

    // Initialize trpc channel and get schedule info with retry logic
    while (retry_count < MAX_CONNECTION_RETRIES) {
        if (init_trpc_connection(ctx->config.addr, ctx->config.port,
                                &ctx->comm.dbus, &ctx->comm.event) == 0) {
            if (get_schedinfo(ctx->comm.dbus, ctx->config.node_id,
                             &ctx->runtime.sched_info, ctx) == 0) {
                /* Successfully retrieved schedule info */
                printf("Successfully connected and retrieved schedule info (attempt %d)\n", retry_count + 1);
                return TT_SUCCESS;
            }
        }

        /* failed to get schedule info, retrying */
        retry_count++;
        printf("Connection attempt %d/%d failed, retrying...\n", retry_count, MAX_CONNECTION_RETRIES);
        usleep(RETRY_INTERVAL_US);
    }

    fprintf(stderr, "Failed to connect to server after %d attempts\n", MAX_CONNECTION_RETRIES);
    return TT_ERROR_NETWORK;
}

tt_error_t sync_timer_with_server(struct context *ctx)
{
    if (!ctx->config.enable_sync) {
        return TT_SUCCESS;
    }

    if (sync_timer_internal(ctx->comm.dbus, ctx->config.node_id,
                           &ctx->runtime.starttimer_ts) < 0) {
        return TT_ERROR_NETWORK;
    }

    return TT_SUCCESS;
}

tt_error_t report_deadline_miss(sd_bus *dbus, char *node_id, const char *taskname)
{
    int result = trpc_client_dmiss(dbus, node_id, taskname);
    return (result < 0) ? TT_ERROR_NETWORK : TT_SUCCESS;
}
