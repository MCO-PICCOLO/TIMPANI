#ifndef _LIBTTTRACE_H
#define _LIBTTTRACE_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_TRACE_BPF

// ring_buffer callback function type from libbpf.h
typedef int (*ring_buffer_sample_fn)(void *ctx, void *data, size_t size);

int tracer_on(ring_buffer_sample_fn sigwait_cb, void *ctx);
void tracer_off(void);
void write_trace_marker(const char *fmt, ...);
int tracer_add_pid(int pid);

#else

void tracer_on(void);
void tracer_off(void);
void write_trace_marker(const char *fmt, ...);
static inline int tracer_add_pid(int pid) { }

#endif /* CONFIG_TRACE_BPF */

#ifdef __cplusplus
}
#endif

#endif	/* _LIBTTTRACE_H */
