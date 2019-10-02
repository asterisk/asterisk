/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007-2013, Digium, Inc.
 *
 * Dwayne M. Hubbard <dhubbard@digium.com>
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

/*!
 * \file
 * \brief Maintain a container of uniquely-named taskprocessor threads that can be shared across modules.
 *
 * \author Dwayne Hubbard <dhubbard@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/_private.h"
#include "asterisk/module.h"
#include "asterisk/time.h"
#include "asterisk/astobj2.h"
#include "asterisk/cli.h"
#include "asterisk/taskprocessor.h"
#include "asterisk/sem.h"

/*!
 * \brief tps_task structure is queued to a taskprocessor
 *
 * tps_tasks are processed in FIFO order and freed by the taskprocessing
 * thread after the task handler returns.  The callback function that is assigned
 * to the execute() function pointer is responsible for releasing datap resources if necessary.
 */
struct tps_task {
	/*! \brief The execute() task callback function pointer */
	union {
		int (*execute)(void *datap);
		int (*execute_local)(struct ast_taskprocessor_local *local);
	} callback;
	/*! \brief The data pointer for the task execute() function */
	void *datap;
	/*! \brief AST_LIST_ENTRY overhead */
	AST_LIST_ENTRY(tps_task) list;
	unsigned int wants_local:1;
};

/*! \brief tps_taskprocessor_stats maintain statistics for a taskprocessor. */
struct tps_taskprocessor_stats {
	/*! \brief This is the maximum number of tasks queued at any one time */
	unsigned long max_qsize;
	/*! \brief This is the current number of tasks processed */
	unsigned long _tasks_processed_count;
};

/*! \brief A ast_taskprocessor structure is a singleton by name */
struct ast_taskprocessor {
	/*! \brief Taskprocessor statistics */
	struct tps_taskprocessor_stats stats;
	void *local_data;
	/*! \brief Taskprocessor current queue size */
	long tps_queue_size;
	/*! \brief Taskprocessor low water clear alert level */
	long tps_queue_low;
	/*! \brief Taskprocessor high water alert trigger level */
	long tps_queue_high;
	/*! \brief Taskprocessor queue */
	AST_LIST_HEAD_NOLOCK(tps_queue, tps_task) tps_queue;
	struct ast_taskprocessor_listener *listener;
	/*! Current thread executing the tasks */
	pthread_t thread;
	/*! Indicates if the taskprocessor is currently executing a task */
	unsigned int executing:1;
	/*! Indicates that a high water warning has been issued on this task processor */
	unsigned int high_water_warned:1;
	/*! Indicates that a high water alert is active on this taskprocessor */
	unsigned int high_water_alert:1;
	/*! Indicates if the taskprocessor is currently suspended */
	unsigned int suspended:1;
	/*! \brief Anything before the first '/' in the name (if there is one) */
	char *subsystem;
	/*! \brief Friendly name of the taskprocessor.
	 * Subsystem is appended after the name's NULL terminator.
	 */
	char name[0];
};

/*!
 * \brief A listener for taskprocessors
 *
 * \since 12.0.0
 *
 * When a taskprocessor's state changes, the listener
 * is notified of the change. This allows for tasks
 * to be addressed in whatever way is appropriate for
 * the module using the taskprocessor.
 */
struct ast_taskprocessor_listener {
	/*! The callbacks the taskprocessor calls into to notify of state changes */
	const struct ast_taskprocessor_listener_callbacks *callbacks;
	/*! The taskprocessor that the listener is listening to */
	struct ast_taskprocessor *tps;
	/*! Data private to the listener */
	void *user_data;
};

/*!
 * Keep track of which subsystems are in alert
 * and how many of their taskprocessors are overloaded.
 */
struct subsystem_alert {
	unsigned int alert_count;
	char subsystem[0];
};
static AST_VECTOR_RW(subsystem_alert_vector, struct subsystem_alert *) overloaded_subsystems;

#ifdef LOW_MEMORY
#define TPS_MAX_BUCKETS 61
#else
/*! \brief Number of buckets in the tps_singletons container. */
#define TPS_MAX_BUCKETS 1567
#endif

/*! \brief tps_singletons is the astobj2 container for taskprocessor singletons */
static struct ao2_container *tps_singletons;

/*! \brief CLI <example>taskprocessor ping &lt;blah&gt;</example> operation requires a ping condition */
static ast_cond_t cli_ping_cond;

/*! \brief CLI <example>taskprocessor ping &lt;blah&gt;</example> operation requires a ping condition lock */
AST_MUTEX_DEFINE_STATIC(cli_ping_cond_lock);

/*! \brief The astobj2 hash callback for taskprocessors */
static int tps_hash_cb(const void *obj, const int flags);
/*! \brief The astobj2 compare callback for taskprocessors */
static int tps_cmp_cb(void *obj, void *arg, int flags);

/*! \brief CLI <example>taskprocessor ping &lt;blah&gt;</example> handler function */
static int tps_ping_handler(void *datap);

static char *cli_tps_ping(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *cli_tps_report(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *cli_subsystem_alert_report(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *cli_tps_reset_stats(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *cli_tps_reset_stats_all(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);

static struct ast_cli_entry taskprocessor_clis[] = {
	AST_CLI_DEFINE(cli_tps_ping, "Ping a named task processor"),
	AST_CLI_DEFINE(cli_tps_report, "List instantiated task processors and statistics"),
	AST_CLI_DEFINE(cli_subsystem_alert_report, "List task processor subsystems in alert"),
	AST_CLI_DEFINE(cli_tps_reset_stats, "Reset a named task processor's stats"),
	AST_CLI_DEFINE(cli_tps_reset_stats_all, "Reset all task processors' stats"),
};

struct default_taskprocessor_listener_pvt {
	pthread_t poll_thread;
	int dead;
	struct ast_sem sem;
};

static void default_listener_pvt_destroy(struct default_taskprocessor_listener_pvt *pvt)
{
	ast_assert(pvt->dead);
	ast_sem_destroy(&pvt->sem);
	ast_free(pvt);
}

static void default_listener_pvt_dtor(struct ast_taskprocessor_listener *listener)
{
	struct default_taskprocessor_listener_pvt *pvt = listener->user_data;

	default_listener_pvt_destroy(pvt);

	listener->user_data = NULL;
}

/*!
 * \brief Function that processes tasks in the taskprocessor
 * \internal
 */
static void *default_tps_processing_function(void *data)
{
	struct ast_taskprocessor_listener *listener = data;
	struct ast_taskprocessor *tps = listener->tps;
	struct default_taskprocessor_listener_pvt *pvt = listener->user_data;
	int sem_value;
	int res;

	while (!pvt->dead) {
		res = ast_sem_wait(&pvt->sem);
		if (res != 0 && errno != EINTR) {
			ast_log(LOG_ERROR, "ast_sem_wait(): %s\n",
				strerror(errno));
			/* Just give up */
			break;
		}
		ast_taskprocessor_execute(tps);
	}

	/* No posting to a dead taskprocessor! */
	res = ast_sem_getvalue(&pvt->sem, &sem_value);
	ast_assert(res == 0 && sem_value == 0);

	/* Free the shutdown reference (see default_listener_shutdown) */
	ao2_t_ref(listener->tps, -1, "tps-shutdown");

	return NULL;
}

static int default_listener_start(struct ast_taskprocessor_listener *listener)
{
	struct default_taskprocessor_listener_pvt *pvt = listener->user_data;

	if (ast_pthread_create(&pvt->poll_thread, NULL, default_tps_processing_function, listener)) {
		return -1;
	}

	return 0;
}

static void default_task_pushed(struct ast_taskprocessor_listener *listener, int was_empty)
{
	struct default_taskprocessor_listener_pvt *pvt = listener->user_data;

	if (ast_sem_post(&pvt->sem) != 0) {
		ast_log(LOG_ERROR, "Failed to notify of enqueued task: %s\n",
			strerror(errno));
	}
}

static int default_listener_die(void *data)
{
	struct default_taskprocessor_listener_pvt *pvt = data;
	pvt->dead = 1;
	return 0;
}

static void default_listener_shutdown(struct ast_taskprocessor_listener *listener)
{
	struct default_taskprocessor_listener_pvt *pvt = listener->user_data;
	int res;

	/* Hold a reference during shutdown */
	ao2_t_ref(listener->tps, +1, "tps-shutdown");

	if (ast_taskprocessor_push(listener->tps, default_listener_die, pvt)) {
		/* This will cause the thread to exit early without completing tasks already
		 * in the queue.  This is probably the least bad option in this situation. */
		default_listener_die(pvt);
	}

	ast_assert(pvt->poll_thread != AST_PTHREADT_NULL);

	if (pthread_equal(pthread_self(), pvt->poll_thread)) {
		res = pthread_detach(pvt->poll_thread);
		if (res != 0) {
			ast_log(LOG_ERROR, "pthread_detach(): %s\n", strerror(errno));
		}
	} else {
		res = pthread_join(pvt->poll_thread, NULL);
		if (res != 0) {
			ast_log(LOG_ERROR, "pthread_join(): %s\n", strerror(errno));
		}
	}
	pvt->poll_thread = AST_PTHREADT_NULL;
}

static const struct ast_taskprocessor_listener_callbacks default_listener_callbacks = {
	.start = default_listener_start,
	.task_pushed = default_task_pushed,
	.shutdown = default_listener_shutdown,
	.dtor = default_listener_pvt_dtor,
};

/*!
 * \internal
 * \brief Clean up resources on Asterisk shutdown
 */
static void tps_shutdown(void)
{
	ast_cli_unregister_multiple(taskprocessor_clis, ARRAY_LEN(taskprocessor_clis));
	AST_VECTOR_CALLBACK_VOID(&overloaded_subsystems, ast_free);
	AST_VECTOR_RW_FREE(&overloaded_subsystems);
	ao2_t_ref(tps_singletons, -1, "Unref tps_singletons in shutdown");
	tps_singletons = NULL;
}

/* initialize the taskprocessor container and register CLI operations */
int ast_tps_init(void)
{
	tps_singletons = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0,
		TPS_MAX_BUCKETS, tps_hash_cb, NULL, tps_cmp_cb);
	if (!tps_singletons) {
		ast_log(LOG_ERROR, "taskprocessor container failed to initialize!\n");
		return -1;
	}

	if (AST_VECTOR_RW_INIT(&overloaded_subsystems, 10)) {
		ao2_ref(tps_singletons, -1);
		ast_log(LOG_ERROR, "taskprocessor subsystems vector failed to initialize!\n");
		return -1;
	}

	ast_cond_init(&cli_ping_cond, NULL);

	ast_cli_register_multiple(taskprocessor_clis, ARRAY_LEN(taskprocessor_clis));

	ast_register_cleanup(tps_shutdown);

	return 0;
}

/* allocate resources for the task */
static struct tps_task *tps_task_alloc(int (*task_exe)(void *datap), void *datap)
{
	struct tps_task *t;
	if (!task_exe) {
		ast_log(LOG_ERROR, "task_exe is NULL!\n");
		return NULL;
	}

	t = ast_calloc(1, sizeof(*t));
	if (!t) {
		ast_log(LOG_ERROR, "failed to allocate task!\n");
		return NULL;
	}

	t->callback.execute = task_exe;
	t->datap = datap;

	return t;
}

static struct tps_task *tps_task_alloc_local(int (*task_exe)(struct ast_taskprocessor_local *local), void *datap)
{
	struct tps_task *t;
	if (!task_exe) {
		ast_log(LOG_ERROR, "task_exe is NULL!\n");
		return NULL;
	}

	t = ast_calloc(1, sizeof(*t));
	if (!t) {
		ast_log(LOG_ERROR, "failed to allocate task!\n");
		return NULL;
	}

	t->callback.execute_local = task_exe;
	t->datap = datap;
	t->wants_local = 1;

	return t;
}

/* release task resources */
static void *tps_task_free(struct tps_task *task)
{
	ast_free(task);
	return NULL;
}

/* Taskprocessor tab completion.
 *
 * The caller of this function is responsible for argument
 * position checks prior to calling.
 */
static char *tps_taskprocessor_tab_complete(struct ast_cli_args *a)
{
	int tklen;
	struct ast_taskprocessor *p;
	struct ao2_iterator i;

	tklen = strlen(a->word);
	i = ao2_iterator_init(tps_singletons, 0);
	while ((p = ao2_iterator_next(&i))) {
		if (!strncasecmp(a->word, p->name, tklen)) {
			if (ast_cli_completion_add(ast_strdup(p->name))) {
				ast_taskprocessor_unreference(p);
				break;
			}
		}
		ast_taskprocessor_unreference(p);
	}
	ao2_iterator_destroy(&i);

	return NULL;
}

/* ping task handling function */
static int tps_ping_handler(void *datap)
{
	ast_mutex_lock(&cli_ping_cond_lock);
	ast_cond_signal(&cli_ping_cond);
	ast_mutex_unlock(&cli_ping_cond_lock);
	return 0;
}

/* ping the specified taskprocessor and display the ping time on the CLI */
static char *cli_tps_ping(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct timeval begin, end, delta;
	const char *name;
	struct timeval when;
	struct timespec ts;
	struct ast_taskprocessor *tps;

	switch (cmd) {
	case CLI_INIT:
		e->command = "core ping taskprocessor";
		e->usage =
			"Usage: core ping taskprocessor <taskprocessor>\n"
			"	Displays the time required for a task to be processed\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 3) {
			return tps_taskprocessor_tab_complete(a);
		} else {
			return NULL;
		}
	}

	if (a->argc != 4)
		return CLI_SHOWUSAGE;

	name = a->argv[3];
	if (!(tps = ast_taskprocessor_get(name, TPS_REF_IF_EXISTS))) {
		ast_cli(a->fd, "\nping failed: %s not found\n\n", name);
		return CLI_SUCCESS;
	}
	ast_cli(a->fd, "\npinging %s ...", name);

	/*
	 * Wait up to 5 seconds for a ping reply.
	 *
	 * On a very busy system it could take awhile to get a
	 * ping response from some taskprocessors.
	 */
	begin = ast_tvnow();
	when = ast_tvadd(begin, ast_samp2tv(5000, 1000));
	ts.tv_sec = when.tv_sec;
	ts.tv_nsec = when.tv_usec * 1000;

	ast_mutex_lock(&cli_ping_cond_lock);
	if (ast_taskprocessor_push(tps, tps_ping_handler, 0) < 0) {
		ast_mutex_unlock(&cli_ping_cond_lock);
		ast_cli(a->fd, "\nping failed: could not push task to %s\n\n", name);
		ast_taskprocessor_unreference(tps);
		return CLI_FAILURE;
	}
	ast_cond_timedwait(&cli_ping_cond, &cli_ping_cond_lock, &ts);
	ast_mutex_unlock(&cli_ping_cond_lock);

	end = ast_tvnow();
	delta = ast_tvsub(end, begin);
	ast_cli(a->fd, "\n\t%24s ping time: %.1ld.%.6ld sec\n\n", name, (long)delta.tv_sec, (long int)delta.tv_usec);
	ast_taskprocessor_unreference(tps);
	return CLI_SUCCESS;
}

/*!
 * \internal
 * \brief Taskprocessor ao2 container sort function.
 * \since 13.8.0
 *
 * \param obj_left pointer to the (user-defined part) of an object.
 * \param obj_right pointer to the (user-defined part) of an object.
 * \param flags flags from ao2_callback()
 *   OBJ_SEARCH_OBJECT - if set, 'obj_right', is an object.
 *   OBJ_SEARCH_KEY - if set, 'obj_right', is a search key item that is not an object.
 *   OBJ_SEARCH_PARTIAL_KEY - if set, 'obj_right', is a partial search key item that is not an object.
 *
 * \retval <0 if obj_left < obj_right
 * \retval =0 if obj_left == obj_right
 * \retval >0 if obj_left > obj_right
 */
static int tps_sort_cb(const void *obj_left, const void *obj_right, int flags)
{
	const struct ast_taskprocessor *tps_left = obj_left;
	const struct ast_taskprocessor *tps_right = obj_right;
	const char *right_key = obj_right;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	default:
	case OBJ_SEARCH_OBJECT:
		right_key = tps_right->name;
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcasecmp(tps_left->name, right_key);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		cmp = strncasecmp(tps_left->name, right_key, strlen(right_key));
		break;
	}
	return cmp;
}

#define FMT_HEADERS		"%-70s %10s %10s %10s %10s %10s\n"
#define FMT_FIELDS		"%-70s %10lu %10lu %10lu %10lu %10lu\n"

/*!
 * \internal
 * \brief Print taskprocessor information to CLI.
 * \since 13.30.0
 *
 * \param fd the file descriptor
 * \param tps the taskprocessor
 */
static void tps_report_taskprocessor_list_helper(int fd, struct ast_taskprocessor *tps)
{
	ast_cli(fd, FMT_FIELDS, tps->name, tps->stats._tasks_processed_count,
		tps->tps_queue_size, tps->stats.max_qsize, tps->tps_queue_low,
		tps->tps_queue_high);
}

/*!
 * \internal
 * \brief Prints an optionally narrowed down list of taskprocessors to the CLI.
 * \since 13.30.0
 *
 * \param fd the file descriptor
 * \param like the string we are matching on
 *
 * \retval number of taskprocessors on success
 * \retval 0 otherwise
 */
static int tps_report_taskprocessor_list(int fd, const char *like)
{
	int tps_count = 0;
	int word_len;
	struct ao2_container *sorted_tps;
	struct ast_taskprocessor *tps;
	struct ao2_iterator iter;

	sorted_tps = ao2_container_alloc_rbtree(AO2_ALLOC_OPT_LOCK_NOLOCK, 0, tps_sort_cb,
		NULL);
	if (!sorted_tps
		|| ao2_container_dup(sorted_tps, tps_singletons, 0)) {
		ast_debug(1, "Failed to retrieve sorted taskprocessors\n");
		ao2_cleanup(sorted_tps);
		return 0;
	}

	word_len = strlen(like);
	iter = ao2_iterator_init(sorted_tps, AO2_ITERATOR_UNLINK);
	while ((tps = ao2_iterator_next(&iter))) {
		if (like) {
			if (!strncasecmp(like, tps->name, word_len)) {
				tps_report_taskprocessor_list_helper(fd, tps);
				tps_count++;
			}
		} else {
			tps_report_taskprocessor_list_helper(fd, tps);
			tps_count++;
		}
		ast_taskprocessor_unreference(tps);
	}
	ao2_iterator_destroy(&iter);
	ao2_ref(sorted_tps, -1);

	return tps_count;
}

static char *cli_tps_report(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	const char *like;

	switch (cmd) {
	case CLI_INIT:
		e->command = "core show taskprocessors [like]";
		e->usage =
			"Usage: core show taskprocessors [like keyword]\n"
			"	Shows a list of instantiated task processors and their statistics\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == e->args) {
			return tps_taskprocessor_tab_complete(a);
		} else {
			return NULL;
		}
	}

	if (a->argc == e->args - 1) {
		like = "";
	} else if (a->argc == e->args + 1 && !strcasecmp(a->argv[e->args-1], "like")) {
		like = a->argv[e->args];
	} else {
		return CLI_SHOWUSAGE;
	}

	ast_cli(a->fd, "\n" FMT_HEADERS, "Processor", "Processed", "In Queue", "Max Depth", "Low water", "High water");
	ast_cli(a->fd, "\n%d taskprocessors\n\n", tps_report_taskprocessor_list(a->fd, like));

	return CLI_SUCCESS;
}

/* hash callback for astobj2 */
static int tps_hash_cb(const void *obj, const int flags)
{
	const struct ast_taskprocessor *tps = obj;
	const char *name = flags & OBJ_KEY ? obj : tps->name;

	return ast_str_case_hash(name);
}

/* compare callback for astobj2 */
static int tps_cmp_cb(void *obj, void *arg, int flags)
{
	struct ast_taskprocessor *lhs = obj, *rhs = arg;
	const char *rhsname = flags & OBJ_KEY ? arg : rhs->name;

	return !strcasecmp(lhs->name, rhsname) ? CMP_MATCH | CMP_STOP : 0;
}

static int subsystem_match(struct subsystem_alert *alert, const char *subsystem)
{
	return !strcmp(alert->subsystem, subsystem);
}

static int subsystem_cmp(struct subsystem_alert *a, struct subsystem_alert *b)
{
	return strcmp(a->subsystem, b->subsystem);
}

unsigned int ast_taskprocessor_get_subsystem_alert(const char *subsystem)
{
	struct subsystem_alert *alert;
	unsigned int count = 0;
	int idx;

	AST_VECTOR_RW_RDLOCK(&overloaded_subsystems);
	idx = AST_VECTOR_GET_INDEX(&overloaded_subsystems, subsystem, subsystem_match);
	if (idx >= 0) {
		alert = AST_VECTOR_GET(&overloaded_subsystems, idx);
		count = alert->alert_count;
	}
	AST_VECTOR_RW_UNLOCK(&overloaded_subsystems);

	return count;
}

static void subsystem_alert_increment(const char *subsystem)
{
	struct subsystem_alert *alert;
	int idx;

	if (ast_strlen_zero(subsystem)) {
		return;
	}

	AST_VECTOR_RW_WRLOCK(&overloaded_subsystems);
	idx = AST_VECTOR_GET_INDEX(&overloaded_subsystems, subsystem, subsystem_match);
	if (idx >= 0) {
		alert = AST_VECTOR_GET(&overloaded_subsystems, idx);
		alert->alert_count++;
		AST_VECTOR_RW_UNLOCK(&overloaded_subsystems);
		return;
	}

	alert = ast_malloc(sizeof(*alert) + strlen(subsystem) + 1);
	if (!alert) {
		AST_VECTOR_RW_UNLOCK(&overloaded_subsystems);
		return;
	}
	alert->alert_count = 1;
	strcpy(alert->subsystem, subsystem); /* Safe */

	if (AST_VECTOR_APPEND(&overloaded_subsystems, alert)) {
		ast_free(alert);
	}
	AST_VECTOR_RW_UNLOCK(&overloaded_subsystems);
}

static void subsystem_alert_decrement(const char *subsystem)
{
	struct subsystem_alert *alert;
	int idx;

	if (ast_strlen_zero(subsystem)) {
		return;
	}

	AST_VECTOR_RW_WRLOCK(&overloaded_subsystems);
	idx = AST_VECTOR_GET_INDEX(&overloaded_subsystems, subsystem, subsystem_match);
	if (idx < 0) {
		ast_log(LOG_ERROR,
			"Can't decrement alert count for subsystem '%s' as it wasn't in alert\n", subsystem);
		AST_VECTOR_RW_UNLOCK(&overloaded_subsystems);
		return;
	}
	alert = AST_VECTOR_GET(&overloaded_subsystems, idx);

	alert->alert_count--;
	if (alert->alert_count <= 0) {
		AST_VECTOR_REMOVE(&overloaded_subsystems, idx, 0);
		ast_free(alert);
	}

	AST_VECTOR_RW_UNLOCK(&overloaded_subsystems);
}

static void subsystem_copy(struct subsystem_alert *alert,
	struct subsystem_alert_vector *vector)
{
	struct subsystem_alert *alert_copy;
	alert_copy = ast_malloc(sizeof(*alert_copy) + strlen(alert->subsystem) + 1);
	if (!alert_copy) {
		return;
	}
	alert_copy->alert_count = alert->alert_count;
	strcpy(alert_copy->subsystem, alert->subsystem); /* Safe */
	if (AST_VECTOR_ADD_SORTED(vector, alert_copy, subsystem_cmp)) {
		ast_free(alert_copy);
	}
}

static char *cli_subsystem_alert_report(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct subsystem_alert_vector sorted_subsystems;
	int i;

#define FMT_HEADERS_SUBSYSTEM		"%-32s %12s\n"
#define FMT_FIELDS_SUBSYSTEM		"%-32s %12u\n"

	switch (cmd) {
	case CLI_INIT:
		e->command = "core show taskprocessor alerted subsystems";
		e->usage =
			"Usage: core show taskprocessor alerted subsystems\n"
			"	Shows a list of task processor subsystems that are currently alerted\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != e->args) {
		return CLI_SHOWUSAGE;
	}

	if (AST_VECTOR_INIT(&sorted_subsystems, AST_VECTOR_SIZE(&overloaded_subsystems))) {
		return CLI_FAILURE;
	}

	AST_VECTOR_RW_RDLOCK(&overloaded_subsystems);
	for (i = 0; i < AST_VECTOR_SIZE(&overloaded_subsystems); i++) {
		subsystem_copy(AST_VECTOR_GET(&overloaded_subsystems, i), &sorted_subsystems);
	}
	AST_VECTOR_RW_UNLOCK(&overloaded_subsystems);

	ast_cli(a->fd, "\n" FMT_HEADERS_SUBSYSTEM, "Subsystem", "Alert Count");

	for (i = 0; i < AST_VECTOR_SIZE(&sorted_subsystems); i++) {
		struct subsystem_alert *alert = AST_VECTOR_GET(&sorted_subsystems, i);
		ast_cli(a->fd, FMT_FIELDS_SUBSYSTEM, alert->subsystem, alert->alert_count);
	}

	ast_cli(a->fd, "\n%zu subsystems\n\n", AST_VECTOR_SIZE(&sorted_subsystems));

	AST_VECTOR_CALLBACK_VOID(&sorted_subsystems, ast_free);
	AST_VECTOR_FREE(&sorted_subsystems);

	return CLI_SUCCESS;
}


/*! Count of the number of taskprocessors in high water alert. */
static unsigned int tps_alert_count;

/*! Access protection for tps_alert_count */
AST_RWLOCK_DEFINE_STATIC(tps_alert_lock);

/*!
 * \internal
 * \brief Add a delta to tps_alert_count with protection.
 * \since 13.10.0
 *
 * \param tps Taskprocessor updating queue water mark alert trigger.
 * \param delta The amount to add to tps_alert_count.
 *
 * \return Nothing
 */
static void tps_alert_add(struct ast_taskprocessor *tps, int delta)
{
	unsigned int old;

	ast_rwlock_wrlock(&tps_alert_lock);
	old = tps_alert_count;
	tps_alert_count += delta;
	if (DEBUG_ATLEAST(3)
		/* and tps_alert_count becomes zero or non-zero */
		&& !old != !tps_alert_count) {
		ast_log(LOG_DEBUG, "Taskprocessor '%s' %s the high water alert.\n",
			tps->name, tps_alert_count ? "triggered" : "cleared");
	}

	if (tps->subsystem[0] != '\0') {
		if (delta > 0) {
			subsystem_alert_increment(tps->subsystem);
		} else {
			subsystem_alert_decrement(tps->subsystem);
		}
	}

	ast_rwlock_unlock(&tps_alert_lock);
}

unsigned int ast_taskprocessor_alert_get(void)
{
	unsigned int count;

	ast_rwlock_rdlock(&tps_alert_lock);
	count = tps_alert_count;
	ast_rwlock_unlock(&tps_alert_lock);

	return count;
}

int ast_taskprocessor_alert_set_levels(struct ast_taskprocessor *tps, long low_water, long high_water)
{
	if (!tps || high_water < 0 || high_water < low_water) {
		return -1;
	}

	if (low_water < 0) {
		/* Set low water level to 90% of high water level */
		low_water = (high_water * 9) / 10;
	}

	ao2_lock(tps);

	tps->tps_queue_low = low_water;
	tps->tps_queue_high = high_water;

	if (tps->high_water_alert) {
		if (!tps->tps_queue_size || tps->tps_queue_size < low_water) {
			/* Update water mark alert immediately */
			tps->high_water_alert = 0;
			tps_alert_add(tps, -1);
		}
	} else {
		if (high_water < tps->tps_queue_size) {
			/* Update water mark alert immediately */
			tps->high_water_alert = 1;
			tps_alert_add(tps, +1);
		}
	}

	ao2_unlock(tps);

	return 0;
}

/* destroy the taskprocessor */
static void tps_taskprocessor_dtor(void *tps)
{
	struct ast_taskprocessor *t = tps;
	struct tps_task *task;

	while ((task = AST_LIST_REMOVE_HEAD(&t->tps_queue, list))) {
		tps_task_free(task);
	}
	t->tps_queue_size = 0;

	if (t->high_water_alert) {
		t->high_water_alert = 0;
		tps_alert_add(t, -1);
	}

	ao2_cleanup(t->listener);
	t->listener = NULL;
}

/* pop the front task and return it */
static struct tps_task *tps_taskprocessor_pop(struct ast_taskprocessor *tps)
{
	struct tps_task *task;

	if ((task = AST_LIST_REMOVE_HEAD(&tps->tps_queue, list))) {
		--tps->tps_queue_size;
		if (tps->high_water_alert && tps->tps_queue_size <= tps->tps_queue_low) {
			tps->high_water_alert = 0;
			tps_alert_add(tps, -1);
		}
	}
	return task;
}

long ast_taskprocessor_size(struct ast_taskprocessor *tps)
{
	return (tps) ? tps->tps_queue_size : -1;
}

/* taskprocessor name accessor */
const char *ast_taskprocessor_name(struct ast_taskprocessor *tps)
{
	if (!tps) {
		ast_log(LOG_ERROR, "no taskprocessor specified!\n");
		return NULL;
	}
	return tps->name;
}

static void listener_shutdown(struct ast_taskprocessor_listener *listener)
{
	listener->callbacks->shutdown(listener);
	ao2_ref(listener->tps, -1);
}

static void taskprocessor_listener_dtor(void *obj)
{
	struct ast_taskprocessor_listener *listener = obj;

	if (listener->callbacks->dtor) {
		listener->callbacks->dtor(listener);
	}
}

struct ast_taskprocessor_listener *ast_taskprocessor_listener_alloc(const struct ast_taskprocessor_listener_callbacks *callbacks, void *user_data)
{
	struct ast_taskprocessor_listener *listener;

	listener = ao2_alloc(sizeof(*listener), taskprocessor_listener_dtor);
	if (!listener) {
		return NULL;
	}
	listener->callbacks = callbacks;
	listener->user_data = user_data;

	return listener;
}

struct ast_taskprocessor *ast_taskprocessor_listener_get_tps(const struct ast_taskprocessor_listener *listener)
{
	ao2_ref(listener->tps, +1);
	return listener->tps;
}

void *ast_taskprocessor_listener_get_user_data(const struct ast_taskprocessor_listener *listener)
{
	return listener->user_data;
}

static void *default_listener_pvt_alloc(void)
{
	struct default_taskprocessor_listener_pvt *pvt;

	pvt = ast_calloc(1, sizeof(*pvt));
	if (!pvt) {
		return NULL;
	}
	pvt->poll_thread = AST_PTHREADT_NULL;
	if (ast_sem_init(&pvt->sem, 0, 0) != 0) {
		ast_log(LOG_ERROR, "ast_sem_init(): %s\n", strerror(errno));
		ast_free(pvt);
		return NULL;
	}
	return pvt;
}

/*!
 * \internal
 * \brief Allocate a task processor structure
 *
 * \param name Name of the task processor.
 * \param listener Listener to associate with the task processor.
 *
 * \return The newly allocated task processor.
 *
 * \pre tps_singletons must be locked by the caller.
 */
static struct ast_taskprocessor *__allocate_taskprocessor(const char *name, struct ast_taskprocessor_listener *listener)
{
	struct ast_taskprocessor *p;
	char *subsystem_separator;
	size_t subsystem_length = 0;
	size_t name_length;

	name_length = strlen(name);
	subsystem_separator = strchr(name, '/');
	if (subsystem_separator) {
		subsystem_length = subsystem_separator - name;
	}

	p = ao2_alloc(sizeof(*p) + name_length + subsystem_length + 2, tps_taskprocessor_dtor);
	if (!p) {
		ast_log(LOG_WARNING, "failed to create taskprocessor '%s'\n", name);
		return NULL;
	}

	/* Set default congestion water level alert triggers. */
	p->tps_queue_low = (AST_TASKPROCESSOR_HIGH_WATER_LEVEL * 9) / 10;
	p->tps_queue_high = AST_TASKPROCESSOR_HIGH_WATER_LEVEL;

	strcpy(p->name, name); /* Safe */
	p->subsystem = p->name + name_length + 1;
	ast_copy_string(p->subsystem, name, subsystem_length + 1);

	ao2_ref(listener, +1);
	p->listener = listener;

	p->thread = AST_PTHREADT_NULL;

	ao2_ref(p, +1);
	listener->tps = p;

	if (!(ao2_link_flags(tps_singletons, p, OBJ_NOLOCK))) {
		ast_log(LOG_ERROR, "Failed to add taskprocessor '%s' to container\n", p->name);
		listener->tps = NULL;
		ao2_ref(p, -2);
		return NULL;
	}

	return p;
}

static struct ast_taskprocessor *__start_taskprocessor(struct ast_taskprocessor *p)
{
	if (p && p->listener->callbacks->start(p->listener)) {
		ast_log(LOG_ERROR, "Unable to start taskprocessor listener for taskprocessor %s\n",
			p->name);
		ast_taskprocessor_unreference(p);

		return NULL;
	}

	return p;
}

/* Provide a reference to a taskprocessor.  Create the taskprocessor if necessary, but don't
 * create the taskprocessor if we were told via ast_tps_options to return a reference only
 * if it already exists */
struct ast_taskprocessor *ast_taskprocessor_get(const char *name, enum ast_tps_options create)
{
	struct ast_taskprocessor *p;
	struct ast_taskprocessor_listener *listener;
	struct default_taskprocessor_listener_pvt *pvt;

	if (ast_strlen_zero(name)) {
		ast_log(LOG_ERROR, "requesting a nameless taskprocessor!!!\n");
		return NULL;
	}
	ao2_lock(tps_singletons);
	p = ao2_find(tps_singletons, name, OBJ_KEY | OBJ_NOLOCK);
	if (p || (create & TPS_REF_IF_EXISTS)) {
		/* calling function does not want a new taskprocessor to be created if it doesn't already exist */
		ao2_unlock(tps_singletons);
		return p;
	}

	/* Create a new taskprocessor. Start by creating a default listener */
	pvt = default_listener_pvt_alloc();
	if (!pvt) {
		ao2_unlock(tps_singletons);
		return NULL;
	}
	listener = ast_taskprocessor_listener_alloc(&default_listener_callbacks, pvt);
	if (!listener) {
		ao2_unlock(tps_singletons);
		default_listener_pvt_destroy(pvt);
		return NULL;
	}

	p = __allocate_taskprocessor(name, listener);
	ao2_unlock(tps_singletons);
	p = __start_taskprocessor(p);
	ao2_ref(listener, -1);

	return p;
}

struct ast_taskprocessor *ast_taskprocessor_create_with_listener(const char *name, struct ast_taskprocessor_listener *listener)
{
	struct ast_taskprocessor *p;

	ao2_lock(tps_singletons);
	p = ao2_find(tps_singletons, name, OBJ_KEY | OBJ_NOLOCK);
	if (p) {
		ao2_unlock(tps_singletons);
		ast_taskprocessor_unreference(p);
		return NULL;
	}

	p = __allocate_taskprocessor(name, listener);
	ao2_unlock(tps_singletons);

	return __start_taskprocessor(p);
}

void ast_taskprocessor_set_local(struct ast_taskprocessor *tps,
	void *local_data)
{
	SCOPED_AO2LOCK(lock, tps);
	tps->local_data = local_data;
}

/* decrement the taskprocessor reference count and unlink from the container if necessary */
void *ast_taskprocessor_unreference(struct ast_taskprocessor *tps)
{
	if (!tps) {
		return NULL;
	}

	/* To prevent another thread from finding and getting a reference to this
	 * taskprocessor we hold the singletons lock. If we didn't do this then
	 * they may acquire it and find that the listener has been shut down.
	 */
	ao2_lock(tps_singletons);

	if (ao2_ref(tps, -1) > 3) {
		ao2_unlock(tps_singletons);
		return NULL;
	}

	/* If we're down to 3 references, then those must be:
	 * 1. The reference we just got rid of
	 * 2. The container
	 * 3. The listener
	 */
	ao2_unlink_flags(tps_singletons, tps, OBJ_NOLOCK);
	ao2_unlock(tps_singletons);

	listener_shutdown(tps->listener);
	return NULL;
}

/* push the task into the taskprocessor queue */
static int taskprocessor_push(struct ast_taskprocessor *tps, struct tps_task *t)
{
	int previous_size;
	int was_empty;

	if (!tps) {
		ast_log(LOG_ERROR, "tps is NULL!\n");
		return -1;
	}

	if (!t) {
		ast_log(LOG_ERROR, "t is NULL!\n");
		return -1;
	}

	ao2_lock(tps);
	AST_LIST_INSERT_TAIL(&tps->tps_queue, t, list);
	previous_size = tps->tps_queue_size++;

	if (tps->tps_queue_high <= tps->tps_queue_size) {
		if (!tps->high_water_alert) {
			ast_log(LOG_WARNING, "The '%s' task processor queue reached %ld scheduled tasks%s.\n",
				tps->name, tps->tps_queue_size, tps->high_water_warned ? " again" : "");
			tps->high_water_warned = 1;
			tps->high_water_alert = 1;
			tps_alert_add(tps, +1);
		}
	}

	/* The currently executing task counts as still in queue */
	was_empty = tps->executing ? 0 : previous_size == 0;
	ao2_unlock(tps);
	tps->listener->callbacks->task_pushed(tps->listener, was_empty);
	return 0;
}

int ast_taskprocessor_push(struct ast_taskprocessor *tps, int (*task_exe)(void *datap), void *datap)
{
	return taskprocessor_push(tps, tps_task_alloc(task_exe, datap));
}

int ast_taskprocessor_push_local(struct ast_taskprocessor *tps, int (*task_exe)(struct ast_taskprocessor_local *datap), void *datap)
{
	return taskprocessor_push(tps, tps_task_alloc_local(task_exe, datap));
}

int ast_taskprocessor_suspend(struct ast_taskprocessor *tps)
{
	if (tps) {
		ao2_lock(tps);
		tps->suspended = 1;
		ao2_unlock(tps);
		return 0;
	}
	return -1;
}

int ast_taskprocessor_unsuspend(struct ast_taskprocessor *tps)
{
	if (tps) {
		ao2_lock(tps);
		tps->suspended = 0;
		ao2_unlock(tps);
		return 0;
	}
	return -1;
}

int ast_taskprocessor_is_suspended(struct ast_taskprocessor *tps)
{
	return tps ? tps->suspended : -1;
}

int ast_taskprocessor_execute(struct ast_taskprocessor *tps)
{
	struct ast_taskprocessor_local local;
	struct tps_task *t;
	long size;

	ao2_lock(tps);
	t = tps_taskprocessor_pop(tps);
	if (!t) {
		ao2_unlock(tps);
		return 0;
	}

	tps->thread = pthread_self();
	tps->executing = 1;

	if (t->wants_local) {
		local.local_data = tps->local_data;
		local.data = t->datap;
	}
	ao2_unlock(tps);

	if (t->wants_local) {
		t->callback.execute_local(&local);
	} else {
		t->callback.execute(t->datap);
	}
	tps_task_free(t);

	ao2_lock(tps);
	tps->thread = AST_PTHREADT_NULL;
	/* We need to check size in the same critical section where we reset the
	 * executing bit. Avoids a race condition where a task is pushed right
	 * after we pop an empty stack.
	 */
	tps->executing = 0;
	size = ast_taskprocessor_size(tps);

	/* Update the stats */
	++tps->stats._tasks_processed_count;

	/* Include the task we just executed as part of the queue size. */
	if (size >= tps->stats.max_qsize) {
		tps->stats.max_qsize = size + 1;
	}
	ao2_unlock(tps);

	/* If we executed a task, check for the transition to empty */
	if (size == 0 && tps->listener->callbacks->emptied) {
		tps->listener->callbacks->emptied(tps->listener);
	}
	return size > 0;
}

int ast_taskprocessor_is_task(struct ast_taskprocessor *tps)
{
	int is_task;

	ao2_lock(tps);
	is_task = pthread_equal(tps->thread, pthread_self());
	ao2_unlock(tps);
	return is_task;
}

unsigned int ast_taskprocessor_seq_num(void)
{
	static int seq_num;

	return (unsigned int) ast_atomic_fetchadd_int(&seq_num, +1);
}

#define SEQ_STR_SIZE (1 + 8 + 1)	/* Dash plus 8 hex digits plus null terminator */

void ast_taskprocessor_name_append(char *buf, unsigned int size, const char *name)
{
	int final_size = strlen(name) + SEQ_STR_SIZE;

	ast_assert(buf != NULL && name != NULL);
	ast_assert(final_size <= size);

	snprintf(buf, final_size, "%s-%08x", name, ast_taskprocessor_seq_num());
}

void ast_taskprocessor_build_name(char *buf, unsigned int size, const char *format, ...)
{
	va_list ap;
	int user_size;

	ast_assert(buf != NULL);
	ast_assert(SEQ_STR_SIZE <= size);

	va_start(ap, format);
	user_size = vsnprintf(buf, size - (SEQ_STR_SIZE - 1), format, ap);
	va_end(ap);
	if (user_size < 0) {
		/*
		 * Wow!  We got an output error to a memory buffer.
		 * Assume no user part of name written.
		 */
		user_size = 0;
	} else if (size < user_size + SEQ_STR_SIZE) {
		/* Truncate user part of name to make sequence number fit. */
		user_size = size - SEQ_STR_SIZE;
	}

	/* Append sequence number to end of user name. */
	snprintf(buf + user_size, SEQ_STR_SIZE, "-%08x", ast_taskprocessor_seq_num());
}

static void tps_reset_stats(struct ast_taskprocessor *tps)
{
	ao2_lock(tps);
	tps->stats._tasks_processed_count = 0;
	tps->stats.max_qsize = 0;
	ao2_unlock(tps);
}

static char *cli_tps_reset_stats(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	const char *name;
	struct ast_taskprocessor *tps;

	switch (cmd) {
	case CLI_INIT:
		e->command = "core reset taskprocessor";
		e->usage =
			"Usage: core reset taskprocessor <taskprocessor>\n"
			"    Resets stats for the specified taskprocessor\n";
		return NULL;
	case CLI_GENERATE:
		return tps_taskprocessor_tab_complete(a);
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	name = a->argv[3];
	if (!(tps = ast_taskprocessor_get(name, TPS_REF_IF_EXISTS))) {
		ast_cli(a->fd, "\nReset failed: %s not found\n\n", name);
		return CLI_SUCCESS;
	}
	ast_cli(a->fd, "\nResetting %s\n\n", name);

	tps_reset_stats(tps);

	ast_taskprocessor_unreference(tps);

	return CLI_SUCCESS;
}

static char *cli_tps_reset_stats_all(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_taskprocessor *tps;
	struct ao2_iterator iter;

	switch (cmd) {
	case CLI_INIT:
		e->command = "core reset taskprocessors";
		e->usage =
			"Usage: core reset taskprocessors\n"
			"    Resets stats for all taskprocessors\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != e->args) {
		return CLI_SHOWUSAGE;
	}

	ast_cli(a->fd, "\nResetting stats for all taskprocessors\n\n");

	iter = ao2_iterator_init(tps_singletons, 0);
	while ((tps = ao2_iterator_next(&iter))) {
		tps_reset_stats(tps);
		ast_taskprocessor_unreference(tps);
	}
	ao2_iterator_destroy(&iter);

	return CLI_SUCCESS;
}
