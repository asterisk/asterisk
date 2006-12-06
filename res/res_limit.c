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

#include "asterisk.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#define _XOPEN_SOURCE 600
#include <string.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <errno.h>
#include "asterisk/module.h"
#include "asterisk/cli.h"

static struct limits {
	int resource;
	char limit[3];
	char desc[40];
} limits[] = {
	{ RLIMIT_CPU, "-t", "cpu time" },
	{ RLIMIT_FSIZE, "-f", "file size" },
	{ RLIMIT_DATA, "-d", "program data segment" },
	{ RLIMIT_STACK, "-s", "program stack size" },
	{ RLIMIT_CORE, "-c", "core file size" },
#ifdef RLIMIT_RSS
	{ RLIMIT_RSS, "-m", "resident memory" },
	{ RLIMIT_NPROC, "-u", "number of processes" },
	{ RLIMIT_MEMLOCK, "-l", "amount of memory locked into RAM" },
#endif
	{ RLIMIT_NOFILE, "-n", "number of file descriptors" },
#ifndef RLIMIT_AS	/* *BSD use RLIMIT_VMEM */
#define	RLIMIT_AS	RLIMIT_VMEM
#endif
	{ RLIMIT_AS, "-v", "virtual memory" },
};

static int str2limit(const char *string)
{
	size_t i;
	for (i = 0; i < sizeof(limits) / sizeof(limits[0]); i++) {
		if (!strcasecmp(string, limits[i].limit))
			return limits[i].resource;
	}
	return -1;
}

static const char *str2desc(const char *string)
{
	size_t i;
	for (i = 0; i < sizeof(limits) / sizeof(limits[0]); i++) {
		if (!strcmp(string, limits[i].limit))
			return limits[i].desc;
	}
	return "<unknown>";
}

static int my_ulimit(int fd, int argc, char **argv)
{
	int resource;
	struct rlimit rlimit = { 0, 0 };
	if (argc > 3)
		return RESULT_SHOWUSAGE;

	if (argc == 1) {
		char arg2[3];
		char *newargv[2] = { "ulimit", arg2 };
		for (resource = 0; resource < sizeof(limits) / sizeof(limits[0]); resource++) {
			ast_copy_string(arg2, limits[resource].limit, sizeof(arg2));
			my_ulimit(fd, 2, newargv);
		}
		return RESULT_SUCCESS;
	} else {
		resource = str2limit(argv[1]);
		if (resource == -1) {
			ast_cli(fd, "Unknown resource\n");
			return RESULT_FAILURE;
		}

		if (argc == 3) {
			if (resource != RLIMIT_NOFILE && resource != RLIMIT_CORE && resource != RLIMIT_NPROC && resource != RLIMIT_FSIZE) {
				ast_cli(fd, "Resource not permitted to be set\n");
				return RESULT_FAILURE;
			}

			sscanf(argv[2], "%d", (int *)&rlimit.rlim_cur);
			rlimit.rlim_max = rlimit.rlim_cur;
			setrlimit(resource, &rlimit);
			return RESULT_SUCCESS;
		} else {
			if (!getrlimit(resource, &rlimit)) {
				char printlimit[32];
				const char *desc;
				if (rlimit.rlim_max == RLIM_INFINITY)
					ast_copy_string(printlimit, "effectively unlimited", sizeof(printlimit));
				else
					snprintf(printlimit, sizeof(printlimit), "limited to %d", (int)rlimit.rlim_cur);
				desc = str2desc(argv[1]);
				ast_cli(fd, "%c%s (%s) is %s.\n", toupper(desc[0]), desc + 1, argv[1], printlimit);
			} else
				ast_cli(fd, "Could not retrieve resource limits for %s: %s\n", str2desc(argv[1]), strerror(errno));
			return RESULT_SUCCESS;
		}
	}
}

static char *complete_ulimit(const char *line, const char *word, int pos, int state)
{
	int which = 0, i;
	int wordlen = strlen(word);

	if (pos > 2)
		return NULL;
	for (i = 0; i < sizeof(limits) / sizeof(limits[0]); i++) {
		if (!strncasecmp(limits[i].limit, word, wordlen)) {
			if (++which > state)
				return ast_strdup(limits[i].limit);
		}
	}
	return NULL;
}

static const char ulimit_usage[] =
"Usage: ulimit {-d|-l|-f|-m|-s|-t|-u|-v|-c|-n} [<num>]\n"
"       Shows or sets the corresponding resource limit.\n"
"         -d  Process data segment [readonly]\n"
"         -l  Memory lock size [readonly]\n"
"         -f  File size\n"
"         -m  Process resident memory [readonly]\n"
"         -s  Process stack size [readonly]\n"
"         -t  CPU usage [readonly]\n"
"         -u  Child processes\n"
"         -v  Process virtual memory [readonly]\n"
"         -c  Core dump file size\n"
"         -n  Number of file descriptors\n";

static struct ast_cli_entry cli_ulimit = {
	{ "ulimit", NULL }, my_ulimit,
	"Set or show process resource limits", ulimit_usage, complete_ulimit };

static int unload_module(void)
{
	return ast_cli_unregister(&cli_ulimit);
}

static int load_module(void)
{
	return ast_cli_register(&cli_ulimit)? AST_MODULE_LOAD_FAILURE: AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Resource limits");

