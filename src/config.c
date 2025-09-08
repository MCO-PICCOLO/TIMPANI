#include "internal.h"

// 기본값 설정
static void config_set_defaults(struct context *ctx)
{
    if (!ctx) {
        return;
    }

    ctx->config.cpu = -1;
    ctx->config.prio = -1;
    ctx->config.port = 7777;
    ctx->config.addr = "127.0.0.1";
    strncpy(ctx->config.node_id, "1", sizeof(ctx->config.node_id) - 1);
    ctx->config.node_id[sizeof(ctx->config.node_id) - 1] = '\0';
    ctx->config.enable_sync = false;
    ctx->config.enable_plot = false;
    ctx->config.clockid = CLOCK_REALTIME;
    ctx->config.traceduration = 3;
}

static void print_usage(const char *program_name)
{
    fprintf(stderr, "Usage: %s [options] [host]\n"
            "Options:\n"
            "  -c <cpu_num>\tcpu affinity for timetrigger\n"
            "  -P <prio>\tRT priority (1~99) for timetrigger\n"
            "  -p <port>\tport to connect to\n"
            "  -t <seconds>\ttrace duration in seconds\n"
            "  -n <node id>\tNode ID\n"
            "  -s\tEnable timer synchronization across multiple nodes\n"
            "  -g\tEnable saving plot data file by using BPF (<node id>.gpdata)\n"
            "  -h\tshow this help\n",
            program_name);
}

tt_error_t config_parse(int argc, char *argv[], struct context *ctx)
{
    config_set_defaults(ctx);

    int opt;
    while ((opt = getopt(argc, argv, "hc:P:p:n:st:g")) >= 0) {
        switch (opt) {
        case 'c':
            ctx->config.cpu = atoi(optarg);
            break;
        case 'P':
            ctx->config.prio = atoi(optarg);
            break;
        case 'p':
            ctx->config.port = atoi(optarg);
            break;
        case 't':
            ctx->config.traceduration = atoi(optarg);
            break;
        case 'n':
            strncpy(ctx->config.node_id, optarg, sizeof(ctx->config.node_id) - 1);
            ctx->config.node_id[sizeof(ctx->config.node_id) - 1] = '\0';
            break;
        case 's':
            ctx->config.enable_sync = true;
            break;
        case 'g':
            ctx->config.enable_plot = true;
            break;
        case 'h':
        default:
            print_usage(argv[0]);
            return TT_ERROR_CONFIG;
        }
    }

    if (optind < argc) {
        ctx->config.addr = argv[optind++];
    }

    return config_validate(ctx);
}

tt_error_t config_validate(const struct context *ctx)
{
    // 우선순위 검증
    if (ctx->config.prio < -1 || ctx->config.prio > 99) {
        fprintf(stderr, "Invalid priority: %d (must be -1 or 1-99)\n", ctx->config.prio);
        return TT_ERROR_CONFIG;
    }

    // 포트 검증
    if (ctx->config.port <= 0 || ctx->config.port > 65535) {
        fprintf(stderr, "Invalid port: %d (must be 1-65535)\n", ctx->config.port);
        return TT_ERROR_CONFIG;
    }

    // CPU 검증 (간단한 범위 체크)
    if (ctx->config.cpu < -1 || ctx->config.cpu > 1024) {
        fprintf(stderr, "Invalid CPU number: %d\n", ctx->config.cpu);
        return TT_ERROR_CONFIG;
    }

    // 트레이스 지속시간 검증
    if (ctx->config.traceduration < 0) {
        fprintf(stderr, "Invalid trace duration: %d (must be >= 0)\n", ctx->config.traceduration);
        return TT_ERROR_CONFIG;
    }

    // 노드 ID 검증
    if (strlen(ctx->config.node_id) == 0) {
        fprintf(stderr, "Node ID cannot be empty\n");
        return TT_ERROR_CONFIG;
    }

    printf("Configuration:\n");
    printf("  CPU affinity: %d\n", ctx->config.cpu);
    printf("  Priority: %d\n", ctx->config.prio);
    printf("  Server: %s:%d\n", ctx->config.addr, ctx->config.port);
    printf("  Node ID: %s\n", ctx->config.node_id);
    printf("  Sync enabled: %s\n", ctx->config.enable_sync ? "yes" : "no");
    printf("  Plot enabled: %s\n", ctx->config.enable_plot ? "yes" : "no");
    printf("  Trace duration: %d seconds\n", ctx->config.traceduration);

    return TT_SUCCESS;
}
