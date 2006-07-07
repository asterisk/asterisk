/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * KDE Console monitor -- Mostly glue code
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/logger.h"
#include "asterisk/lock.h"
#include "pbx_kdeconsole.h"

static char *dtext = "KDE Console Monitor";

static int inuse = 0;

static KAsteriskConsole *w;

static void verboser(char *stuff, int opos, int replacelast, int complete)
{
	const char *s2[2];
	s2[0] = stuff;
	s2[1] = NULL;
	if (replacelast)  {
		printf("Removing %d\n", w->verbose->count());
		w->verbose->removeItem(w->verbose->count());
	}
	w->verbose->insertStrList(s2, 1, -1);
	w->verbose->setBottomItem(w->verbose->count());
}

static int kde_main(int argc, char *argv[])
{
	KApplication a ( argc, argv );
	w = new KAsteriskConsole();
	a.setMainWidget(w);
	w->show();
	ast_register_verbose(verboser);
	return a.exec();
}

static void *kdemain(void *data)
{
	/* It would appear kde really wants to be main */;
	char *argv[1] = { "asteriskconsole" };
	kde_main(1, argv);
	return NULL;
}

extern "C" {

int unload_module(void)
{
	return inuse;
}

int load_module(void)
{
	pthread_t t;
	pthread_create(&t, NULL, kdemain, NULL);
	return 0;
}

int usecount(void)
{
	return inuse;
}

char *description(void)
{
	return dtext;
}

}
