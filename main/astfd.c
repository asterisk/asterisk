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
 * \author Tilghman Lesher \verbatim <tlesher@digium.com> \endverbatim
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#ifdef DEBUG_FD_LEAKS

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
#include "asterisk/localtime.h"
#include "asterisk/time.h"

static struct fdleaks {
	const char *callname;
	int line;
	unsigned int isopen:1;
	char file[40];
	char function[25];
	char callargs[100];
	struct timeval now;
} fdleaks[1024] = { { "", }, };

/* COPY does ast_copy_string(dst, src, sizeof(dst)), except:
 * - if it doesn't fit, it copies the value after the slash
 *   (possibly truncated)
 * - if there is no slash, it copies the value with the head
 *   truncated */
#define	COPY(dst, src)                                             \
	do {                                                           \
		int dlen = sizeof(dst), slen = strlen(src);                \
		if (slen + 1 > dlen) {                                     \
			const char *slash = strrchr(src, '/');                 \
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
	do { \
		struct fdleaks *tmp = &fdleaks[offset]; \
		tmp->now = ast_tvnow(); \
		COPY(tmp->file, file);      \
		tmp->line = line;           \
		COPY(tmp->function, func);  \
		tmp->callname = name;       \
		snprintf(tmp->callargs, sizeof(tmp->callargs), __VA_ARGS__); \
		tmp->isopen = 1;            \
	} while (0)

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
		if (res > -1 && res < ARRAY_LEN(fdleaks)) {
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
		if (res > -1 && res < ARRAY_LEN(fdleaks)) {
			STORE_COMMON(res, "open", "\"%s\",%d", path, flags);
		}
	}
	return res;
}

#undef accept
int __ast_fdleak_accept(int socket, struct sockaddr *address, socklen_t *address_len,
	const char *file, int line, const char *func)
{
	int res = accept(socket, address, address_len);

	if (res >= 0) {
		STORE_COMMON(res, "accept", "{%d}", socket);
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
		if (fds[i] > -1 && fds[i] < ARRAY_LEN(fdleaks)) {
			STORE_COMMON(fds[i], "pipe", "{%d,%d}", fds[0], fds[1]);
		}
	}
	return 0;
}

#undef socketpair
int __ast_fdleak_socketpair(int domain, int type, int protocol, int sv[2],
	const char *file, int line, const char *func)
{
	int i, res = socketpair(domain, type, protocol, sv);
	if (res) {
		return res;
	}
	for (i = 0; i < 2; i++) {
		if (sv[i] > -1 && sv[i] < ARRAY_LEN(fdleaks)) {
			STORE_COMMON(sv[i], "socketpair", "{%d,%d}", sv[0], sv[1]);
		}
	}
	return 0;
}

#if defined(HAVE_EVENTFD)
#undef eventfd
#include <sys/eventfd.h>
int __ast_fdleak_eventfd(unsigned int initval, int flags, const char *file, int line, const char *func)
{
	int res = eventfd(initval, flags);

	if (res >= 0) {
		STORE_COMMON(res, "eventfd", "{%d}", res);
	}

	return res;
}
#endif

#if defined(HAVE_TIMERFD)
#undef timerfd_create
#include <sys/timerfd.h>
int __ast_fdleak_timerfd_create(int clockid, int flags, const char *file, int line, const char *func)
{
	int res = timerfd_create(clockid, flags);

	if (res >= 0) {
		STORE_COMMON(res, "timerfd_create", "{%d}", res);
	}

	return res;
}
#endif

#undef socket
int __ast_fdleak_socket(int domain, int type, int protocol, const char *file, int line, const char *func)
{
	char sdomain[20], stype[20], *sproto = NULL;
	struct protoent *pe;
	int res = socket(domain, type, protocol);
	if (res < 0 || res >= ARRAY_LEN(fdleaks)) {
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
	if (!res && fd > -1 && fd < ARRAY_LEN(fdleaks)) {
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
	if (fd > -1 && fd < ARRAY_LEN(fdleaks)) {
		STORE_COMMON(fd, "fopen", "\"%s\",\"%s\"", path, mode);
	}
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
	if ((res = fclose(ptr)) || fd < 0 || fd >= ARRAY_LEN(fdleaks)) {
		return res;
	}
	fdleaks[fd].isopen = 0;
	return res;
}

#undef dup2
int __ast_fdleak_dup2(int oldfd, int newfd, const char *file, int line, const char *func)
{
	int res = dup2(oldfd, newfd);
	if (res < 0 || res >= ARRAY_LEN(fdleaks)) {
		return res;
	}
	/* On success, newfd will be closed automatically if it was already
	 * open. We don't need to mention anything about that, we're updating
	 * the value anyway. */
	STORE_COMMON(res, "dup2", "%d,%d", oldfd, newfd); /* res == newfd */
	return res;
}

#undef dup
int __ast_fdleak_dup(int oldfd, const char *file, int line, const char *func)
{
	int res = dup(oldfd);
	if (res < 0 || res >= ARRAY_LEN(fdleaks)) {
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
	getrlimit(RLIMIT_NOFILE, &rl);
	if (rl.rlim_cur == RLIM_INFINITY) {
		ast_copy_string(line, "unlimited", sizeof(line));
	} else {
		snprintf(line, sizeof(line), "%d", (int) rl.rlim_cur);
	}
	ast_cli(a->fd, "Current maxfiles: %s\n", line);
	for (i = 0; i < ARRAY_LEN(fdleaks); i++) {
		if (fdleaks[i].isopen) {
			struct ast_tm tm = {0};
			char datestring[256];

			ast_localtime(&fdleaks[i].now, &tm, NULL);
			ast_strftime(datestring, sizeof(datestring), "%F %T", &tm);
			snprintf(line, sizeof(line), "%d", fdleaks[i].line);
			ast_cli(a->fd, "%5d [%s] %22s:%-7.7s (%-25s): %s(%s)\n", i, datestring, fdleaks[i].file, line, fdleaks[i].function, fdleaks[i].callname, fdleaks[i].callargs);
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
