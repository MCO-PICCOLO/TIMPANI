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

char pr_name[16];
int test_loops;

clockid_t clockid = CLOCK_MONOTONIC;
struct timespec now, before;

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
	printf("nsqrt: complete!!!, i: %d cnt: %d result: %LF\n", i, cnt, res);

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

// Function to perform calculations upon receiving SIGNO_TT
static void do_calculations(int signo) {
	// Do some calculation here, for example:
	// int x = 5;
	// int y = 6;
	//printf("Result of %d + %d is: %d\n", x, y, x+y);
	//x+y;
	//stress_cpu_fibonacci();
	for (int i = 0; i < test_loops; i++) {
		stress_cpu_nsqrt();
	}
}

unsigned int task_period;	// microseconds
unsigned long long jitter_cnt;
// Function to handle SIGNO_TT signals and execute do_calculations()
static void sig_handler(int signo, siginfo_t *info, void *context) {
	if (signo == SIGNO_TT) {
		before = now;
		clock_gettime(clockid, &now);

		int jitter = diff(ts_ns(now), ts_ns(before)) / NSEC_PER_USEC - task_period;
		if (jitter < -100 || jitter > 100) {
			printf("%s: jitter(%llu) for execution: %d us\n", pr_name, ++jitter_cnt, jitter);
		}
		do_calculations(SIGNO_TT); // Perform calculations when receiving SIGNO_TT
	}
}

#if 0
void sigusr2_handler(int signo) {
	if (signo == SIGUSR2) {
		printf("Received SIGUSR2 signal\n");
	}
}
#endif

int main(int argc, char *argv[]) {
	struct sigaction act;

	int pid = getpid();

	task_period = atoi(argv[2]);
	test_loops = atoi(argv[3]);

	prctl(PR_SET_NAME, (unsigned long)argv[1], 0, 0, 0);
	prctl(PR_GET_NAME, pr_name, 0, 0, 0);

	memset(&act, 0, sizeof(struct sigaction));
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_SIGINFO;
	act.sa_sigaction = &sig_handler; // Set the signal handler function to handle SIGNO_TT

	if (sigaction(SIGNO_TT, &act, NULL) == -1) {  // Register the signal handler for SIGNO_TT
		perror("Failed to register sigaction");
		return EXIT_FAILURE;
	}

#if 0
	act.sa_sigaction = &sigusr2_handler;

	if (sigaction(SIGUSR2, &act, NULL) == -1) {
		perror("Failed to register sigaction");
		return EXIT_FAILURE;
	}
#endif

	printf("%s (%d) with period %d us is waiting for signal(%d)\n", pr_name, pid, task_period, SIGNO_TT);

	int signal_received = 0; // Variable to keep track of the received signals

	while (1) {
		siginfo_t info;

		clock_gettime(clockid, &now);

		if (sigwait(&act.sa_mask, &signal_received) == -1) {   // Wait for a signal from act.sa_mask
			perror("Failed to wait for signals");
			return EXIT_FAILURE;
		}

		if (signal_received != SIGNO_TT) {
			printf("signal %d is received!!!\n", signal_received);
			continue;  // If the received signal is not SIGNO_TT, continue waiting
		}
		printf("Received SIGNO_TT\n");
	}

	return EXIT_SUCCESS;
}

