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

#include <math.h>

#include "sample_apps.h"

#define ALGO_NSQRT	1
#define ALGO_FIBO	2
#define ALGO_BUSY	3

char pr_name[16];
int algo = ALGO_NSQRT;

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

int main(int argc, char *argv[]) {
	sigset_t sig_set;
	struct timespec now, before;

	clockid_t clockid = CLOCK_REALTIME;

	int signo = SIGNO_TT;
	int signal_received = -1;
	int pid = getpid();

	if (argc < 3) {
		fprintf(stderr, "Usage: %s name loop_cnt [algo]\n", argv[0]);
		fprintf(stderr, "algo:\n");
		fprintf(stderr, "  1: NSQRT (by default)\n");
		fprintf(stderr, "  2: Fibonacci\n");
		fprintf(stderr, "  3: Busy loop (loop_cnt means runtime in us\n");
		return EXIT_FAILURE;
	}

	int loop_cnt = atoi(argv[2]);
	if (argc > 3) {
		algo = atoi(argv[3]);
	}

	prctl(PR_SET_NAME, (unsigned long)argv[1], 0, 0, 0);
	prctl(PR_GET_NAME, pr_name, 0, 0, 0);

	sigemptyset(&sig_set);
	sigaddset(&sig_set, signo);
	sigprocmask(SIG_BLOCK, &sig_set, NULL);

	printf("%s(%d) is waiting for the signal(%d)\n", pr_name, pid, signo);

	while (true) {
		if (sigwait(&sig_set, &signal_received) == -1) {
			perror("Failed to wait for the signal");
			return EXIT_FAILURE;
		}

		if (signal_received != signo) {
			printf("Another signal(%d) is received!!!\n", signal_received);
			continue;
		}

		clock_gettime(clockid, &before);
		do_calculations(loop_cnt);
		clock_gettime(clockid, &now);
#if DEBUG
		printf("now: %ld before: %ld runtime: %8lu us loops: %d\n",
			       ts_ns(now), ts_ns(before),
			       (diff(ts_ns(now), ts_ns(before)) / NSEC_PER_USEC), loop_cnt);
#endif
	}

	return EXIT_SUCCESS;
}
