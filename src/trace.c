#include <stdio.h>
#include <fcntl.h>
#include <stdarg.h>
#include <unistd.h>

#include "libtttrace.h"

int tracer_fd = -1;
int marker_fd = -1;
int en_ev_sched_fd, en_ev_irq_fd;
int en_ev_timer_fd, en_ev_signal_fd;
int en_ev_enter_sigwait_fd, en_ev_exit_sigwait_fd;

const char *tracing_path = "/sys/kernel/debug/tracing";

const char *tracer_path = "/sys/kernel/debug/tracing/tracing_on";
const char *marker_path = "/sys/kernel/debug/tracing/trace_marker";
const char *en_ev_sched_path = "/sys/kernel/debug/tracing/events/sched/enable";
const char *en_ev_timer_path = "/sys/kernel/debug/tracing/events/timer/enable";
const char *en_ev_signal_path = "/sys/kernel/debug/tracing/events/signal/enable";
const char *en_ev_enter_sigwait_path = "/sys/kernel/debug/tracing/events/syscalls/sys_enter_rt_sigtimedwait/enable";
const char *en_ev_exit_sigwait_path = "/sys/kernel/debug/tracing/events/syscalls/sys_exit_rt_sigtimedwait/enable";

static int enable_event(const char *path) {
	int fd;

	fd = open(path, O_WRONLY);
	if (fd >= 0) {
		printf("enable %s\n", path);
		write(fd, "1", 1);
	}

	return fd;
}

static void disable_event(int fd, const char *path) {
	if (fd >= 0) {
		printf("disable %s\n", path);
		write(fd, "0", 1);
		close(fd);
	}
}

void tracer_on(void) {
	en_ev_sched_fd = enable_event(en_ev_sched_path);
	en_ev_timer_fd = enable_event(en_ev_timer_path);
	en_ev_signal_fd = enable_event(en_ev_signal_path);
	en_ev_enter_sigwait_fd = enable_event(en_ev_enter_sigwait_path);
	en_ev_exit_sigwait_fd = enable_event(en_ev_exit_sigwait_path);

	tracer_fd = open(tracer_path, O_WRONLY);
	if (tracer_fd >= 0) {
		printf("Start Tracer\n");
		write(tracer_fd, "1", 1);
	}

	marker_fd = open(marker_path, O_WRONLY);
}

void tracer_off(void) {
	if (marker_fd >= 0) {
		close(marker_fd);
	}

	if (tracer_fd >= 0) {
		write(tracer_fd, "0", 1);
		close(tracer_fd);
		printf("Stop Tracer\n");
	}

	disable_event(en_ev_sched_fd, en_ev_sched_path);
	disable_event(en_ev_timer_fd, en_ev_timer_path);
	disable_event(en_ev_signal_fd, en_ev_signal_path);
	disable_event(en_ev_enter_sigwait_fd, en_ev_enter_sigwait_path);
	disable_event(en_ev_exit_sigwait_fd, en_ev_exit_sigwait_path);
}

void write_trace_marker(const char *fmt, ...) {
	va_list ap;
	char buf[256];
	int n;

	if (marker_fd < 0)
		return;
	va_start(ap, fmt);
	n = vsnprintf(buf, 256, fmt, ap);
	va_end(ap);
	write(marker_fd, buf, n);
}
