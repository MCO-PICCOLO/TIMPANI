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

#include "libtttrace.h"

static struct sigwait_bpf *sigwait;
static struct ring_buffer *rb;
static pthread_t rb_thread;

static int libbpf_print_fn(enum libbpf_print_level level, const char *format,
			   va_list args)
{
	if (level == LIBBPF_DEBUG) return 0;
	return vfprintf(stderr, format, args);
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

static int start_sigwait_bpf(ring_buffer_sample_fn sigwait_cb, void *ctx)
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

	rb = ring_buffer__new(bpf_map__fd(sigwait->maps.buffer), sigwait_cb, ctx, NULL);
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

int bpf_on(ring_buffer_sample_fn sigwait_cb, void *ctx)
{
	int ret;

	libbpf_set_print(libbpf_print_fn);

	ret = start_sigwait_bpf(sigwait_cb, ctx);

	return ret;
}

void bpf_off(void)
{

}

int bpf_add_pid(int pid)
{
	uint8_t value = 1;

	// Check if sigwait BPF feature is initialized
	if (!sigwait) return -1;

	if (bpf_map_update_elem(bpf_map__fd(sigwait->maps.pid_filter_map), &pid, &value, BPF_ANY)) {
		fprintf(stderr, "Error adding PID %d to pid_filter_map\n", pid);
		return -1;
	}

	return 0;
}
