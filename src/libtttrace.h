#ifndef _LIBTTTRACE_H
#define _LIBTTTRACE_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_TRACE_BPF
int tracer_on(int argc, char *argv[]);
#else
void tracer_on(void);
#endif
void tracer_off(void);
void write_trace_marker(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif	/* _LIBTTTRACE_H */
