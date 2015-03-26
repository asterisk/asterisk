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

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

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
	/*! \brief Friendly name of the taskprocessor */
	const char *name;
	/*! \brief Taskprocessor statistics */
	struct tps_taskprocessor_stats *stats;
	void *local_data;
	/*! \brief Taskprocessor current queue size */
	long tps_queue_size;
	/*! \brief Taskprocessor queue */
	AST_LIST_HEAD_NOLOCK(tps_queue, tps_task) tps_queue;
	struct ast_taskprocessor_listener *listener;
	/*! Current thread executing the tasks */
	pthread_t thread;
	/*! Indicates if the taskprocessor is currently executing a task */
	unsigned int executing:1;
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

#define TPS_MAX_BUCKETS 7
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

/*! \brief Destroy the taskprocessor when its refcount reaches zero */
static void tps_taskprocessor_destroy(void *tps);

/*! \brief CLI <example>taskprocessor ping &lt;blah&gt;</example> handler function */
static int tps_ping_handler(void *datap);

/*! \brief Remove the front task off the taskprocessor queue */
static struct tps_task *tps_taskprocessor_pop(struct ast_taskprocessor *tps);

/*! \brief Return the size of the taskprocessor queue */
static int tps_taskprocessor_depth(struct ast_taskprocessor *tps);

static char *cli_tps_ping(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *cli_tps_report(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);

static struct ast_cli_entry taskprocessor_clis[] = {
	AST_CLI_DEFINE(cli_tps_ping, "Ping a named task processor"),
	AST_CLI_DEFINE(cli_tps_report, "List instantiated task processors and statistics"),
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

	ast_taskprocessor_push(listener->tps, default_listener_die, pvt);

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
	ao2_t_ref(tps_singletons, -1, "Unref tps_singletons in shutdown");
	tps_singletons = NULL;
}

/* initialize the taskprocessor container and register CLI operations */
int ast_tps_init(void)
{
	if (!(tps_singletons = ao2_container_alloc(TPS_MAX_BUCKETS, tps_hash_cb, tps_cmp_cb))) {
		ast_log(LOG_ERROR, "taskprocessor container failed to initialize!\n");
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

/* taskprocessor tab completion */
static char *tps_taskprocessor_tab_complete(struct ast_taskprocessor *p, struct ast_cli_args *a)
{
	int tklen;
	int wordnum = 0;
	char *name = NULL;
	struct ao2_iterator i;

	if (a->pos != 3)
		return NULL;

	tklen = strlen(a->word);
	i = ao2_iterator_init(tps_singletons, 0);
	while ((p = ao2_iterator_next(&i))) {
		if (!strncasecmp(a->word, p->name, tklen) && ++wordnum > a->n) {
			name = ast_strdup(p->name);
			ao2_ref(p, -1);
			break;
		}
		ao2_ref(p, -1);
	}
	ao2_iterator_destroy(&i);
	return name;
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
	struct ast_taskprocessor *tps = NULL;

	switch (cmd) {
	case CLI_INIT:
		e->command = "core ping taskprocessor";
		e->usage =
			"Usage: core ping taskprocessor <taskprocessor>\n"
			"	Displays the time required for a task to be processed\n";
		return NULL;
	case CLI_GENERATE:
		return tps_taskprocessor_tab_complete(tps, a);
	}

	if (a->argc != 4)
		return CLI_SHOWUSAGE;

	name = a->argv[3];
	if (!(tps = ast_taskprocessor_get(name, TPS_REF_IF_EXISTS))) {
		ast_cli(a->fd, "\nping failed: %s not found\n\n", name);
		return CLI_SUCCESS;
	}
	ast_cli(a->fd, "\npinging %s ...", name);
	when = ast_tvadd((begin = ast_tvnow()), ast_samp2tv(1000, 1000));
	ts.tv_sec = when.tv_sec;
	ts.tv_nsec = when.tv_usec * 1000;
	ast_mutex_lock(&cli_ping_cond_lock);
	if (ast_taskprocessor_push(tps, tps_ping_handler, 0) < 0) {
		ast_cli(a->fd, "\nping failed: could not push task to %s\n\n", name);
		ao2_ref(tps, -1);
		return CLI_FAILURE;
	}
	ast_cond_timedwait(&cli_ping_cond, &cli_ping_cond_lock, &ts);
	ast_mutex_unlock(&cli_ping_cond_lock);
	end = ast_tvnow();
	delta = ast_tvsub(end, begin);
	ast_cli(a->fd, "\n\t%24s ping time: %.1ld.%.6ld sec\n\n", name, (long)delta.tv_sec, (long int)delta.tv_usec);
	ao2_ref(tps, -1);
	return CLI_SUCCESS;
}

static char *cli_tps_report(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char name[256];
	int tcount;
	unsigned long qsize;
	unsigned long maxqsize;
	unsigned long processed;
	struct ast_taskprocessor *p;
	struct ao2_iterator i;

	switch (cmd) {
	case CLI_INIT:
		e->command = "core show taskprocessors";
		e->usage =
			"Usage: core show taskprocessors\n"
			"	Shows a list of instantiated task processors and their statistics\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != e->args)
		return CLI_SHOWUSAGE;

	ast_cli(a->fd, "\n\t+----- Processor -----+--- Processed ---+- In Queue -+- Max Depth -+");
	i = ao2_iterator_init(tps_singletons, 0);
	while ((p = ao2_iterator_next(&i))) {
		ast_copy_string(name, p->name, sizeof(name));
		qsize = p->tps_queue_size;
		maxqsize = p->stats->max_qsize;
		processed = p->stats->_tasks_processed_count;
		ast_cli(a->fd, "\n%24s   %17lu %12lu %12lu", name, processed, qsize, maxqsize);
		ao2_ref(p, -1);
	}
	ao2_iterator_destroy(&i);
	tcount = ao2_container_count(tps_singletons);
	ast_cli(a->fd, "\n\t+---------------------+-----------------+------------+-------------+\n\t%d taskprocessors\n\n", tcount);
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

/* destroy the taskprocessor */
static void tps_taskprocessor_destroy(void *tps)
{
	struct ast_taskprocessor *t = tps;
	struct tps_task *task;

	if (!tps) {
		ast_log(LOG_ERROR, "missing taskprocessor\n");
		return;
	}
	ast_debug(1, "destroying taskprocessor '%s'\n", t->name);
	/* free it */
	ast_free(t->stats);
	t->stats = NULL;
	ast_free((char *) t->name);
	if (t->listener) {
		ao2_ref(t->listener, -1);
		t->listener = NULL;
	}
	while ((task = AST_LIST_REMOVE_HEAD(&t->tps_queue, list))) {
		tps_task_free(task);
	}
}

/* pop the front task and return it */
static struct tps_task *tps_taskprocessor_pop(struct ast_taskprocessor *tps)
{
	struct tps_task *task;

	if ((task = AST_LIST_REMOVE_HEAD(&tps->tps_queue, list))) {
		tps->tps_queue_size--;
	}
	return task;
}

static int tps_taskprocessor_depth(struct ast_taskprocessor *tps)
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

static struct ast_taskprocessor *__allocate_taskprocessor(const char *name, struct ast_taskprocessor_listener *listener)
{
	RAII_VAR(struct ast_taskprocessor *, p,
			ao2_alloc(sizeof(*p), tps_taskprocessor_destroy), ao2_cleanup);

	if (!p) {
		ast_log(LOG_WARNING, "failed to create taskprocessor '%s'\n", name);
		return NULL;
	}

	if (!(p->stats = ast_calloc(1, sizeof(*p->stats)))) {
		ast_log(LOG_WARNING, "failed to create taskprocessor stats for '%s'\n", name);
		return NULL;
	}
	if (!(p->name = ast_strdup(name))) {
		ao2_ref(p, -1);
		return NULL;
	}

	ao2_ref(listener, +1);
	p->listener = listener;

	p->thread = AST_PTHREADT_NULL;

	ao2_ref(p, +1);
	listener->tps = p;

	if (!(ao2_link(tps_singletons, p))) {
		ast_log(LOG_ERROR, "Failed to add taskprocessor '%s' to container\n", p->name);
		return NULL;
	}

	if (p->listener->callbacks->start(p->listener)) {
		ast_log(LOG_ERROR, "Unable to start taskprocessor listener for taskprocessor %s\n", p->name);
		ast_taskprocessor_unreference(p);
		return NULL;
	}

	/* RAII_VAR will decrement the refcount at the end of the function.
	 * Since we want to pass back a reference to p, we bump the refcount
	 */
	ao2_ref(p, +1);
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
	p = ao2_find(tps_singletons, name, OBJ_KEY);
	if (p) {
		return p;
	}
	if (create & TPS_REF_IF_EXISTS) {
		/* calling function does not want a new taskprocessor to be created if it doesn't already exist */
		return NULL;
	}
	/* Create a new taskprocessor. Start by creating a default listener */
	pvt = default_listener_pvt_alloc();
	if (!pvt) {
		return NULL;
	}
	listener = ast_taskprocessor_listener_alloc(&default_listener_callbacks, pvt);
	if (!listener) {
		default_listener_pvt_destroy(pvt);
		return NULL;
	}

	p = __allocate_taskprocessor(name, listener);
	if (!p) {
		ao2_ref(listener, -1);
		return NULL;
	}

	/* Unref listener here since the taskprocessor has gained a reference to the listener */
	ao2_ref(listener, -1);
	return p;
}

struct ast_taskprocessor *ast_taskprocessor_create_with_listener(const char *name, struct ast_taskprocessor_listener *listener)
{
	struct ast_taskprocessor *p = ao2_find(tps_singletons, name, OBJ_KEY);

	if (p) {
		ast_taskprocessor_unreference(p);
		return NULL;
	}
	return __allocate_taskprocessor(name, listener);
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

	if (ao2_ref(tps, -1) > 3) {
		return NULL;
	}
	/* If we're down to 3 references, then those must be:
	 * 1. The reference we just got rid of
	 * 2. The container
	 * 3. The listener
	 */
	ao2_unlink(tps_singletons, tps);
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

int ast_taskprocessor_execute(struct ast_taskprocessor *tps)
{
	struct ast_taskprocessor_local local;
	struct tps_task *t;
	int size;

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
	size = tps_taskprocessor_depth(tps);
	/* If we executed a task, bump the stats */
	if (tps->stats) {
		tps->stats->_tasks_processed_count++;
		if (size > tps->stats->max_qsize) {
			tps->stats->max_qsize = size;
		}
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
