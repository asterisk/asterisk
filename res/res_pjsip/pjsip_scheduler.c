/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2016, Fairview 5 Engineering, LLC
 *
 * George Joseph <george.joseph@fairview5.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 *
 * \brief res_pjsip Scheduler
 *
 * \author George Joseph <george.joseph@fairview5.com>
 */

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/res_pjsip.h"
#include "include/res_pjsip_private.h"
#include "asterisk/res_pjsip_cli.h"

#define TASK_BUCKETS 53

static struct ast_sched_context *scheduler_context;
static struct ao2_container *tasks;
static int task_count;

struct ast_sip_sched_task {
	/*! ast_sip_sched task id */
	uint32_t task_id;
	/*! ast_sched scheudler id */
	int current_scheduler_id;
	/*! task is currently running */
	int is_running;
	/*! task */
	ast_sip_task task;
	/*! task data */
	void *task_data;
	/*! reschedule interval in milliseconds */
	int interval;
	/*! the time the task was queued */
	struct timeval when_queued;
	/*! the last time the task was started */
	struct timeval last_start;
	/*! the last time the task was ended */
	struct timeval last_end;
	/*! times run */
	int run_count;
	/*! the task reschedule, cleanup and policy flags */
	enum ast_sip_scheduler_task_flags flags;
	/*! the serializer to be used (if any) */
	struct ast_taskprocessor *serializer;
	/* A name to be associated with the task */
	char name[0];
};

AO2_STRING_FIELD_HASH_FN(ast_sip_sched_task, name);
AO2_STRING_FIELD_CMP_FN(ast_sip_sched_task, name);
AO2_STRING_FIELD_SORT_FN(ast_sip_sched_task, name);

static int push_to_serializer(const void *data);

/*
 * This function is run in the context of the serializer.
 * It runs the task with a simple call and reschedules based on the result.
 */
static int run_task(void *data)
{
	RAII_VAR(struct ast_sip_sched_task *, schtd, ao2_bump(data), ao2_cleanup);
	int res;
	int delay;

	ao2_lock(schtd);
	schtd->last_start = ast_tvnow();
	schtd->is_running = 1;
	schtd->run_count++;
	ao2_unlock(schtd);

	res = schtd->task(schtd->task_data);

	ao2_lock(schtd);
	schtd->is_running = 0;
	schtd->last_end = ast_tvnow();

	/*
	 * Don't restart if the task returned 0 or if the interval
	 * was set to 0 while the task was running
	 */
	if (!res || !schtd->interval) {
		schtd->interval = 0;
		ao2_unlock(schtd);
		ao2_unlink(tasks, schtd);
		return -1;
	}

	if (schtd->flags & AST_SIP_SCHED_TASK_VARIABLE) {
		schtd->interval = res;
	}

	if (schtd->flags & AST_SIP_SCHED_TASK_DELAY) {
		delay = schtd->interval;
	} else {
		delay = schtd->interval - (ast_tvdiff_ms(schtd->last_end, schtd->last_start) % schtd->interval);
	}

	schtd->current_scheduler_id = ast_sched_add(scheduler_context, delay, push_to_serializer, (const void *)schtd);
	if (schtd->current_scheduler_id < 0) {
		schtd->interval = 0;
		ao2_unlock(schtd);
		ao2_unlink(tasks, schtd);
		return -1;
	}

	ao2_unlock(schtd);

	return 0;
}

/*
 * This function is run by the scheduler thread.  Its only job is to push the task
 * to the serialize and return.  It returns 0 so it's not rescheduled.
 */
static int push_to_serializer(const void *data)
{
	struct ast_sip_sched_task *schtd = (struct ast_sip_sched_task *)data;

	if (ast_sip_push_task(schtd->serializer, run_task, schtd)) {
		ao2_ref(schtd, -1);
	}

	return 0;
}

int ast_sip_sched_task_cancel(struct ast_sip_sched_task *schtd)
{
	int res;

	if (!ao2_ref_and_lock(schtd)) {
		return -1;
	}

	if (schtd->current_scheduler_id < 0 || schtd->interval <= 0) {
		ao2_unlock_and_unref(schtd);
		return 0;
	}

	schtd->interval = 0;
	ao2_unlock_and_unref(schtd);
	ao2_unlink(tasks, schtd);
	res = ast_sched_del(scheduler_context, schtd->current_scheduler_id);

	return res;
}

int ast_sip_sched_task_cancel_by_name(const char *name)
{
	RAII_VAR(struct ast_sip_sched_task *, schtd, NULL, ao2_cleanup);

	if (ast_strlen_zero(name)) {
		return -1;
	}

	schtd = ao2_find(tasks, name, OBJ_SEARCH_KEY | OBJ_NOLOCK);
	if (!schtd) {
		return -1;
	}

	return ast_sip_sched_task_cancel(schtd);
}


int ast_sip_sched_task_get_times(struct ast_sip_sched_task *schtd,
	struct timeval *queued, struct timeval *last_start, struct timeval *last_end)
{
	if (!ao2_ref_and_lock(schtd)) {
		return -1;
	}

	if (queued) {
		memcpy(queued, &schtd->when_queued, sizeof(struct timeval));
	}
	if (last_start) {
		memcpy(last_start, &schtd->last_start, sizeof(struct timeval));
	}
	if (last_end) {
		memcpy(last_end, &schtd->last_end, sizeof(struct timeval));
	}

	ao2_unlock_and_unref(schtd);

	return 0;
}

int ast_sip_sched_task_get_times_by_name(const char *name,
	struct timeval *queued, struct timeval *last_start, struct timeval *last_end)
{
	RAII_VAR(struct ast_sip_sched_task *, schtd, NULL, ao2_cleanup);

	if (ast_strlen_zero(name)) {
		return -1;
	}

	schtd = ao2_find(tasks, name, OBJ_SEARCH_KEY | OBJ_NOLOCK);
	if (!schtd) {
		return -1;
	}

	return ast_sip_sched_task_get_times(schtd, queued, last_start, last_end);
}

int ast_sip_sched_task_get_name(struct ast_sip_sched_task *schtd, char *name, size_t maxlen)
{
	if (maxlen <= 0) {
		return -1;
	}

	if (!ao2_ref_and_lock(schtd)) {
		return -1;
	}

	ast_copy_string(name, schtd->name, maxlen);

	ao2_unlock_and_unref(schtd);

	return 0;
}

int ast_sip_sched_task_get_next_run(struct ast_sip_sched_task *schtd)
{
	int delay;
	struct timeval since_when;
	struct timeval now;

	if (!ao2_ref_and_lock(schtd)) {
		return -1;
	}

	if (schtd->interval) {
		delay = schtd->interval;
		now = ast_tvnow();

		if (schtd->flags & AST_SIP_SCHED_TASK_DELAY) {
			since_when = schtd->is_running ? now : schtd->last_end;
		} else {
			since_when = schtd->last_start.tv_sec ? schtd->last_start : schtd->when_queued;
		}

		delay -= ast_tvdiff_ms(now, since_when);

		delay = delay < 0 ? 0 : delay;
	} else {
		delay = -1;
	}

	ao2_unlock_and_unref(schtd);

	return delay;
}

int ast_sip_sched_task_get_next_run_by_name(const char *name)
{
	RAII_VAR(struct ast_sip_sched_task *, schtd, NULL, ao2_cleanup);

	if (ast_strlen_zero(name)) {
		return -1;
	}

	schtd = ao2_find(tasks, name, OBJ_SEARCH_KEY | OBJ_NOLOCK);
	if (!schtd) {
		return -1;
	}

	return ast_sip_sched_task_get_next_run(schtd);
}

int ast_sip_sched_is_task_running(struct ast_sip_sched_task *schtd)
{
	if (!schtd) {
		return 0;
	}

	return schtd->is_running;
}

int ast_sip_sched_is_task_running_by_name(const char *name)
{
	RAII_VAR(struct ast_sip_sched_task *, schtd, NULL, ao2_cleanup);

	if (ast_strlen_zero(name)) {
		return 0;
	}

	schtd = ao2_find(tasks, name, OBJ_SEARCH_KEY | OBJ_NOLOCK);
	if (!schtd) {
		return 0;
	}

	return schtd->is_running;
}

static void schtd_destructor(void *data)
{
	struct ast_sip_sched_task *schtd = data;

	if (schtd->flags & AST_SIP_SCHED_TASK_DATA_AO2) {
		/* release our own ref, then release the callers if asked to do so */
		ao2_ref(schtd->task_data, (schtd->flags & AST_SIP_SCHED_TASK_DATA_FREE) ? -2 : -1);
	} else if (schtd->task_data && (schtd->flags & AST_SIP_SCHED_TASK_DATA_FREE)) {
		ast_free(schtd->task_data);
	}
}

struct ast_sip_sched_task *ast_sip_schedule_task(struct ast_taskprocessor *serializer,
	int interval, ast_sip_task sip_task, char *name, void *task_data, enum ast_sip_scheduler_task_flags flags)
{
#define ID_LEN 13 /* task_deadbeef */
	struct ast_sip_sched_task *schtd;
	int res;

	if (interval < 0) {
		return NULL;
	}

	schtd = ao2_alloc((sizeof(*schtd) + (!ast_strlen_zero(name) ? strlen(name) : ID_LEN) + 1), schtd_destructor);
	if (!schtd) {
		return NULL;
	}

	schtd->task_id = ast_atomic_fetchadd_int(&task_count, 1);
	schtd->serializer = serializer;
	schtd->task = sip_task;
	if (!ast_strlen_zero(name)) {
		strcpy(schtd->name, name); /* Safe */
	} else {
		sprintf(schtd->name, "task_%08x", schtd->task_id);
	}
	schtd->task_data = task_data;
	schtd->flags = flags;
	schtd->interval = interval;
	schtd->when_queued = ast_tvnow();

	if (flags & AST_SIP_SCHED_TASK_DATA_AO2) {
		ao2_ref(task_data, +1);
	}
	res = ast_sched_add(scheduler_context, interval, push_to_serializer, (const void *)schtd);
	if (res < 0) {
		ao2_ref(schtd, -1);
		return NULL;
	} else {
		schtd->current_scheduler_id = res;
		ao2_link(tasks, schtd);
	}

	return schtd;
#undef ID_LEN
}

static char *cli_show_tasks(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_iterator i;
	struct ast_sip_sched_task *schtd;
	const char *log_format = ast_logger_get_dateformat();
	struct ast_tm tm;
	char queued[32];
	char last_start[32];
	char last_end[32];
	int datelen;
	struct timeval now = ast_tvnow();
	const char *separator = "======================================";

	switch (cmd) {
	case CLI_INIT:
		e->command = "pjsip show scheduled_tasks";
		e->usage = "Usage: pjsip show scheduled_tasks\n"
		            "      Show all scheduled tasks\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	ast_localtime(&now, &tm, NULL);
	datelen = ast_strftime(queued, sizeof(queued), log_format, &tm);

	ast_cli(a->fd, "PJSIP Scheduled Tasks:\n\n");

	ast_cli(a->fd, " %1$-24s %2$-8s %3$-9s %4$-7s  %6$-*5$s  %7$-*5$s  %8$-*5$s\n",
		"Task Name", "Interval", "Times Run", "State",
		datelen, "Queued", "Last Started", "Last Ended");

	ast_cli(a->fd, " %1$-24.24s %2$-8.8s %3$-9.9s %4$-7.7s  %6$-*5$.*5$s  %7$-*5$.*5$s  %8$-*5$.*5$s\n",
		separator, separator, separator, separator,
		datelen, separator, separator, separator);


	ao2_ref(tasks, +1);
	ao2_rdlock(tasks);
	i = ao2_iterator_init(tasks, 0);
	while ((schtd = ao2_iterator_next(&i))) {

		ast_localtime(&schtd->when_queued, &tm, NULL);
		ast_strftime(queued, sizeof(queued), log_format, &tm);

		if (ast_tvzero(schtd->last_start)) {
			strcpy(last_start, "not yet started");
		} else {
			ast_localtime(&schtd->last_start, &tm, NULL);
			ast_strftime(last_start, sizeof(last_start), log_format, &tm);
		}

		if (ast_tvzero(schtd->last_end)) {
			if (ast_tvzero(schtd->last_start)) {
				strcpy(last_end, "not yet started");
			} else {
				strcpy(last_end, "running");
			}
		} else {
			ast_localtime(&schtd->last_end, &tm, NULL);
			ast_strftime(last_end, sizeof(last_end), log_format, &tm);
		}

		ast_cli(a->fd, " %1$-24.24s %2$-8.3f %3$-9d %4$-7s  %6$-*5$s  %7$-*5$s  %8$-*5$s\n",
			schtd->name,
			schtd->interval / 1000.0,
			schtd->run_count,
			schtd->is_running ? "running" : "waiting",
			datelen, queued, last_start, last_end);
		ao2_cleanup(schtd);
	}
	ao2_iterator_destroy(&i);
	ao2_unlock(tasks);
	ao2_ref(tasks, -1);
	ast_cli(a->fd, "\n");

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_commands[] = {
	AST_CLI_DEFINE(cli_show_tasks, "Show all scheduled tasks"),
};

int ast_sip_initialize_scheduler(void)
{
	if (!(scheduler_context = ast_sched_context_create())) {
		ast_log(LOG_ERROR, "Failed to create scheduler. Aborting load\n");
		return -1;
	}

	if (ast_sched_start_thread(scheduler_context)) {
		ast_log(LOG_ERROR, "Failed to start scheduler. Aborting load\n");
		ast_sched_context_destroy(scheduler_context);
		return -1;
	}

	tasks = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_RWLOCK, AO2_CONTAINER_ALLOC_OPT_DUPS_REJECT,
		TASK_BUCKETS, ast_sip_sched_task_hash_fn, ast_sip_sched_task_sort_fn, ast_sip_sched_task_cmp_fn);
	if (!tasks) {
		ast_log(LOG_ERROR, "Failed to allocate task container. Aborting load\n");
		ast_sched_context_destroy(scheduler_context);
		return -1;
	}

	ast_cli_register_multiple(cli_commands, ARRAY_LEN(cli_commands));

	return 0;
}

int ast_sip_destroy_scheduler(void)
{
	ast_cli_unregister_multiple(cli_commands, ARRAY_LEN(cli_commands));

	if (scheduler_context) {
		ast_sched_context_destroy(scheduler_context);
	}

	ao2_cleanup(tasks);
	tasks = NULL;

	return 0;
}
