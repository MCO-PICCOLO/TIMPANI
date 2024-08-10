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
#define diff(b, a)		(b - a)

#ifdef __cplusplus
}
#endif

#endif /* _TIMETRIGGER_H */
