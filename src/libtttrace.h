#ifndef _LIBTTTRACE_H
#define _LIBTTTRACE_H

#ifdef __cplusplus
extern "C" {
#endif

void tracer_on(void);
void tracer_off(void);
void write_trace_marker(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif	/* _LIBTTTRACE_H */
