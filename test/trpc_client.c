#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libtrpc.h>

int main(int argc, char *argv[])
{
	sd_event *event = NULL;
	sd_bus *dbus = NULL;
	int fd = -1;
	int ret;
	void *buf = NULL;
	size_t bufsize;
	serial_buf_t *sbuf = NULL;
	uint64_t u64;
	char str[64];

	ret = sd_event_default(&event);
	if (ret < 0) {
		fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, strerror(-ret));
		goto out;
	}

	ret = trpc_client_create("tcp:host=localhost,port=7777", event, &dbus);
	if (ret < 0) {
		fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, strerror(-ret));
		goto out;
	}
	fd = ret;

	ret = trpc_client_register(dbus, "Timpani-N");
	if (ret < 0) {
		fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, strerror(-ret));
		goto out;
	}

	ret = trpc_client_schedinfo(dbus, "Timpani-N", &buf, &bufsize);
	if (ret < 0) {
		fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, strerror(-ret));
		goto out;
	}

	sbuf = make_serial_buf((void *)buf, bufsize);
	if (sbuf == NULL) {
		fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, strerror(-ret));
		goto out;
	}
	buf = NULL;	// now use sbuf->data

	deserialize_int64_t(sbuf, &u64);
	deserialize_str(sbuf, str);

	printf("str: %s\n", str);
	printf("u64: %"PRIx64"\n", u64);

	free_serial_buf(sbuf);

	ret = sd_event_loop(event);
	if (ret < 0)
		goto out;

out:
	event = sd_event_unref(event);
	return ret < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
