#define _GNU_SOURCE
#include "internal.h"
#include <sched.h>
#include <dirent.h>
#include <sys/syscall.h>
#include <signal.h>

// Ensure SCHED_NORMAL is defined (sometimes defined as SCHED_OTHER)
#ifndef SCHED_NORMAL
#define SCHED_NORMAL 0
#endif
#define PROCESS_NAME_SIZE	16

ttsched_error_t set_affinity(pid_t pid, int cpu) {
	cpu_set_t cpuset;
	int num_cpus = sysconf(_SC_NPROCESSORS_ONLN);

	if (num_cpus < 0) {
		TT_LOG_ERROR("Failed to get number of CPUs: %s", strerror(errno));
		return TTSCHED_ERROR_SYSTEM;
	}

	// Validate CPU number
	if (cpu < 0 || cpu >= num_cpus) {
		TT_LOG_WARNING("Invalid CPU %d (available: 0-%d), setting to CPU 0",
			cpu, num_cpus - 1);
		cpu = 0; // Fallback to CPU 0
	}

	CPU_ZERO(&cpuset);
	CPU_SET(cpu, &cpuset);

	// Set pid's CPU affinity mask
	if (sched_setaffinity(pid, sizeof(cpu_set_t), &cpuset) == -1) {
		TT_LOG_ERROR("sched_setaffinity failed for PID %d with CPU %d: %s",
			pid, cpu, strerror(errno));
		return TTSCHED_ERROR_PERMISSION;
	}

	TT_LOG_INFO("Successfully set CPU affinity for PID %d to CPU %d", pid, cpu);
	return TTSCHED_SUCCESS;
}

static int set_sched_attr_syscall(pid_t pid, const struct sched_attr_tt *attr,
			unsigned int flags)
{
	return syscall(SYS_sched_setattr, pid, attr, flags);
}

ttsched_error_t set_schedattr(pid_t pid, unsigned int priority, unsigned int policy) {
	struct sched_attr_tt attr;

	// 입력 인자 검증
	if (priority > 99) {
		TT_LOG_ERROR("Invalid priority %u (must be <= 99)", priority);
		return TTSCHED_ERROR_INVALID_ARGS;
	}

	if (policy != SCHED_NORMAL && policy != SCHED_FIFO && policy != SCHED_RR) {
		TT_LOG_ERROR("Invalid policy %u", policy);
		return TTSCHED_ERROR_INVALID_ARGS;
	}

	memset(&attr, 0, sizeof(attr));
	attr.size = sizeof(struct sched_attr_tt);
	attr.sched_priority = priority;
	attr.sched_policy = policy;

	if (set_sched_attr_syscall(pid, &attr, 0) == -1) {
		TT_LOG_ERROR("sched_setattr failed for PID %d: %s", pid, strerror(errno));
		return TTSCHED_ERROR_PERMISSION;
	}
	TT_LOG_INFO("Successfully set scheduling attributes for PID %d (priority=%u, policy=%u)",
		pid, priority, policy);
	return TTSCHED_SUCCESS;
}

ttsched_error_t get_process_name_by_pid(const int pid, char name[])
{
	if (!name) {
		TT_LOG_ERROR("Invalid name buffer pointer");
		return TTSCHED_ERROR_INVALID_ARGS;
	}

	if (pid <= 0) {
		TT_LOG_ERROR("Invalid PID %d", pid);
		return TTSCHED_ERROR_INVALID_ARGS;
	}

	char procpath[60] = {};
	sprintf(procpath, "/proc/%d/comm", pid);

	FILE* f = fopen(procpath, "r");
	if (!f) {
		TT_LOG_ERROR("Failed to open %s: %s", procpath, strerror(errno));
		return TTSCHED_ERROR_SYSTEM;
	}

	size_t size = fread(name, sizeof(char), PROCESS_NAME_SIZE, f);
	if (size > 0) {
		if ('\n' == name[size-1])
			name[size-1] = '\0';
	}
	fclose(f);

	return TTSCHED_SUCCESS;
}

static void get_thread_name(pid_t pid, pid_t tid, char *name, size_t len)
{
	char path[256];
	snprintf(path, sizeof(path), "/proc/%d/task/%d/comm", pid, tid);

	FILE *file = fopen(path, "r");
	if (file == NULL) {
		return;
	}

	if (fgets(name, len, file) == NULL) {
		name[0] = '\0';  // 실패 시 빈 문자열
	}
	fclose(file);

	// Remove the newline character at the end
	size_t nl = strcspn(name, "\n");
	if (name[nl] == '\n') {
		name[nl] = '\0';
	}
}

static int find_threads_by_name(const char *name, int pid)
{
	int ret = -1;
	char path[256];
	snprintf(path, sizeof(path), "/proc/%d/task", pid);

	DIR *dir = opendir(path);
	if (!dir) {
		perror("opendir");
		return -1;
	}

	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_type == DT_DIR) {
			int tid = atoi(entry->d_name);
			if (tid > 0) {	// Skip '.' and '..' and non-numeric entries
				char tname[256];
				get_thread_name(pid, tid, tname, sizeof(tname));
				if (strcmp(name, tname) == 0) {
					// found it
					ret = tid;
					break;
				}
			}
		}
	}
	closedir(dir);
	return ret;
}

ttsched_error_t get_pid_by_name(const char *name, int *pid)
{
	if (!name || !pid) {
		TT_LOG_ERROR("Invalid name or pid pointer");
		return TTSCHED_ERROR_INVALID_ARGS;
	}

	*pid = -1;

	DIR *proc_dir = opendir("/proc");
	if (!proc_dir) {
		TT_LOG_ERROR("Failed to open /proc: %s", strerror(errno));
		return TTSCHED_ERROR_SYSTEM;
	}

	struct dirent *entry;
	while ((entry = readdir(proc_dir)) != NULL) {
		if (entry->d_type == DT_DIR) {
			int current_pid = atoi(entry->d_name);
			if (current_pid > 0) {	// Skip '.' and '..' and non-numeric entries
				int tid = find_threads_by_name(name, current_pid);
				if (tid != -1) {
					*pid = tid;
					break;
				}
			}
		}
	}
	closedir(proc_dir);

	if (*pid == -1) {
		TT_LOG_WARNING("Process with name '%s' not found", name);
		return TTSCHED_ERROR_SYSTEM;
	}

	return TTSCHED_SUCCESS;
}

static int open_pidfd_syscall(pid_t pid, unsigned int flags)
{
	return syscall(SYS_pidfd_open, pid, flags);
}

static int send_signal_pidfd_syscall(int pidfd, int sig, siginfo_t *info, unsigned int flags)
{
	return syscall(SYS_pidfd_send_signal, pidfd, sig, info, flags);
}

ttsched_error_t create_pidfd(pid_t pid, int *pidfd)
{
	if (!pidfd) {
		TT_LOG_ERROR("Invalid pidfd pointer");
		return TTSCHED_ERROR_INVALID_ARGS;
	}

	if (pid <= 0) {
		TT_LOG_ERROR("Invalid PID %d", pid);
		return TTSCHED_ERROR_INVALID_ARGS;
	}

	*pidfd = open_pidfd_syscall(pid, 0);
	if (*pidfd < 0) {
		TT_LOG_ERROR("pidfd_open failed for PID %d: %s", pid, strerror(errno));
		return TTSCHED_ERROR_PERMISSION;
	}
	return TTSCHED_SUCCESS;
}

ttsched_error_t send_signal_pidfd(int pidfd, int signal)
{
	if (pidfd < 0) {
		TT_LOG_ERROR("Invalid pidfd %d", pidfd);
		return TTSCHED_ERROR_INVALID_ARGS;
	}

	int ret = send_signal_pidfd_syscall(pidfd, signal, NULL, 0);
	if (ret < 0) {
		TT_LOG_ERROR("pidfd_send_signal failed: %s", strerror(errno));
		return TTSCHED_ERROR_PERMISSION;
	}
	return TTSCHED_SUCCESS;
}

ttsched_error_t is_process_alive(int pidfd, int *alive)
{
	if (!alive) {
		TT_LOG_ERROR("Invalid alive pointer");
		return TTSCHED_ERROR_INVALID_ARGS;
	}

	if (pidfd < 0) {
		*alive = 0;
		return TTSCHED_SUCCESS;
	}

	// Try a null signal to check if process is alive
	int ret = send_signal_pidfd_syscall(pidfd, 0, NULL, 0);
	*alive = (ret == 0) ? 1 : 0;
	return TTSCHED_SUCCESS;
}
