#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include "internal.h"

#define SOCKET_DIR "/var/run/timpani/"
#define SOCKET_FILE "timpani.sock"
#define SOCKET_PATH SOCKET_DIR SOCKET_FILE

// Communication message structure for Apex.OS
#define MAX_APEX_NAME_LEN 256
typedef struct {
	char name[MAX_APEX_NAME_LEN];
	int type;
} timpani_msg_t;

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

void apex_monitor_cleanup(struct context *ctx)
{
	int server_fd = ctx->comm.apex_fd;

	if (server_fd != -1) {
		ctx->comm.apex_fd = -1;
		close(server_fd);
		unlink(SOCKET_PATH);
	}
}

int apex_monitor_recv(struct context *ctx, char *name, int size)
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
	TT_LOG_DEBUG("%s %d", msg.name, msg.type);

	if (name) {
		strncpy(name, msg.name, size - 1);
		name[size - 1] = '\0';
	}
	// Data received
	return TT_SUCCESS;
}
