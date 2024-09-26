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

#include "sample_apps.h"

#include "libttsched.h"

char pr_name[16];

clockid_t clockid = CLOCK_MONOTONIC;
struct timespec now, before;

// Function to perform calculations upon receiving SIGNO_TT
void do_calculations(int signo) {
	// Do some calculation here, for example:
	int x = 5;
	int y = 6;
	//printf("Result of %d + %d is: %d\n", x, y, x+y);
	x+y;
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
	struct sched_attr attr;

	int pid = getpid();

	task_period = atoi(argv[2]);

	prctl(PR_SET_NAME, (unsigned long)argv[1], 0, 0, 0);
	prctl(PR_GET_NAME, pr_name, 0, 0, 0);

	set_schedattr(pid, 80, SCHED_FIFO);

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

