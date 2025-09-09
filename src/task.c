#include "internal.h"

void destroy_task_info_list(struct task_info *tasks)
{
    while (tasks) {
        struct task_info *current = tasks;
        tasks = tasks->next;
        TT_FREE(current);
    }
}

static struct time_trigger *task_create_node(struct task_info *ti, struct context *ctx)
{
    struct time_trigger *tt_node = calloc(1, sizeof(struct time_trigger));
    if (!tt_node) {
        TT_LOG_ERROR("Failed to allocate memory for time_trigger");
        return NULL;
    }

    memcpy(&tt_node->task, ti, sizeof(tt_node->task));
    tt_node->ctx = ctx;  // context 포인터 설정
    return tt_node;
}

static tt_error_t task_setup_process(struct time_trigger *tt_node)
{
    unsigned int pid = get_pid_by_name(tt_node->task.name);
    if (pid == -1) {
        printf("%s is not running!\n", tt_node->task.name);
        return TT_ERROR_CONFIG;
    }

    if (set_affinity(pid, (int)tt_node->task.cpu_affinity) != 0) {
        fprintf(stderr, "Warning: Failed to set CPU affinity for task %s (PID %d)\n",
            tt_node->task.name, pid);
        // Continue anyway, affinity is not critical for basic operation
    }

    if (set_schedattr(pid, tt_node->task.sched_priority, tt_node->task.sched_policy) != 0) {
        fprintf(stderr, "Warning: Failed to set scheduling attributes for task %s (PID %d)\n",
            tt_node->task.name, pid);
        // Continue anyway, scheduling priority is not critical for basic operation
    }

    tt_node->task.pid = pid;

    // Create pidfd for the task
    tt_node->task.pidfd = create_pidfd(pid);
    if (tt_node->task.pidfd < 0) {
        fprintf(stderr, "Failed to create pidfd for task %s (PID %d)\n",
            tt_node->task.name, pid);
        return TT_ERROR_CONFIG;
    }

    if (bpf_add_pid(pid) < 0) {
        fprintf(stderr, "Warning: Failed to add PID %d to BPF monitoring\n", pid);
        // Continue anyway, monitoring is not critical for basic operation
    }

    return TT_SUCCESS;
}

tt_error_t init_task_list(struct context *ctx)
{
    int success_count = 0;

    LIST_INIT(&ctx->runtime.tt_list);

    for (struct task_info *ti = ctx->runtime.sched_info.tasks; ti; ti = ti->next) {
        if (strcmp(ctx->config.node_id, ti->node_id) != 0) {
            /* The task does not belong to this node. */
            continue;
        }

        struct time_trigger *tt_node = task_create_node(ti, ctx);
        if (!tt_node) {
            continue;
        }

        if (task_setup_process(tt_node) != TT_SUCCESS) {
            TT_FREE(tt_node);
            continue;
        }

        LIST_INSERT_HEAD(&ctx->runtime.tt_list, tt_node, entry);

        // Count tasks for hyperperiod management
        ctx->hp_manager.tasks_in_hyperperiod++;

        success_count++;
    }

    if (success_count == 0) {
        fprintf(stderr, "No tasks were successfully initialized\n");
        return TT_ERROR_CONFIG;
    }

    printf("Successfully initialized %d tasks\n", success_count);
    return TT_SUCCESS;
}
