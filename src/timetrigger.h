#ifndef _TIMETRIGGER_H
#define _TIMETRIGGER_H

#ifdef __cplusplus
extern "C" {
#endif

#define SIGNO_TT		__SIGRTMIN+2
#define SIGNO_STOPTRACER	__SIGRTMIN+3

#define NSEC_PER_SEC		1000000000
#define USEC_PER_SEC		1000000
#define NSEC_PER_USEC		1000

#define ts_ns(a)		((a.tv_sec * NSEC_PER_SEC) + a.tv_nsec)
#define ts_us(a)		((a.tv_sec * USEC_PER_SEC) + a.tv_nsec / NSEC_PER_USEC)

static inline struct timespec us_ts(const uint64_t us)
{
	struct timespec ts;
	ts.tv_sec = us / USEC_PER_SEC;
	ts.tv_nsec = (us % USEC_PER_SEC) * NSEC_PER_USEC;
	return ts;
}

static inline struct timespec ns_ts(const uint64_t ns)
{
	struct timespec ts;
	ts.tv_sec = ns / NSEC_PER_SEC;
	ts.tv_nsec = ns % NSEC_PER_SEC;
	return ts;
}

#define ts_diff(b, a)		(ts_ns(b) - ts_ns(a))
#define diff(b, a)		(b - a)

#ifdef __cplusplus
}
#endif

#endif /* _TIMETRIGGER_H */
