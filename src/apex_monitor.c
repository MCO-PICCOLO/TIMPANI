#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include "internal.h"

// Apex.OS UDS path definitions
#define SOCKET_DIR "/tmp/timpani/"
#define SOCKET_FILE "timpani.sock"
#define SOCKET_PATH SOCKET_DIR SOCKET_FILE

// Communication message structure for Apex.OS
// Refer to internal.h for enum definitions
typedef struct {
  int msg_type;
  union {
    struct {
      char name[MAX_APEX_NAME_LEN];
      int type;
    } fault;
    struct {
      char name[MAX_APEX_NAME_LEN];
      int pid;
    } up;
    struct {
      int pid;
    } down;
  } data;
} timpani_msg_t;

// Initialize Apex.OS Monitor UDS
int apex_monitor_init(struct context *ctx)
{
	int server_fd;
	struct sockaddr_un server_addr;

	// Create Unix domain socket
	server_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (server_fd == -1) {
		return TT_ERROR_NETWORK;
	}

	TT_LOG_INFO("Apex.OS Monitor socket created: %s", SOCKET_PATH);

	// Remove any existing socket file
	if (access(SOCKET_PATH, F_OK) == 0) {
		unlink(SOCKET_PATH);
	} else {
		// Ensure the directory exists
		mkdir(SOCKET_DIR, 0755);
	}

	// Set up server address
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sun_family = AF_UNIX;
	strncpy(server_addr.sun_path, SOCKET_PATH,
		sizeof(server_addr.sun_path) - 1);

	// Bind socket to address
	if (bind(server_fd, (struct sockaddr *)&server_addr,
		 sizeof(server_addr)) < 0) {
		close(server_fd);
		return TT_ERROR_NETWORK;
	}

	// Set socket permissions to allow write for all users
	if (chmod(SOCKET_PATH, S_IRUSR | S_IWUSR | S_IWGRP | S_IWOTH) < 0) {
		close(server_fd);
		unlink(SOCKET_PATH);
		return TT_ERROR_NETWORK;
	}

	ctx->comm.apex_fd = server_fd;
	return TT_SUCCESS;
}

// Cleanup Apex.OS Monitor UDS
void apex_monitor_cleanup(struct context *ctx)
{
	int server_fd = ctx->comm.apex_fd;

	if (server_fd != -1) {
		ctx->comm.apex_fd = -1;
		close(server_fd);
		unlink(SOCKET_PATH);
	}
}

// Receive a Apex.OS fault event
int apex_monitor_recv(struct context *ctx, char *name, int size, int *pid, int *type)
{
	int ret;
	int server_fd = ctx->comm.apex_fd;
	timpani_msg_t msg;

	ret = recvfrom(server_fd, &msg, sizeof(msg), 0, NULL, NULL);
	if (ret < 0) {
		if (errno == EAGAIN) {
			// No data available
			return TT_ERROR_IO;
		}
		return TT_ERROR_NETWORK;
	} else if (ret == 0) {
		// No data received
		return TT_ERROR_IO;
	}

	if (msg.msg_type == APEX_FAULT) {
		if (name) {
			strncpy(name, msg.data.fault.name, size - 1);
			name[size - 1] = '\0';
		}
	} else if (msg.msg_type == APEX_UP) {
		if (name) {
			strncpy(name, msg.data.up.name, size - 1);
			name[size - 1] = '\0';
		}
		if (pid) {
			*pid = msg.data.up.pid;
		}
	} else if (msg.msg_type == APEX_DOWN) {
		if (pid) {
			*pid = msg.data.down.pid;
		}
	} else if (msg.msg_type == APEX_RESET) {
		// RESET message received, it is for DEMO purpose only
	} else {
		TT_LOG_WARNING("Unknown Apex.OS message type: %d", msg.msg_type);
		return TT_ERROR_IO;
	}

	if (type) {
		*type = msg.msg_type;
	}
	// Data received
	return TT_SUCCESS;
}

// Initialize Apex.OS task list from sched_info
tt_error_t init_apex_list(struct context *ctx)
{
	int success_count = 0;

	// LIST_INIT is already invoked at config_set_defaults

	for (struct task_info *ti = ctx->runtime.sched_info.tasks; ti; ti = ti->next) {
		if (strcmp(ctx->config.node_id, ti->node_id) != 0) {
			/* The task does not belong to this node. */
			continue;
		}

		struct apex_info *apex_task = calloc(1, sizeof(struct apex_info));
		if (!apex_task) {
			TT_LOG_ERROR("Failed to allocate memory for Apex.OS task");
			continue;
		}
		memcpy(&apex_task->task, ti, sizeof(apex_task->task));

		LIST_INSERT_HEAD(&ctx->runtime.apex_list, apex_task, entry);
		TT_LOG_INFO("Initialized Apex.OS task: %s", ti->name);
		success_count++;
	}

	if (success_count == 0) {
		TT_LOG_ERROR("No tasks were successfully initialized");
		return TT_ERROR_CONFIG;
	}

	TT_LOG_INFO("Successfully initialized %d tasks", success_count);
	return TT_SUCCESS;
}
