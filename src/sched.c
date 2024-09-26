#define _GNU_SOURCE
#include <sched.h>

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/syscall.h>

#include "libttsched.h"

#define PROCESS_NAME_SIZE	16

void set_affinity(int cpu) {
	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(cpu, &cpuset);

	// Set the current thread's (the main thread) CPU affinity mask
	if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) == -1) {
		perror("Error: sched_setaffinity");
	}
}

static int sched_setattr(pid_t pid, const struct sched_attr *attr,
			unsigned int flags)
{
	return syscall(SYS_sched_setattr, pid, attr, flags);
}

void set_schedattr(pid_t pid, unsigned int priority, unsigned int policy) {
	struct sched_attr attr;

	memset(&attr, 0, sizeof(attr));
	attr.size = sizeof(struct sched_attr);
	attr.sched_priority = priority;
	attr.sched_policy = policy;

	if (sched_setattr(pid, &attr, 0) == -1) {
		perror("Error calling sched_setattr.");
	}
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
