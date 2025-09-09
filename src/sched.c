#define _GNU_SOURCE
#include <sched.h>

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/syscall.h>
#include <signal.h>
#include <errno.h>

#include "libttsched.h"

#define PROCESS_NAME_SIZE	16

int set_affinity(pid_t pid, int cpu) {
	cpu_set_t cpuset;
	int num_cpus = sysconf(_SC_NPROCESSORS_ONLN);

	if (num_cpus < 0) {
		fprintf(stderr, "Error: Failed to get number of CPUs: %s\n", strerror(errno));
		return -1;
	}

	// Validate CPU number
	if (cpu < 0 || cpu >= num_cpus) {
		fprintf(stderr, "Warning: Invalid CPU %d (available: 0-%d), setting to CPU 0\n",
			cpu, num_cpus - 1);
		cpu = 0; // Fallback to CPU 0
	}

	CPU_ZERO(&cpuset);
	CPU_SET(cpu, &cpuset);

	// Set pid's CPU affinity mask
	if (sched_setaffinity(pid, sizeof(cpu_set_t), &cpuset) == -1) {
		fprintf(stderr, "Error: sched_setaffinity failed for PID %d with CPU %d: %s\n",
			pid, cpu, strerror(errno));
		return -1;
	}

	printf("Info: Successfully set CPU affinity for PID %d to CPU %d\n", pid, cpu);
	return 0;
}

static int sched_setattr_tt(pid_t pid, const struct sched_attr_tt *attr,
			unsigned int flags)
{
	return syscall(SYS_sched_setattr, pid, attr, flags);
}

int set_schedattr(pid_t pid, unsigned int priority, unsigned int policy) {
	struct sched_attr_tt attr;

	memset(&attr, 0, sizeof(attr));
	attr.size = sizeof(struct sched_attr_tt);
	attr.sched_priority = priority;
	attr.sched_policy = policy;

	if (sched_setattr_tt(pid, &attr, 0) == -1) {
		perror("Error calling sched_setattr.");
		return -1;
	}
	return 0;
}

void get_process_name_by_pid(const int pid, char name[])
{
	if (name) {
		char procpath[60] = {};

		sprintf(procpath, "/proc/%d/comm",pid);

		FILE* f = fopen(procpath,"r");
		if (f) {
			size_t size;
			size = fread(name, sizeof(char), PROCESS_NAME_SIZE, f);
			if (size > 0) {
				if ('\n' == name[size-1])
					name[size-1] = '\0';
			}
			fclose(f);
		}
	}
}

static void get_thread_name(pid_t pid, pid_t tid, char *name, size_t len)
{
	char path[256];
	snprintf(path, sizeof(path), "/proc/%d/task/%d/comm", pid, tid);

	FILE *file = fopen(path, "r");
	if (file == NULL) {
		return;
	}

	fgets(name, len, file);
	fclose(file);

	// Remove the newline character at the end
	size_t nl = strcspn(name, "\n");
	if (name[nl] == '\n') {
		name[nl] = '\0';
	}
}

static int list_threads(const char *name, int pid)
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

int get_pid_by_name(const char *name)
{
	int ret = -1;

	DIR *proc_dir = opendir("/proc");
	if (!proc_dir) {
		perror("failed to open /proc");
		return -1;
	}

	struct dirent *entry;
	while ((entry = readdir(proc_dir)) != NULL) {
		if (entry->d_type == DT_DIR) {
			int pid = atoi(entry->d_name);
			if (pid > 0) {	// Skip '.' and '..' and non-numeric entries
				ret = list_threads(name, pid);
				if (ret != -1) {
					break;
				}
			}
		}
	}
	closedir(proc_dir);
	return ret;
}

static int pidfd_open_tt(pid_t pid, unsigned int flags)
{
	return syscall(SYS_pidfd_open, pid, flags);
}

static int pidfd_send_signal_tt(int pidfd, int sig, siginfo_t *info, unsigned int flags)
{
	return syscall(SYS_pidfd_send_signal, pidfd, sig, info, flags);
}

int create_pidfd(pid_t pid)
{
	int pidfd = pidfd_open_tt(pid, 0);
	if (pidfd < 0) {
		perror("pidfd_open failed");
		return -1;
	}
	return pidfd;
}

int send_signal_pidfd(int pidfd, int signal)
{
	int ret = pidfd_send_signal_tt(pidfd, signal, NULL, 0);
	if (ret < 0) {
		perror("pidfd_send_signal failed");
		return ret;
	}
	return 0;
}

int is_process_alive(int pidfd)
{
	if (pidfd < 0) {
		return 0;
	}

	// Try a null signal to check if process is alive
	int ret = pidfd_send_signal_tt(pidfd, 0, NULL, 0);
	return (ret == 0);
}
