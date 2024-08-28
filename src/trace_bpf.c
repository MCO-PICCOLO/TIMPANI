#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#include <pthread.h>

#include <bpf/libbpf.h>
#include <bpf/bpf.h>

#include "trace_bpf.h"

#include "sigwait.skel.h"

static struct sigwait_bpf *sigwait;
static struct ring_buffer *rb;
static pthread_t rb_thread;

static int libbpf_print_fn(enum libbpf_print_level level, const char *format,
			   va_list args)
{
	if (level == LIBBPF_DEBUG) return 0;
	return vfprintf(stderr, format, args);
}

static int sigwait_rb_process(void *ctx, void *data, size_t len)
{
	struct sigwait_event *e = (struct sigwait_event *)data;

	if (e->pid == e->tgid) {
		// main thread or single thread
		printf("[%lu] %d sigwait %s\n",
			e->timestamp, e->pid, e->enter ? "enter" : "exit");
	} else {
		printf("[%lu] %d/%d sigwait %s\n",
			e->timestamp, e->tgid, e->pid, e->enter ? "enter" : "exit");
	}

	return 0;
}

static void *sigwait_func(void *arg)
{
	int ret;

	while (1) {
		ret = ring_buffer__poll(rb, -1);
		if (ret < 0 && ret != -EINTR) {
			fprintf(stderr, "Error ring_buffer__poll: %d\n", ret);
			break;
		}
	}
	pthread_exit(NULL);
	return NULL;
}

static int start_sigwait_bpf(int argc, char **argv)
{
	int ret;

	sigwait = sigwait_bpf__open();
	if (!sigwait) {
		fprintf(stderr, "Error bpf__open\n");
		goto fail;
	}

	ret = sigwait_bpf__load(sigwait);
	if (ret < 0) {
		fprintf(stderr, "Error bpf__load\n");
		goto fail;
	}

	/* Populate the pid_filter_map with PIDs from the command line */
	for (int i = 1; i < argc; i++) {
		uint8_t value = 1;
		int pid = atoi(argv[i]);
		if (bpf_map_update_elem(bpf_map__fd(sigwait->maps.pid_filter_map), &pid, &value, BPF_ANY)) {
			fprintf(stderr, "Error adding PID %d to pid_filter_map\n", pid);
			goto fail;
		}
	}

	rb = ring_buffer__new(bpf_map__fd(sigwait->maps.buffer), sigwait_rb_process, NULL, NULL);
	if (!rb) {
		fprintf(stderr, "Error creating ring buffer\n");
		goto fail;
	}

	ret = sigwait_bpf__attach(sigwait);
	if (ret < 0) {
		fprintf(stderr, "Error bpf__attach\n");
		goto fail;
	}

	if (pthread_create(&rb_thread, NULL, sigwait_func, NULL)) {
		fprintf(stderr, "Error pthread_create\n");
		goto fail;
	}

	return 0;

fail:
	if (rb)
		ring_buffer__free(rb);
	if (sigwait)
		sigwait_bpf__destroy(sigwait);
	return ret;
}

int tracer_on(int argc, char **argv)
{
	int ret;

	libbpf_set_print(libbpf_print_fn);

	ret = start_sigwait_bpf(argc, argv);

	return ret;
}

void tracer_off(void)
{

}

void write_trace_marker(const char *fmt, ...)
{

}
