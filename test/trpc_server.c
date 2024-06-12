#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#include <libtrpc.h>

static void register_callback(const char *name)
{
	printf("Register: %s\n", name);
}

static void schedinfo_callback(const char *name, void **buf, size_t *bufsize)
{
	static serial_buf_t *sbuf;

	printf("SchedInfo: %s\n", name);

	if (sbuf == NULL) {
		uint64_t u64 = 0xdeadbeef;
		char str[] = "Hello, world!";

		sbuf = new_serial_buf(0);
		assert(sbuf);

		serialize_str(sbuf, str);
		serialize_int64_t(sbuf, u64);
	}

	if (buf)
		*buf = sbuf->data;
	if (bufsize)
		*bufsize = sbuf->pos;
}

int main(int argc, char *argv[])
{
	sd_event_source *event_source = NULL;
	sd_event *event = NULL;
	int fd = -1;
	int ret;
	trpc_server_ops_t ops = {
		.register_cb = register_callback,
		.schedinfo_cb = schedinfo_callback,
	};

	ret = sd_event_default(&event);
	if (ret < 0) {
		fprintf(stderr, "%s:%d: %s\n", __FUNCTION__, __LINE__, strerror(-ret));
		goto out;
	}

	ret = trpc_server_create(7777, event, &event_source, &ops);
	if (ret < 0) {
		fprintf(stderr, "%s:%d: %s\n", __FUNCTION__, __LINE__, strerror(-ret));
		goto out;
	}
	fd = ret;

	ret = sd_event_loop(event);
	if (ret < 0)
		goto out;

out:
	event_source = sd_event_source_unref(event_source);
	event = sd_event_unref(event);
	if (fd >= 0)
		close(fd);
	return ret < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
