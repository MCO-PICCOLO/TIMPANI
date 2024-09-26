#ifndef _SAMPLE_APPS_H
#define _SAMPLE_APPS_H

#define SIGNO_TT		__SIGRTMIN+2
#define SIGNO_STOPTRACER	__SIGRTMIN+3

#define NSEC_PER_SEC		1000000000
#define USEC_PER_SEC		1000000
#define NSEC_PER_USEC		1000

#define ts_ns(a)	    	((a.tv_sec * NSEC_PER_SEC) + a.tv_nsec)
#define diff(b, a)	    	(b - a)

#endif /* _SAMPLE_APPS_H */
