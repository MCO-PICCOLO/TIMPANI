#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <sched.h>
#include <stdint.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <stdbool.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>

#include <math.h>

#include "sample_apps.h"

#define ALGO_NSQRT	1
#define ALGO_FIBO	2
#define ALGO_BUSY	3

/* Global variables */
char pr_name[16];
int algo = ALGO_NSQRT;
rt_task_config_t task_config;
rt_stats_t task_stats;
volatile bool shutdown_requested = false;

/*
 *  stress_cpu_nsqrt()
 *	iterative Newton-Raphson square root
 */
static int stress_cpu_nsqrt(void)
{
	int i, cnt;
	long double res;
	const long double precision = 1.0e-12L;
	const int max_iter = 56;

	for (i = 16300; i < 16384; i++) {
		const long double n = (long double)i;
		long double lo = (n < 1.0L) ? n : 1.0L;
		long double hi = (n < 1.0L) ? 1.0L : n;
		long double rt;
		int j = 0;

		while ((j++ < max_iter) && ((hi - lo) > precision)) {
			const long double g = (lo + hi) / 2.0L;
			if ((g * g) > n)
				hi = g;
			else
				lo = g;
		}
		rt = (lo + hi) / 2.0L;
		cnt = j;
		res = rt;
		if (true) {
			const long double r2 = ((long double)rint((double)(rt * rt))); //shim_rintl(rt * rt);

			if (j >= max_iter) {
				perror("nsqrt: Newton-Raphson sqrt "
					"computation took more iterations "
					"than expected\n");
				return EXIT_FAILURE;
			}
			if ((int)r2 != i) {
				perror("nsqrt: Newton-Raphson sqrt not "
					"accurate enough\n");
				return EXIT_FAILURE;
			}
		}
	}

	return EXIT_SUCCESS;
}

/*
 *   stress_cpu_fibonacci()
 *	compute fibonacci series
 */
static int stress_cpu_fibonacci(void)
{
	const uint64_t fn_res = 0xa94fad42221f2702ULL;
	register uint64_t f1 = 0, f2 = 1, fn;
	uint64_t i = 0;

	do {
		fn = f1 + f2;
		f1 = f2;
		f2 = fn;
		i++;
	} while (!(fn & 0x8000000000000000ULL));

	if (fn_res != fn) {
		perror("fibonacci: fibonacci error detected, summation "
			"or assignment failure\n");
		return EXIT_FAILURE;
	}
	else {
		printf("%lu loops completed!!!\n", i);
	}

	return EXIT_SUCCESS;
}

/*
 *   stress_cpu_busyloop()
 *	do busy-loop for the given runtime
 */
static inline uint64_t get_cpu_time(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
	return ts_ns(ts);
}

static void stress_cpu_busyloop(int runtime_us)
{
	uint64_t start_ns, runtime_ns;

	runtime_ns = runtime_us * NSEC_PER_USEC;
	start_ns = get_cpu_time();
	while(1) {
		if ((get_cpu_time() - start_ns) >= runtime_ns) break;
	}
}

static void do_calculations(int loop_count) {
	if (algo == ALGO_NSQRT) {
		for (int i = 0; i < loop_count; i++) {
			stress_cpu_nsqrt();
		}
	}
	else if (algo == ALGO_FIBO) {
		for (int i = 0; i < loop_count; i++) {
			stress_cpu_fibonacci();
		}
	} else if (algo == ALGO_BUSY) {
		stress_cpu_busyloop(loop_count);
	}
}

/* Realtime task configuration and statistics functions */
void rt_task_init(rt_task_config_t *config) {
    config->period_ms = 100;      /* Default 100ms period */
    config->deadline_ms = 100;    /* Default deadline = period */
    config->runtime_us = 50000;   /* Default 50ms runtime */
    config->priority = 50;        /* Default priority */
    config->enable_stats = true;  /* Enable stats by default */
    strncpy(config->name, "rt_task", sizeof(config->name) - 1);
}

void rt_stats_init(rt_stats_t *stats) {
    stats->min_runtime_us = ULONG_MAX;
    stats->max_runtime_us = 0;
    stats->avg_runtime_us = 0;
    stats->total_runtime_us = 0;
    stats->deadline_misses = 0;
    stats->iterations = 0;
    stats->last_runtime_us = 0;
}

void rt_stats_update(rt_stats_t *stats, unsigned long runtime_us, bool deadline_miss) {
    stats->last_runtime_us = runtime_us;
    stats->total_runtime_us += runtime_us;
    stats->iterations++;

    if (runtime_us < stats->min_runtime_us) {
        stats->min_runtime_us = runtime_us;
    }
    if (runtime_us > stats->max_runtime_us) {
        stats->max_runtime_us = runtime_us;
    }

    stats->avg_runtime_us = stats->total_runtime_us / stats->iterations;

    if (deadline_miss) {
        stats->deadline_misses++;
    }
}

void rt_stats_print(const rt_stats_t *stats, const rt_task_config_t *config) {
    printf("\n=== Runtime Statistics for %s ===\n", config->name);
    printf("Iterations:      %lu\n", stats->iterations);
    printf("Min runtime:     %lu us\n", stats->min_runtime_us);
    printf("Max runtime:     %lu us\n", stats->max_runtime_us);
    printf("Avg runtime:     %lu us\n", stats->avg_runtime_us);
    printf("Last runtime:    %lu us\n", stats->last_runtime_us);
    printf("Deadline misses: %lu (%.2f%%)\n",
           stats->deadline_misses,
           (stats->iterations > 0) ? (100.0 * stats->deadline_misses / stats->iterations) : 0.0);
    printf("Period:          %lu ms\n", config->period_ms);
    printf("Deadline:        %lu ms\n", config->deadline_ms);
    printf("Expected runtime: %lu us\n", config->runtime_us);
    printf("=====================================\n");
}

/* Signal handlers */
static void sigint_handler(int signo) {
    shutdown_requested = true;
    printf("\nShutdown requested. Printing final statistics...\n");
}

static void setup_signal_handlers(void) {
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

/* Timer-based periodic execution */
static int setup_periodic_timer(unsigned long period_ms) {
    timer_t timer_id;
    struct sigevent sev;
    struct itimerspec its;

    /* Create timer */
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIGNO_TT;
    sev.sigev_value.sival_ptr = &timer_id;

    if (timer_create(CLOCK_MONOTONIC, &sev, &timer_id) == -1) {
        perror("timer_create");
        return -1;
    }

    /* Configure timer */
    its.it_value.tv_sec = period_ms / 1000;
    its.it_value.tv_nsec = (period_ms % 1000) * 1000000;
    its.it_interval.tv_sec = its.it_value.tv_sec;
    its.it_interval.tv_nsec = its.it_value.tv_nsec;

    if (timer_settime(timer_id, 0, &its, NULL) == -1) {
        perror("timer_settime");
        return -1;
    }

    return 0;
}

/* Set real-time scheduling */
static int set_realtime_priority(int priority) {
    struct sched_param param;

    param.sched_priority = priority;
    if (sched_setscheduler(0, SCHED_FIFO, &param) == -1) {
        perror("sched_setscheduler");
        return -1;
    }

    return 0;
}

static void print_usage(const char *prog_name) {
    printf("Usage: %s [OPTIONS] name\n", prog_name);
    printf("Options:\n");
    printf("  -p, --period PERIOD     Period in milliseconds (default: 100)\n");
    printf("  -d, --deadline DEADLINE Deadline in milliseconds (default: period)\n");
    printf("  -r, --runtime RUNTIME   Expected runtime in microseconds (default: 50000)\n");
    printf("  -P, --priority PRIORITY Real-time priority 1-99 (default: 50)\n");
    printf("  -a, --algorithm ALGO    Algorithm: 1=NSQRT, 2=Fibonacci, 3=Busy loop (default: 1)\n");
    printf("  -l, --loops LOOPS       Loop count for algorithms 1&2, runtime_us for algo 3 (default: 10)\n");
    printf("  -s, --stats             Enable detailed statistics (default: enabled)\n");
    printf("  -t, --timer             Use timer-based periodic execution (default: signal-based)\n");
    printf("  -h, --help              Show this help message\n");
    printf("\nExamples:\n");
    printf("  %s -p 50 -d 45 -r 30000 -P 80 mytask\n", prog_name);
    printf("  %s --period 100 --algorithm 2 --loops 5 fibonacci_task\n", prog_name);
}

int main(int argc, char *argv[]) {
	sigset_t sig_set;
	struct timespec now, before, deadline_time;
	clockid_t clockid = CLOCK_MONOTONIC;
	int signo = SIGNO_TT;
	int signal_received = -1;
	int pid = getpid();
	int loop_cnt = 10;
	bool use_timer = false;

	/* Initialize task configuration and stats */
	rt_task_init(&task_config);
	rt_stats_init(&task_stats);

	/* Parse command line options */
	static struct option long_options[] = {
		{"period",    required_argument, 0, 'p'},
		{"deadline",  required_argument, 0, 'd'},
		{"runtime",   required_argument, 0, 'r'},
		{"priority",  required_argument, 0, 'P'},
		{"algorithm", required_argument, 0, 'a'},
		{"loops",     required_argument, 0, 'l'},
		{"stats",     no_argument,       0, 's'},
		{"timer",     no_argument,       0, 't'},
		{"help",      no_argument,       0, 'h'},
		{0, 0, 0, 0}
	};

	int opt;
	while ((opt = getopt_long(argc, argv, "p:d:r:P:a:l:sth", long_options, NULL)) != -1) {
		switch (opt) {
		case 'p':
			task_config.period_ms = strtoul(optarg, NULL, 10);
			break;
		case 'd':
			task_config.deadline_ms = strtoul(optarg, NULL, 10);
			break;
		case 'r':
			task_config.runtime_us = strtoul(optarg, NULL, 10);
			break;
		case 'P':
			task_config.priority = atoi(optarg);
			if (task_config.priority < 1 || task_config.priority > 99) {
				fprintf(stderr, "Priority must be between 1 and 99\n");
				return EXIT_FAILURE;
			}
			break;
		case 'a':
			algo = atoi(optarg);
			if (algo < 1 || algo > 3) {
				fprintf(stderr, "Algorithm must be 1, 2, or 3\n");
				return EXIT_FAILURE;
			}
			break;
		case 'l':
			loop_cnt = atoi(optarg);
			break;
		case 's':
			task_config.enable_stats = true;
			break;
		case 't':
			use_timer = true;
			break;
		case 'h':
			print_usage(argv[0]);
			return EXIT_SUCCESS;
		default:
			print_usage(argv[0]);
			return EXIT_FAILURE;
		}
	}

	/* Check for task name argument */
	if (optind >= argc) {
		fprintf(stderr, "Error: Task name is required\n");
		print_usage(argv[0]);
		return EXIT_FAILURE;
	}

	/* Set task name */
	strncpy(task_config.name, argv[optind], sizeof(task_config.name) - 1);

	/* Set default deadline to period if not specified */
	if (task_config.deadline_ms == 100 && task_config.period_ms != 100) {
		task_config.deadline_ms = task_config.period_ms;
	}

	/* Set process name */
	prctl(PR_SET_NAME, (unsigned long)task_config.name, 0, 0, 0);
	prctl(PR_GET_NAME, pr_name, 0, 0, 0);

	/* Set real-time priority */
	if (set_realtime_priority(task_config.priority) == -1) {
		fprintf(stderr, "Warning: Could not set real-time priority. Running as normal priority.\n");
	}

	/* Setup signal handling */
	setup_signal_handlers();
	sigemptyset(&sig_set);
	sigaddset(&sig_set, signo);
	sigprocmask(SIG_BLOCK, &sig_set, NULL);

	/* Setup periodic execution */
	if (use_timer) {
		if (setup_periodic_timer(task_config.period_ms) == -1) {
			fprintf(stderr, "Failed to setup periodic timer\n");
			return EXIT_FAILURE;
		}
	}

	printf("=== Real-time Task Configuration ===\n");
	printf("Task name:       %s (PID: %d)\n", pr_name, pid);
	printf("Period:          %lu ms\n", task_config.period_ms);
	printf("Deadline:        %lu ms\n", task_config.deadline_ms);
	printf("Expected runtime: %lu us\n", task_config.runtime_us);
	printf("Priority:        %d\n", task_config.priority);
	printf("Algorithm:       %d (%s)\n", algo,
	       algo == ALGO_NSQRT ? "Newton-Raphson sqrt" :
	       algo == ALGO_FIBO ? "Fibonacci" : "Busy loop");
	printf("Loop count:      %d\n", loop_cnt);
	printf("Execution mode:  %s\n", use_timer ? "Timer-based" : "Signal-based");
	printf("=====================================\n");

	if (!use_timer) {
		printf("Waiting for signal %d to start periodic execution...\n", signo);
	}

	while (!shutdown_requested) {
		if (sigwait(&sig_set, &signal_received) == -1) {
			if (errno == EINTR) continue;
			perror("Failed to wait for the signal");
			break;
		}

		if (signal_received != signo) {
			printf("Another signal(%d) is received!!!\n", signal_received);
			continue;
		}

		/* Record start time */
		clock_gettime(clockid, &before);

		/* Calculate deadline time */
		deadline_time = before;
		deadline_time.tv_sec += task_config.deadline_ms / 1000;
		deadline_time.tv_nsec += (task_config.deadline_ms % 1000) * NSEC_PER_MSEC;
		if (deadline_time.tv_nsec >= NSEC_PER_SEC) {
			deadline_time.tv_sec++;
			deadline_time.tv_nsec -= NSEC_PER_SEC;
		}

		/* Execute workload */
		do_calculations(loop_cnt);

		/* Record end time */
		clock_gettime(clockid, &now);

		/* Calculate runtime and check deadline */
		unsigned long runtime_us = diff(ts_ns(now), ts_ns(before)) / NSEC_PER_USEC;
		bool deadline_miss = (ts_ns(now) > ts_ns(deadline_time));

		/* Update statistics */
		if (task_config.enable_stats) {
			rt_stats_update(&task_stats, runtime_us, deadline_miss);
		}

		/* Print runtime information */
		printf("[%lu] Runtime: %8lu us", task_stats.iterations, runtime_us);
		if (deadline_miss) {
			printf(" [DEADLINE MISS!]");
		}
		printf(" (Period: %lu ms, Deadline: %lu ms)\n",
		       task_config.period_ms, task_config.deadline_ms);

		/* Print periodic statistics every 100 iterations */
		if (task_config.enable_stats && (task_stats.iterations % 100 == 0)) {
			printf("--- Periodic Stats (iter %lu) ---\n", task_stats.iterations);
			printf("Avg: %lu us, Min: %lu us, Max: %lu us, Misses: %lu\n",
			       task_stats.avg_runtime_us, task_stats.min_runtime_us,
			       task_stats.max_runtime_us, task_stats.deadline_misses);
		}
	}

	/* Print final statistics */
	if (task_config.enable_stats && task_stats.iterations > 0) {
		rt_stats_print(&task_stats, &task_config);
	}

	return EXIT_SUCCESS;
}
