#ifndef _TRACE_APP_H_
#define _TRACE_APP_H_

#ifdef __cplusplus
extern "C" {
#endif

struct sigwait_event {
	int pid;
	int tgid;
	uint64_t timestamp;
	uint8_t enter;
};

#ifdef __cplusplus
}
#endif

#endif /* _TRACE_APP_H_ */
