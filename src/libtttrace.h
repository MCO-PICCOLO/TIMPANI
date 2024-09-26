#ifndef _LIBTTTRACE_H
#define _LIBTTTRACE_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_TRACE_EVENT
void tracer_on(void);
void tracer_off(void);
void write_trace_marker(const char *fmt, ...);
#else
static inline void tracer_on(void) {}
static inline void tracer_off(void) {}
static inline void write_trace_marker(const char *fmt, ...) {}
#endif /* CONFIG_TRACE_EVENT */

// ring_buffer callback function type from libbpf.h
typedef int (*ring_buffer_sample_fn)(void *ctx, void *data, size_t size);

#ifdef CONFIG_TRACE_BPF
int bpf_on(ring_buffer_sample_fn sigwait_cb,
	ring_buffer_sample_fn schedstat_cb,
	void *ctx);
void bpf_off(void);
int bpf_add_pid(int pid);
#else
static inline int bpf_on(ring_buffer_sample_fn sigwait_cb,
	ring_buffer_sample_fn schedstat_cb,
	void *ctx) { return 0; }
static inline void bpf_off(void) {}
static inline int bpf_add_pid(int pid) { return 0; }
#endif /* CONFIG_TRACE_BPF */

#ifdef __cplusplus
}
#endif

#endif	/* _LIBTTTRACE_H */
