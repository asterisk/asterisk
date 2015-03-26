/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2009, Digium, Inc.
 *
 * Tilghman Lesher <tlesher@digium.com>
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
 * \brief Debugging routines for file descriptor leaks
 *
 * \author Tilghman Lesher <tlesher@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#ifdef DEBUG_FD_LEAKS

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <time.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "asterisk/cli.h"
#include "asterisk/logger.h"
#include "asterisk/options.h"
#include "asterisk/lock.h"
#include "asterisk/strings.h"
#include "asterisk/unaligned.h"

static struct fdleaks {
	char file[40];
	int line;
	char function[25];
	char callname[10];
	char callargs[60];
	unsigned int isopen:1;
} fdleaks[1024] = { { "", }, };

#define	COPY(dst, src)                                             \
	do {                                                           \
		int dlen = sizeof(dst), slen = strlen(src);                \
		if (slen + 1 > dlen) {                                     \
			char *slash = strrchr(src, '/');                       \
			if (slash) {                                           \
				ast_copy_string(dst, slash + 1, dlen);             \
			} else {                                               \
				ast_copy_string(dst, src + slen - dlen + 1, dlen); \
			}                                                      \
		} else {                                                   \
			ast_copy_string(dst, src, dlen);                       \
		}                                                          \
	} while (0)

#define STORE_COMMON(offset, name, ...)     \
	COPY(fdleaks[offset].file, file);       \
	fdleaks[offset].line = line;            \
	COPY(fdleaks[offset].function, func);   \
	strcpy(fdleaks[offset].callname, name); \
	snprintf(fdleaks[offset].callargs, sizeof(fdleaks[offset].callargs), __VA_ARGS__); \
	fdleaks[offset].isopen = 1;

#undef open
int __ast_fdleak_open(const char *file, int line, const char *func, const char *path, int flags, ...)
{
	int res;
	va_list ap;
	int mode;

	if (flags & O_CREAT) {
		va_start(ap, flags);
		mode = va_arg(ap, int);
		va_end(ap);
		res = open(path, flags, mode);
		if (res > -1 && res < (sizeof(fdleaks) / sizeof(fdleaks[0]))) {
			char sflags[80];
			snprintf(sflags, sizeof(sflags), "O_CREAT%s%s%s%s%s%s%s%s",
				flags & O_APPEND ? "|O_APPEND" : "",
				flags & O_EXCL ? "|O_EXCL" : "",
				flags & O_NONBLOCK ? "|O_NONBLOCK" : "",
				flags & O_TRUNC ? "|O_TRUNC" : "",
				flags & O_RDWR ? "|O_RDWR" : "",
#if O_RDONLY == 0
				!(flags & (O_WRONLY | O_RDWR)) ? "|O_RDONLY" : "",
#else
				flags & O_RDONLY ? "|O_RDONLY" : "",
#endif
				flags & O_WRONLY ? "|O_WRONLY" : "",
				"");
			flags &= ~(O_CREAT | O_APPEND | O_EXCL | O_NONBLOCK | O_TRUNC | O_RDWR | O_RDONLY | O_WRONLY);
			if (flags) {
				STORE_COMMON(res, "open", "\"%s\",%s|%d,%04o", path, sflags, flags, mode);
			} else {
				STORE_COMMON(res, "open", "\"%s\",%s,%04o", path, sflags, mode);
			}
		}
	} else {
		res = open(path, flags);
		if (res > -1 && res < (sizeof(fdleaks) / sizeof(fdleaks[0]))) {
			STORE_COMMON(res, "open", "\"%s\",%d", path, flags);
		}
	}
	return res;
}

#undef pipe
int __ast_fdleak_pipe(int *fds, const char *file, int line, const char *func)
{
	int i, res = pipe(fds);
	if (res) {
		return res;
	}
	for (i = 0; i < 2; i++) {
		STORE_COMMON(fds[i], "pipe", "{%d,%d}", fds[0], fds[1]);
	}
	return 0;
}

#undef socket
int __ast_fdleak_socket(int domain, int type, int protocol, const char *file, int line, const char *func)
{
	char sdomain[20], stype[20], *sproto = NULL;
	struct protoent *pe;
	int res = socket(domain, type, protocol);
	if (res < 0 || res > 1023) {
		return res;
	}

	if ((pe = getprotobynumber(protocol))) {
		sproto = pe->p_name;
	}

	if (domain == PF_UNIX) {
		ast_copy_string(sdomain, "PF_UNIX", sizeof(sdomain));
	} else if (domain == PF_INET) {
		ast_copy_string(sdomain, "PF_INET", sizeof(sdomain));
	} else {
		snprintf(sdomain, sizeof(sdomain), "%d", domain);
	}

	if (type == SOCK_DGRAM) {
		ast_copy_string(stype, "SOCK_DGRAM", sizeof(stype));
		if (protocol == 0) {
			sproto = "udp";
		}
	} else if (type == SOCK_STREAM) {
		ast_copy_string(stype, "SOCK_STREAM", sizeof(stype));
		if (protocol == 0) {
			sproto = "tcp";
		}
	} else {
		snprintf(stype, sizeof(stype), "%d", type);
	}

	if (sproto) {
		STORE_COMMON(res, "socket", "%s,%s,\"%s\"", sdomain, stype, sproto);
	} else {
		STORE_COMMON(res, "socket", "%s,%s,\"%d\"", sdomain, stype, protocol);
	}
	return res;
}

#undef close
int __ast_fdleak_close(int fd)
{
	int res = close(fd);
	if (!res && fd > -1 && fd < 1024) {
		fdleaks[fd].isopen = 0;
	}
	return res;
}

#undef fopen
FILE *__ast_fdleak_fopen(const char *path, const char *mode, const char *file, int line, const char *func)
{
	FILE *res = fopen(path, mode);
	int fd;
	if (!res) {
		return res;
	}
	fd = fileno(res);
	STORE_COMMON(fd, "fopen", "\"%s\",\"%s\"", path, mode);
	return res;
}

#undef fclose
int __ast_fdleak_fclose(FILE *ptr)
{
	int fd, res;
	if (!ptr) {
		return fclose(ptr);
	}

	fd = fileno(ptr);
	if ((res = fclose(ptr)) || fd < 0 || fd > 1023) {
		return res;
	}
	fdleaks[fd].isopen = 0;
	return res;
}

#undef dup2
int __ast_fdleak_dup2(int oldfd, int newfd, const char *file, int line, const char *func)
{
	int res = dup2(oldfd, newfd);
	if (res < 0 || res > 1023) {
		return res;
	}
	STORE_COMMON(res, "dup2", "%d,%d", oldfd, newfd);
	return res;
}

#undef dup
int __ast_fdleak_dup(int oldfd, const char *file, int line, const char *func)
{
	int res = dup(oldfd);
	if (res < 0 || res > 1023) {
		return res;
	}
	STORE_COMMON(res, "dup2", "%d", oldfd);
	return res;
}

static char *handle_show_fd(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int i;
	char line[24];
	struct rlimit rl;
	switch (cmd) {
	case CLI_INIT:
		e->command = "core show fd";
		e->usage =
			"Usage: core show fd\n"
			"       List all file descriptors currently in use and where\n"
			"       each was opened, and with what command.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	getrlimit(RLIMIT_FSIZE, &rl);
	if (rl.rlim_cur == RLIM_INFINITY || rl.rlim_max == RLIM_INFINITY) {
		ast_copy_string(line, "unlimited", sizeof(line));
	} else {
		snprintf(line, sizeof(line), "%d/%d", (int) rl.rlim_cur, (int) rl.rlim_max);
	}
	ast_cli(a->fd, "Current maxfiles: %s\n", line);
	for (i = 0; i < 1024; i++) {
		if (fdleaks[i].isopen) {
			snprintf(line, sizeof(line), "%d", fdleaks[i].line);
			ast_cli(a->fd, "%5d %15s:%-7.7s (%-25s): %s(%s)\n", i, fdleaks[i].file, line, fdleaks[i].function, fdleaks[i].callname, fdleaks[i].callargs);
		}
	}
	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_show_fd = AST_CLI_DEFINE(handle_show_fd, "Show open file descriptors");

static void fd_shutdown(void)
{
	ast_cli_unregister(&cli_show_fd);
}

int ast_fd_init(void)
{
	ast_register_cleanup(fd_shutdown);
	return ast_cli_register(&cli_show_fd);
}

#else  /* !defined(DEBUG_FD_LEAKS) */
int ast_fd_init(void)
{
	return 0;
}
#endif /* defined(DEBUG_FD_LEAKS) */

