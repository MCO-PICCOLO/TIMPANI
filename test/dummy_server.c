#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>	// struct timespec

#include <libtrpc.h>
#include <yaml.h>

#include "schedinfo.h"

#define SERVER_PORT	7777
#define CONTAINER_ID	"cc5c0d4ba8e10568df25f67b6f396da65c2615a4e6dd6f2f0ad554e9465fbb55"

static serial_buf_t *sbuf;
static struct sched_info sched_info;

static void parse_task(yaml_document_t *doc, yaml_node_t *node, struct sched_info *sinfo)
{
	struct task_info *tinfo;

	tinfo = malloc(sizeof(struct task_info));
	if (tinfo == NULL) {
		// TODO: out of memory
		return;
	}

	for (yaml_node_pair_t *pair = node->data.mapping.pairs.start;
	     pair < node->data.mapping.pairs.top; pair++) {
		yaml_node_t *key_node = yaml_document_get_node(doc, pair->key);
		yaml_node_t *value_node = yaml_document_get_node(doc, pair->value);

		if (key_node && value_node) {
			char *keystr = key_node->data.scalar.value;
			char *valstr = value_node->data.scalar.value;
			if (!strcmp(keystr, "name")) {
				strncpy(tinfo->name, valstr, sizeof(tinfo->name));
				tinfo->name[sizeof(tinfo->name) - 1] = '\0';
			} else if (!strcmp(keystr, "priority")) {
				tinfo->sched_priority = atoi(valstr);
			} else if (!strcmp(keystr, "policy")) {
				tinfo->sched_policy = atoi(valstr);
			} else if (!strcmp(keystr, "period")) {
				tinfo->period = atoi(valstr);
			} else if (!strcmp(keystr, "release")) {
				tinfo->release_time = atoi(valstr);
			} else if (!strcmp(keystr, "allowable_dmisses")) {
				tinfo->allowable_deadline_misses = atoi(valstr);
			} else if (!strcmp(keystr, "node_id")) {
				tinfo->node_id = atoi(valstr);
			}
		}
	}

	tinfo->pid = 0;
	tinfo->next = sinfo->tasks;
	sinfo->tasks = tinfo;
	sinfo->nr_tasks++;
}

static void parse_node(yaml_document_t *doc, yaml_node_t *node, struct sched_info *sinfo)
{
	for (yaml_node_pair_t *pair = node->data.mapping.pairs.start;
	     pair < node->data.mapping.pairs.top; pair++) {
		yaml_node_t *key_node = yaml_document_get_node(doc, pair->key);
		yaml_node_t *value_node = yaml_document_get_node(doc, pair->value);

		if (key_node && value_node) {
			char *keystr = key_node->data.scalar.value;
			char *valstr = value_node->data.scalar.value;
			if (!strcmp(keystr, "id")) {
				if (sinfo->nr_nodes >= SINFO_NODE_MAX) {
					printf("No. nodes(%u) exceeds max. value(%u) !\n",
						sinfo->nr_nodes, SINFO_NODE_MAX);
					break;
				}
				sinfo->node_ids[sinfo->nr_nodes] = atoi(valstr);
				sinfo->nr_nodes++;
			}
		}
	}
}

static void parse_task_list(yaml_document_t *doc, yaml_node_t *node, struct sched_info *sinfo)
{
	switch (node->type) {
	case YAML_SEQUENCE_NODE:
		for (yaml_node_item_t *item = node->data.sequence.items.start;
		     item < node->data.sequence.items.top; item++) {
			yaml_node_t *next_node = yaml_document_get_node(doc, *item);
			if (next_node && next_node->type == YAML_MAPPING_NODE) {
				parse_task(doc, next_node, sinfo);
			}
		}
		break;
	}
}

static void parse_node_list(yaml_document_t *doc, yaml_node_t *node, struct sched_info *sinfo)
{
	switch (node->type) {
	case YAML_SEQUENCE_NODE:
		for (yaml_node_item_t *item = node->data.sequence.items.start;
		     item < node->data.sequence.items.top; item++) {
			yaml_node_t *next_node = yaml_document_get_node(doc, *item);
			if (next_node && next_node->type == YAML_MAPPING_NODE) {
				parse_node(doc, next_node, sinfo);
			}
		}
		break;
	}
}

static void parse_root_node(yaml_document_t *doc, yaml_node_t *node, struct sched_info *sinfo)
{
	switch (node->type) {
	case YAML_MAPPING_NODE:
		for (yaml_node_pair_t *pair = node->data.mapping.pairs.start;
		     pair < node->data.mapping.pairs.top; pair++) {
			yaml_node_t *key_node = yaml_document_get_node(doc, pair->key);
			yaml_node_t *value_node = yaml_document_get_node(doc, pair->value);

			if (key_node) {
				char *keystr = key_node->data.scalar.value;

				if (!strcmp(keystr, "version")) {
					if (value_node) {
						char *valstr = value_node->data.scalar.value;
						printf("Version: %s\n", valstr);
					}
				} else if (!strcmp(keystr, "tasks")) {
					parse_task_list(doc, value_node, sinfo);
				} else if (!strcmp(keystr, "nodes")) {
					parse_node_list(doc, value_node, sinfo);
				}
			}
		}
		break;
	}
}

static int read_schedinfo_file(const char *filename, struct sched_info *sinfo)
{
	FILE *fh = fopen(filename, "r");
	if (!fh) {
		perror("Failed to open schedinfo file");
		return -1;
	}

	yaml_parser_t parser;
	yaml_document_t document;

	if (!yaml_parser_initialize(&parser)) {
		fprintf(stderr, "Failed to initialize parser!\n");
		fclose(fh);
		return -1;
	}

	// Set input file
	yaml_parser_set_input_file(&parser, fh);

	// Load YAML document
	if (!yaml_parser_load(&parser, &document)) {
		fprintf(stderr, "Failed to parse YAML document!\n");
		yaml_parser_delete(&parser);
		fclose(fh);
		return -1;
	}

	// Start parsing from the root node
	yaml_node_t *root_node = yaml_document_get_root_node(&document);
	if (!root_node) {
		yaml_document_delete(&document);
		yaml_parser_delete(&parser);
		fclose(fh);
		return -1;
	}
	parse_root_node(&document, root_node, sinfo);

	// Clean up
	yaml_document_delete(&document);
	yaml_parser_delete(&parser);
	fclose(fh);

	return 0;
}

static int is_node_valid(uint32_t node_id, struct sched_info *sinfo)
{
	int i;

	for (i = 0; i < sinfo->nr_nodes; i++) {
		if (sinfo->node_ids[i] == node_id) {
			return 1;
		}

	}
	return 0;
}

static int init_schedinfo(const char *fname, struct sched_info *sinfo)
{
	int ret;

	memcpy(sinfo->container_id, CONTAINER_ID, sizeof(CONTAINER_ID) - 1);
	sinfo->container_rt_runtime = 800000;
	sinfo->container_rt_period = 1000000;
	sinfo->cpumask = 0xffffffff;
	sinfo->container_period = 1000000;
	sinfo->pod_period = 1000000;
	sinfo->nr_tasks = 0;
	sinfo->tasks = NULL;

	ret = read_schedinfo_file(fname, sinfo);

	return ret;
}

static void serialize_schedinfo(struct sched_info *sinfo)
{
	uint32_t nr_tasks = 0;

	if (sbuf) return;

	sbuf = new_serial_buf(256);
	assert(sbuf);

	printf("sinfo->nr_tasks: %u\n", sinfo->nr_tasks);

	// Pack task_info list entries into serial buffer.
	for (struct task_info *t = sinfo->tasks; t; t = t->next) {
		printf("t->pid: %u\n", t->pid);
		printf("t->name: %s\n", t->name);
		printf("t->sched_priority: %u\n", t->sched_priority);
		printf("t->sched_policy: %u\n", t->sched_policy);
		printf("t->period: %u\n", t->period);
		printf("t->release_time: %u\n", t->release_time);
		printf("t->allowable_deadline_misses: %u\n", t->allowable_deadline_misses);
		printf("t->node_id: %u\n", t->node_id);

		serialize_int32_t(sbuf, t->pid);
		serialize_str(sbuf, t->name);
		serialize_int32_t(sbuf, t->sched_priority);
		serialize_int32_t(sbuf, t->sched_policy);
		serialize_int32_t(sbuf, t->period);
		serialize_int32_t(sbuf, t->release_time);
		serialize_int32_t(sbuf, t->allowable_deadline_misses);
		serialize_int32_t(sbuf, t->node_id);
		nr_tasks++;
	}

	if (nr_tasks != sinfo->nr_tasks) {
		printf("WARNING: counted nr_tasks(%u) is different from sched_info->nr_task(%u)\n",
			nr_tasks, sinfo->nr_tasks);
	}

	// Pack sched_info into serial buffer.
	// NOTE: Node info is not used by Timpani-N, so there is no need to send it to them.
	serialize_blob(sbuf, sinfo->container_id, sizeof(sinfo->container_id));
	serialize_int32_t(sbuf, sinfo->container_rt_runtime);
	serialize_int32_t(sbuf, sinfo->container_rt_period);
	serialize_int64_t(sbuf, sinfo->cpumask);
	serialize_int32_t(sbuf, sinfo->container_period);
	serialize_int32_t(sbuf, sinfo->pod_period);
	serialize_int32_t(sbuf, sinfo->nr_tasks);
}

static void register_callback(const char *name)
{
	uint32_t node_id = atoi(name);

	if (!is_node_valid(node_id, &sched_info)) {
		printf("Register: invalid node: %u\n", node_id);
		return;
	}

	printf("Register: node: %u\n", node_id);
}

static void schedinfo_callback(const char *name, void **buf, size_t *bufsize)
{
	uint32_t node_id = atoi(name);

	if (!is_node_valid(node_id, &sched_info)) {
		printf("SchedInfo: invalid node: %u\n", node_id);
		*buf = NULL;
		bufsize = 0;
		return;
	}

	printf("SchedInfo: node: %u\n", node_id);

	serialize_schedinfo(&sched_info);

	printf("sbuf size: %zu\n", sbuf->pos);

	if (buf)
		*buf = sbuf->data;
	if (bufsize)
		*bufsize = sbuf->pos;
}

static void dmiss_callback(const char *name, const char *task)
{
	uint32_t node_id = atoi(name);

	if (!is_node_valid(node_id, &sched_info)) {
		printf("DMiss: invalid node: %u\n", node_id);
		return;
	}

	printf("!!! DEADLINE MISS: %s @ %s !!!\n", task, name);
}

static void sync_callback(const char *name, int *ack, struct timespec *ts)
{
	/* array for marking each node ready status */
	static uint32_t node_ready[SINFO_NODE_MAX];
	/* The number of nodes that are ready */
	static uint32_t no_ready;

	int i;
	uint32_t node_id = atoi(name);

	if (!is_node_valid(node_id, &sched_info)) {
		printf("Sync: invalid node: %u\n", node_id);
		*ack = 0;
		return;
	}

	printf("Sync: node: %u\n", node_id);

	// check if this node is already set in node_ready[]
	for (i = 0; i < no_ready; i++) {
		if (node_ready[i] == node_id) {
			/* No need to set */
			break;
		}
	}
	if (i == no_ready) {
		/* Mark it as ready if it is not found in node_ready[] */
		node_ready[no_ready] = node_id;
		no_ready++;
	}

	if (no_ready == sched_info.nr_nodes) {
		/* ACK: All nodes are ready */
		*ack = 1;

		clock_gettime(CLOCK_REALTIME, ts);

		/* Add extra time to start timer value */
		ts->tv_sec += 1;

		printf("Sync: ACK %u with %ld sec %ld nsec\n",
			node_id, ts->tv_sec, ts->tv_nsec);
		return;
	}

	/* NACK */
	printf("Sync: NACK %u\n", node_id);
	*ack = 0;
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
		.dmiss_cb = dmiss_callback,
		.sync_cb = sync_callback,
	};
	uint32_t port = SERVER_PORT;
	const char *sinfo_fname = "schedinfo.yaml";

	if (argc > 1) {
		sinfo_fname = argv[1];
		if (argc > 2) {
			port = atoi(argv[2]);
		}
	}

	ret = init_schedinfo(sinfo_fname, &sched_info);
	if (ret < 0) {
		goto out;
	}

	ret = sd_event_default(&event);
	if (ret < 0) {
		fprintf(stderr, "%s:%d: %s\n", __FUNCTION__, __LINE__, strerror(-ret));
		goto out;
	}

	ret = trpc_server_create(port, event, &event_source, &ops);
	if (ret < 0) {
		fprintf(stderr, "%s:%d: %s\n", __FUNCTION__, __LINE__, strerror(-ret));
		goto out;
	}
	fd = ret;
	printf("Listening on %u...\n", port);

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
