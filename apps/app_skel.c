/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Skeleton application
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include <pthread.h>

static pthread_mutex_t skellock = PTHREAD_MUTEX_INITIALIZER;

static int usecnt=0;

static char *tdesc = "Trivial skeleton Application";

static char *app = "skel";

struct skeluser {
	struct ast_channel *chan;
	struct skeluser *next;
} *users = NULL;

static int skel_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	struct skeluser *u, *ul=NULL;
	if (!data) {
		ast_log(LOG_WARNING, "skel requires an argument (filename)\n");
		return -1;
	}
	if (!(u=malloc(sizeof(struct skeluser)))) {
		ast_log(LOG_WARNING, "Out of memory\n");
		return -1;
	}
	pthread_mutex_lock(&skellock);
	u->chan = chan;
	u->next = users;
	users = u;
	usecnt++;
	pthread_mutex_unlock(&skellock);
	/* Do our thing here */
	pthread_mutex_lock(&skellock);
	u = users;
	while(u) {
		if (ul)
			ul->next = u->next;
		else
			users = u->next;
		u = u->next;
	}
	usecnt--;
	pthread_mutex_unlock(&skellock);
	return res;
}

int unload_module(void)
{
	struct skeluser *u;
	pthread_mutex_lock(&skellock);
	u = users;
	while(u) {
		/* Hang up anybody who is using us */
		ast_softhangup(u->chan);
		u = u->next;
	}
	pthread_mutex_unlock(&skellock);
	return ast_unregister_application(app);
}

int load_module(void)
{
	return ast_register_application(app, skel_exec);
}

char *description(void)
{
	return tdesc;
}

int usecount(void)
{
	int res;
	pthread_mutex_lock(&skellock);
	res = usecnt;
	pthread_mutex_unlock(&skellock);
	return res;
}
