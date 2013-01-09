/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Resource limits
 * 
 * Copyright (c) 2006 Tilghman Lesher.  All rights reserved.
 *
 * Tilghman Lesher <res_limit_200607@the-tilghman.com>
 *
 * This code is released by the author with no restrictions on usage.
 *
 */

/*! \file
 *
 * \brief Resource limits
 *
 * \author Tilghman Lesher <res_limit_200607@the-tilghman.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <ctype.h>
#include <sys/time.h>
#include <sys/resource.h>
#include "asterisk/module.h"
#include "asterisk/cli.h"

/* Find proper rlimit for virtual memory */
#ifdef RLIMIT_AS
#define VMEM_DEF RLIMIT_AS
#else
#ifdef RLIMIT_VMEM
#define VMEM_DEF RLIMIT_VMEM
#endif
#endif

static const struct limits {
	int resource;
	char limit[3];
	char desc[40];
	char clicmd[15];
} limits[] = {
	{ RLIMIT_CPU,     "-t", "cpu time", "time" },
	{ RLIMIT_FSIZE,   "-f", "file size" , "file" },
	{ RLIMIT_DATA,    "-d", "program data segment", "data" },
	{ RLIMIT_STACK,   "-s", "program stack size", "stack" },
	{ RLIMIT_CORE,    "-c", "core file size", "core" },
#ifdef RLIMIT_RSS
	{ RLIMIT_RSS,     "-m", "resident memory", "memory" },
	{ RLIMIT_MEMLOCK, "-l", "amount of memory locked into RAM", "locked" },
#endif
#ifdef RLIMIT_NPROC
	{ RLIMIT_NPROC,   "-u", "number of processes", "processes" },
#endif
	{ RLIMIT_NOFILE,  "-n", "number of file descriptors", "descriptors" },
#ifdef VMEM_DEF
	{ VMEM_DEF,       "-v", "virtual memory", "virtual" },
#endif
};

static int str2limit(const char *string)
{
	size_t i;
	for (i = 0; i < ARRAY_LEN(limits); i++) {
		if (!strcasecmp(string, limits[i].clicmd))
			return limits[i].resource;
	}
	return -1;
}

static const char *str2desc(const char *string)
{
	size_t i;
	for (i = 0; i < ARRAY_LEN(limits); i++) {
		if (!strcmp(string, limits[i].clicmd))
			return limits[i].desc;
	}
	return "<unknown>";
}

static char *complete_ulimit(struct ast_cli_args *a)
{
	int which = 0, i;
	int wordlen = strlen(a->word);

	if (a->pos > 1)
		return NULL;
	for (i = 0; i < ARRAY_LEN(limits); i++) {
		if (!strncasecmp(limits[i].clicmd, a->word, wordlen)) {
			if (++which > a->n)
				return ast_strdup(limits[i].clicmd);
		}
	}
	return NULL;
}

static char *handle_cli_ulimit(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int resource;
	struct rlimit rlimit = { 0, 0 };

	switch (cmd) {
	case CLI_INIT:
		e->command = "ulimit";
		e->usage =
			"Usage: ulimit {data|"
#ifdef RLIMIT_RSS
			"limit|"
#endif
			"file|"
#ifdef RLIMIT_RSS
			"memory|"
#endif
			"stack|time|"
#ifdef RLIMIT_NPROC
			"processes|"
#endif
#ifdef VMEM_DEF
			"virtual|"
#endif
			"core|descriptors} [<num>]\n"
			"       Shows or sets the corresponding resource limit.\n"
			"         data          Process data segment [readonly]\n"
#ifdef RLIMIT_RSS
			"         lock          Memory lock size [readonly]\n"
#endif
			"         file          File size\n"
#ifdef RLIMIT_RSS
			"         memory        Process resident memory [readonly]\n"
#endif
			"         stack         Process stack size [readonly]\n"
			"         time          CPU usage [readonly]\n"
#ifdef RLIMIT_NPROC
			"         processes     Child processes\n"
#endif
#ifdef VMEM_DEF
			"         virtual       Process virtual memory [readonly]\n"
#endif
			"         core          Core dump file size\n"
			"         descriptors   Number of file descriptors\n";
		return NULL;
	case CLI_GENERATE:
		return complete_ulimit(a);
	}

	if (a->argc > 3)
		return CLI_SHOWUSAGE;

	if (a->argc == 1) {
		char arg2[15];
		const char * const newargv[2] = { "ulimit", arg2 };
		for (resource = 0; resource < ARRAY_LEN(limits); resource++) {
			struct ast_cli_args newArgs = { .argv = newargv, .argc = 2 };
			ast_copy_string(arg2, limits[resource].clicmd, sizeof(arg2));
			handle_cli_ulimit(e, CLI_HANDLER, &newArgs);
		}
		return CLI_SUCCESS;
	} else {
		resource = str2limit(a->argv[1]);
		if (resource == -1) {
			ast_cli(a->fd, "Unknown resource\n");
			return CLI_FAILURE;
		}

		if (a->argc == 3) {
			int x;
#ifdef RLIMIT_NPROC
			if (resource != RLIMIT_NOFILE && resource != RLIMIT_CORE && resource != RLIMIT_NPROC && resource != RLIMIT_FSIZE) {
#else
			if (resource != RLIMIT_NOFILE && resource != RLIMIT_CORE && resource != RLIMIT_FSIZE) {
#endif
				ast_cli(a->fd, "Resource not permitted to be set\n");
				return CLI_FAILURE;
			}

			sscanf(a->argv[2], "%30d", &x);
			rlimit.rlim_max = rlimit.rlim_cur = x;
			setrlimit(resource, &rlimit);
			return CLI_SUCCESS;
		} else {
			if (!getrlimit(resource, &rlimit)) {
				char printlimit[32];
				const char *desc;
				if (rlimit.rlim_max == RLIM_INFINITY)
					ast_copy_string(printlimit, "effectively unlimited", sizeof(printlimit));
				else
					snprintf(printlimit, sizeof(printlimit), "limited to %d", (int) rlimit.rlim_cur);
				desc = str2desc(a->argv[1]);
				ast_cli(a->fd, "%c%s (%s) is %s.\n", toupper(desc[0]), desc + 1, a->argv[1], printlimit);
			} else
				ast_cli(a->fd, "Could not retrieve resource limits for %s: %s\n", str2desc(a->argv[1]), strerror(errno));
			return CLI_SUCCESS;
		}
	}
}

static struct ast_cli_entry cli_ulimit =
	AST_CLI_DEFINE(handle_cli_ulimit, "Set or show process resource limits");

static int unload_module(void)
{
	return ast_cli_unregister(&cli_ulimit);
}

static int load_module(void)
{
	return ast_cli_register(&cli_ulimit) ? AST_MODULE_LOAD_FAILURE : AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Resource limits");

