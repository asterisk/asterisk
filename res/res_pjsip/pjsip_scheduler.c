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

#include "asterisk/res_pjsip.h"
#include "include/res_pjsip_private.h"
#include "asterisk/res_pjsip_cli.h"
#include "asterisk/taskprocessor.h"

#define TASK_BUCKETS 53

static struct ast_sched_context *scheduler_context;
static struct ao2_container *tasks;
static int task_count;

struct ast_sip_sched_task {
	/*! The serializer to be used (if any) (Holds a ref) */
	struct ast_taskprocessor *serializer;
	/*! task data */
	void *task_data;
	/*! task function */
	ast_sip_task task;
	/*! the time the task was originally scheduled/queued */
	struct timeval when_queued;
	/*! the last time the task was started */
	struct timeval last_start;
	/*! the last time the task was ended */
	struct timeval last_end;
	/*! When the periodic task is next expected to run */
	struct timeval next_periodic;
	/*! reschedule interval in milliseconds */
	int interval;
	/*! ast_sched scheudler id */
	int current_scheduler_id;
	/*! task is currently running */
	int is_running;
	/*! times run */
	int run_count;
	/*! the task reschedule, cleanup and policy flags */
	enum ast_sip_scheduler_task_flags flags;
	/*! A name to be associated with the task */
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
	RAII_VAR(struct ast_sip_sched_task *, schtd, data, ao2_cleanup);
	int res;
	int delay;

	if (!schtd->interval) {
		/* Task was cancelled while waiting to be executed by the serializer */
		return -1;
	}

	if (schtd->flags & AST_SIP_SCHED_TASK_TRACK) {
		ast_log(LOG_DEBUG, "Sched %p: Running %s\n", schtd, schtd->name);
	}
	ao2_lock(schtd);
	schtd->last_start = ast_tvnow();
	schtd->is_running = 1;
	++schtd->run_count;
	ao2_unlock(schtd);

	res = schtd->task(schtd->task_data);

	ao2_lock(schtd);
	schtd->is_running = 0;
	schtd->last_end = ast_tvnow();

	/*
	 * Don't restart if the task returned <= 0 or if the interval
	 * was set to 0 while the task was running
	 */
	if (res <= 0 || !schtd->interval) {
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
		int64_t diff;

		/* Determine next periodic interval we need to expire. */
		do {
			schtd->next_periodic = ast_tvadd(schtd->next_periodic,
				ast_samp2tv(schtd->interval, 1000));
			diff = ast_tvdiff_ms(schtd->next_periodic, schtd->last_end);
		} while (diff <= 0);
		delay = diff;
	}

	schtd->current_scheduler_id = ast_sched_add(scheduler_context, delay, push_to_serializer, schtd);
	if (schtd->current_scheduler_id < 0) {
		schtd->interval = 0;
		ao2_unlock(schtd);
		ast_log(LOG_ERROR, "Sched %p: Failed to reschedule task %s\n", schtd, schtd->name);
		ao2_unlink(tasks, schtd);
		return -1;
	}

	ao2_unlock(schtd);
	if (schtd->flags & AST_SIP_SCHED_TASK_TRACK) {
		ast_log(LOG_DEBUG, "Sched %p: Rescheduled %s for %d ms\n", schtd, schtd->name,
			delay);
	}

	return 0;
}

/*
 * This function is run by the scheduler thread.  Its only job is to push the task
 * to the serialize and return.  It returns 0 so it's not rescheduled.
 */
static int push_to_serializer(const void *data)
{
	struct ast_sip_sched_task *schtd = (struct ast_sip_sched_task *)data;
	int sched_id;

	ao2_lock(schtd);
	sched_id = schtd->current_scheduler_id;
	schtd->current_scheduler_id = -1;
	ao2_unlock(schtd);
	if (sched_id < 0) {
		/* Task was cancelled while waiting on the lock */
		return 0;
	}

	if (schtd->flags & AST_SIP_SCHED_TASK_TRACK) {
		ast_log(LOG_DEBUG, "Sched %p: Ready to run %s\n", schtd, schtd->name);
	}
	ao2_t_ref(schtd, +1, "Give ref to run_task()");
	if (ast_sip_push_task(schtd->serializer, run_task, schtd)) {
		/*
		 * Oh my.  Have to cancel the scheduled item because we
		 * unexpectedly cannot run it anymore.
		 */
		ao2_unlink(tasks, schtd);
		ao2_lock(schtd);
		schtd->interval = 0;
		ao2_unlock(schtd);

		ao2_t_ref(schtd, -1, "Failed so release run_task() ref");
	}

	return 0;
}

int ast_sip_sched_task_cancel(struct ast_sip_sched_task *schtd)
{
	int res;
	int sched_id;

	if (schtd->flags & AST_SIP_SCHED_TASK_TRACK) {
		ast_log(LOG_DEBUG, "Sched %p: Canceling %s\n", schtd, schtd->name);
	}

	/*
	 * Prevent any tasks in the serializer queue from
	 * running and restarting the scheduled item on us
	 * first.
	 */
	ao2_lock(schtd);
	schtd->interval = 0;

	sched_id = schtd->current_scheduler_id;
	schtd->current_scheduler_id = -1;
	ao2_unlock(schtd);
	res = ast_sched_del(scheduler_context, sched_id);

	ao2_unlink(tasks, schtd);

	return res;
}

int ast_sip_sched_task_cancel_by_name(const char *name)
{
	int res;
	struct ast_sip_sched_task *schtd;

	if (ast_strlen_zero(name)) {
		return -1;
	}

	schtd = ao2_find(tasks, name, OBJ_SEARCH_KEY);
	if (!schtd) {
		return -1;
	}

	res = ast_sip_sched_task_cancel(schtd);
	ao2_ref(schtd, -1);
	return res;
}


int ast_sip_sched_task_get_times(struct ast_sip_sched_task *schtd,
	struct timeval *queued, struct timeval *last_start, struct timeval *last_end)
{
	ao2_lock(schtd);
	if (queued) {
		memcpy(queued, &schtd->when_queued, sizeof(struct timeval));
	}
	if (last_start) {
		memcpy(last_start, &schtd->last_start, sizeof(struct timeval));
	}
	if (last_end) {
		memcpy(last_end, &schtd->last_end, sizeof(struct timeval));
	}
	ao2_unlock(schtd);

	return 0;
}

int ast_sip_sched_task_get_times_by_name(const char *name,
	struct timeval *queued, struct timeval *last_start, struct timeval *last_end)
{
	int res;
	struct ast_sip_sched_task *schtd;

	if (ast_strlen_zero(name)) {
		return -1;
	}

	schtd = ao2_find(tasks, name, OBJ_SEARCH_KEY);
	if (!schtd) {
		return -1;
	}

	res = ast_sip_sched_task_get_times(schtd, queued, last_start, last_end);
	ao2_ref(schtd, -1);
	return res;
}

int ast_sip_sched_task_get_name(struct ast_sip_sched_task *schtd, char *name, size_t maxlen)
{
	if (maxlen <= 0) {
		return -1;
	}

	ao2_lock(schtd);
	ast_copy_string(name, schtd->name, maxlen);
	ao2_unlock(schtd);

	return 0;
}

int ast_sip_sched_task_get_next_run(struct ast_sip_sched_task *schtd)
{
	int delay;
	struct timeval since_when;
	struct timeval now;

	ao2_lock(schtd);

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

	ao2_unlock(schtd);

	return delay;
}

int ast_sip_sched_task_get_next_run_by_name(const char *name)
{
	int next_run;
	struct ast_sip_sched_task *schtd;

	if (ast_strlen_zero(name)) {
		return -1;
	}

	schtd = ao2_find(tasks, name, OBJ_SEARCH_KEY);
	if (!schtd) {
		return -1;
	}

	next_run = ast_sip_sched_task_get_next_run(schtd);
	ao2_ref(schtd, -1);
	return next_run;
}

int ast_sip_sched_is_task_running(struct ast_sip_sched_task *schtd)
{
	return schtd ? schtd->is_running : 0;
}

int ast_sip_sched_is_task_running_by_name(const char *name)
{
	int is_running;
	struct ast_sip_sched_task *schtd;

	if (ast_strlen_zero(name)) {
		return 0;
	}

	schtd = ao2_find(tasks, name, OBJ_SEARCH_KEY);
	if (!schtd) {
		return 0;
	}

	is_running = schtd->is_running;
	ao2_ref(schtd, -1);
	return is_running;
}

static void schtd_dtor(void *data)
{
	struct ast_sip_sched_task *schtd = data;

	if (schtd->flags & AST_SIP_SCHED_TASK_TRACK) {
		ast_log(LOG_DEBUG, "Sched %p: Destructor %s\n", schtd, schtd->name);
	}
	if (schtd->flags & AST_SIP_SCHED_TASK_DATA_AO2) {
		/* release our own ref, then release the callers if asked to do so */
		ao2_ref(schtd->task_data, (schtd->flags & AST_SIP_SCHED_TASK_DATA_FREE) ? -2 : -1);
	} else if (schtd->task_data && (schtd->flags & AST_SIP_SCHED_TASK_DATA_FREE)) {
		ast_free(schtd->task_data);
	}
	ast_taskprocessor_unreference(schtd->serializer);
}

struct ast_sip_sched_task *ast_sip_schedule_task(struct ast_taskprocessor *serializer,
	int interval, ast_sip_task sip_task, const char *name, void *task_data,
	enum ast_sip_scheduler_task_flags flags)
{
#define ID_LEN 13 /* task_deadbeef */
	struct ast_sip_sched_task *schtd;
	int res;

	if (interval <= 0) {
		return NULL;
	}

	schtd = ao2_alloc((sizeof(*schtd) + (!ast_strlen_zero(name) ? strlen(name) : ID_LEN) + 1),
		schtd_dtor);
	if (!schtd) {
		return NULL;
	}

	schtd->serializer = ao2_bump(serializer);
	schtd->task_data = task_data;
	schtd->task = sip_task;
	schtd->interval = interval;
	schtd->flags = flags;
	if (!ast_strlen_zero(name)) {
		strcpy(schtd->name, name); /* Safe */
	} else {
		uint32_t task_id;

		task_id = ast_atomic_fetchadd_int(&task_count, 1);
		sprintf(schtd->name, "task_%08x", task_id);
	}
	if (schtd->flags & AST_SIP_SCHED_TASK_TRACK) {
		ast_log(LOG_DEBUG, "Sched %p: Scheduling %s for %d ms\n", schtd, schtd->name,
			interval);
	}
	schtd->when_queued = ast_tvnow();
	if (!(schtd->flags & AST_SIP_SCHED_TASK_DELAY)) {
		schtd->next_periodic = ast_tvadd(schtd->when_queued,
			ast_samp2tv(schtd->interval, 1000));
	}

	if (flags & AST_SIP_SCHED_TASK_DATA_AO2) {
		ao2_ref(task_data, +1);
	}

	/*
	 * We must put it in the 'tasks' container before scheduling
	 * the task because we don't want the push_to_serializer()
	 * sched task to "remove" it on failure before we even put
	 * it in.  If this happens then nothing would remove it from
	 * the 'tasks' container.
	 */
	ao2_link(tasks, schtd);

	/*
	 * Lock so we are guaranteed to get the sched id set before
	 * the push_to_serializer() sched task can clear it.
	 */
	ao2_lock(schtd);
	res = ast_sched_add(scheduler_context, interval, push_to_serializer, schtd);
	schtd->current_scheduler_id = res;
	ao2_unlock(schtd);
	if (res < 0) {
		ao2_unlink(tasks, schtd);
		ao2_ref(schtd, -1);
		return NULL;
	}

	return schtd;
#undef ID_LEN
}

static char *cli_show_tasks(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_iterator iter;
	struct ao2_container *sorted_tasks;
	struct ast_sip_sched_task *schtd;
	const char *log_format;
	struct ast_tm tm;
	char queued[32];
	char last_start[32];
	char next_start[32];
	int datelen;
	struct timeval now;
	static const char separator[] = "=============================================";

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

	/* Get a sorted snapshot of the scheduled tasks */
	sorted_tasks = ao2_container_alloc_rbtree(AO2_ALLOC_OPT_LOCK_NOLOCK, 0,
		ast_sip_sched_task_sort_fn, NULL);
	if (!sorted_tasks) {
		return CLI_SUCCESS;
	}
	if (ao2_container_dup(sorted_tasks, tasks, 0)) {
		ao2_ref(sorted_tasks, -1);
		return CLI_SUCCESS;
	}

	now = ast_tvnow();
	log_format = ast_logger_get_dateformat();

	ast_localtime(&now, &tm, NULL);
	datelen = ast_strftime(queued, sizeof(queued), log_format, &tm);

	ast_cli(a->fd, "PJSIP Scheduled Tasks:\n\n");

	ast_cli(a->fd, "%1$-45s %2$-9s %3$-9s %4$-5s  %6$-*5$s  %7$-*5$s  %8$-*5$s %9$7s\n",
		"Task Name", "Interval", "Times Run", "State",
		datelen, "Queued", "Last Started", "Next Start", "( secs)");

	ast_cli(a->fd, "%1$-45.45s %2$-9.9s %3$-9.9s %4$-5.5s  %6$-*5$.*5$s  %7$-*5$.*5$s  %9$-*8$.*8$s\n",
		separator, separator, separator, separator,
		datelen, separator, separator, datelen + 8, separator);

	iter = ao2_iterator_init(sorted_tasks, AO2_ITERATOR_UNLINK);
	for (; (schtd = ao2_iterator_next(&iter)); ao2_ref(schtd, -1)) {
		int next_run_sec;
		struct timeval next;

		ao2_lock(schtd);

		next_run_sec = ast_sip_sched_task_get_next_run(schtd) / 1000;
		if (next_run_sec < 0) {
			/* Scheduled task is now canceled */
			ao2_unlock(schtd);
			continue;
		}
		next = ast_tvadd(now, ast_tv(next_run_sec, 0));

		ast_localtime(&schtd->when_queued, &tm, NULL);
		ast_strftime(queued, sizeof(queued), log_format, &tm);

		if (ast_tvzero(schtd->last_start)) {
			strcpy(last_start, "not yet started");
		} else {
			ast_localtime(&schtd->last_start, &tm, NULL);
			ast_strftime(last_start, sizeof(last_start), log_format, &tm);
		}

		ast_localtime(&next, &tm, NULL);
		ast_strftime(next_start, sizeof(next_start), log_format, &tm);

		ast_cli(a->fd, "%1$-46.46s%2$9.3f %3$9d %4$-5s  %6$-*5$s  %7$-*5$s  %8$-*5$s (%9$5d)\n",
			schtd->name,
			schtd->interval / 1000.0,
			schtd->run_count,
			schtd->is_running ? "run" : "wait",
			datelen, queued, last_start,
			next_start,
			next_run_sec);
		ao2_unlock(schtd);
	}
	ao2_iterator_destroy(&iter);
	ao2_ref(sorted_tasks, -1);
	ast_cli(a->fd, "\n");

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_commands[] = {
	AST_CLI_DEFINE(cli_show_tasks, "Show all scheduled tasks"),
};

int ast_sip_initialize_scheduler(void)
{
	scheduler_context = ast_sched_context_create();
	if (!scheduler_context) {
		ast_log(LOG_ERROR, "Failed to create scheduler. Aborting load\n");
		return -1;
	}

	if (ast_sched_start_thread(scheduler_context)) {
		ast_log(LOG_ERROR, "Failed to start scheduler. Aborting load\n");
		ast_sched_context_destroy(scheduler_context);
		return -1;
	}

	tasks = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_RWLOCK,
		AO2_CONTAINER_ALLOC_OPT_DUPS_REJECT, TASK_BUCKETS, ast_sip_sched_task_hash_fn,
		ast_sip_sched_task_sort_fn, ast_sip_sched_task_cmp_fn);
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
		if (tasks) {
			struct ao2_iterator iter;
			struct ast_sip_sched_task *schtd;

			/* Cancel all scheduled tasks */
			iter = ao2_iterator_init(tasks, 0);
			while ((schtd = ao2_iterator_next(&iter))) {
				ast_sip_sched_task_cancel(schtd);
				ao2_ref(schtd, -1);
			}
			ao2_iterator_destroy(&iter);
		}

		ast_sched_context_destroy(scheduler_context);
		scheduler_context = NULL;
	}

	ao2_cleanup(tasks);
	tasks = NULL;

	return 0;
}
