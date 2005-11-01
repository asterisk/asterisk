/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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


/* Doxygenified Copyright Header */
/*!
 * \mainpage Asterisk -- An Open Source Telephony Toolkit
 *
 * \arg \ref DevDoc 
 * \arg \ref ConfigFiles
 *
 * \section copyright Copyright and author
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 * Asterisk is a trade mark registered by Digium, Inc.
 *
 * \author Mark Spencer <markster@digium.com>
 * Also see \ref AstCREDITS
 *
 * \section license License
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 *
 * \verbinclude LICENSE
 *
 */

/*! \file
  \brief Top level source file for Asterisk  - the Open Source PBX. Implementation
  of PBX core functions and CLI interface.
  
 */

#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdio.h>
#include <signal.h>
#include <sched.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <sys/resource.h>
#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <regex.h>

#if  defined(__FreeBSD__) || defined( __NetBSD__ ) || defined(SOLARIS)
#include <netdb.h>
#endif

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/logger.h"
#include "asterisk/options.h"
#include "asterisk/cli.h"
#include "asterisk/channel.h"
#include "asterisk/ulaw.h"
#include "asterisk/alaw.h"
#include "asterisk/callerid.h"
#include "asterisk/module.h"
#include "asterisk/image.h"
#include "asterisk/tdd.h"
#include "asterisk/term.h"
#include "asterisk/manager.h"
#include "asterisk/cdr.h"
#include "asterisk/pbx.h"
#include "asterisk/enum.h"
#include "asterisk/rtp.h"
#include "asterisk/app.h"
#include "asterisk/lock.h"
#include "asterisk/utils.h"
#include "asterisk/file.h"
#include "asterisk/io.h"
#include "asterisk/lock.h"
#include "editline/histedit.h"
#include "asterisk/config.h"
#include "asterisk/version.h"
#include "asterisk/build.h"
#include "asterisk/linkedlists.h"
#include "asterisk/devicestate.h"

#include "asterisk/doxyref.h"		/* Doxygen documentation */

#include "defaults.h"

#ifndef AF_LOCAL
#define AF_LOCAL AF_UNIX
#define PF_LOCAL PF_UNIX
#endif

#define AST_MAX_CONNECTS 128
#define NUM_MSGS 64

#define WELCOME_MESSAGE ast_verbose( "Asterisk " ASTERISK_VERSION ", Copyright (C) 1999 - 2005 Digium.\n"); \
		ast_verbose( "Written by Mark Spencer <markster@digium.com>\n"); \
		ast_verbose( "=========================================================================\n")

int option_verbose=0;
int option_debug=0;
int option_exec_includes=0;
int option_nofork=0;
int option_quiet=0;
int option_console=0;
int option_highpriority=0;
int option_remote=0;
int option_exec=0;
int option_initcrypto=0;
int option_nocolor;
int option_dumpcore = 0;
int option_cache_record_files = 0;
int option_timestamp = 0;
int option_overrideconfig = 0;
int option_reconnect = 0;
int option_transcode_slin = 1;
int option_transmit_silence_during_record = 0;
int option_maxcalls = 0;
double option_maxload = 0.0;
int option_dontwarn = 0;
int option_priority_jumping = 1;
int fully_booted = 0;
char record_cache_dir[AST_CACHE_DIR_LEN] = AST_TMP_DIR;
char debug_filename[AST_FILENAME_MAX] = "";

static int ast_socket = -1;		/*!< UNIX Socket for allowing remote control */
static int ast_consock = -1;		/*!< UNIX Socket for controlling another asterisk */
int ast_mainpid;
struct console {
	int fd;				/*!< File descriptor */
	int p[2];			/*!< Pipe */
	pthread_t t;			/*!< Thread of handler */
};

static struct ast_atexit {
	void (*func)(void);
	struct ast_atexit *next;
} *atexits = NULL;

AST_MUTEX_DEFINE_STATIC(atexitslock);

time_t ast_startuptime;
time_t ast_lastreloadtime;

static History *el_hist = NULL;
static EditLine *el = NULL;
static char *remotehostname;

struct console consoles[AST_MAX_CONNECTS];

char defaultlanguage[MAX_LANGUAGE] = DEFAULT_LANGUAGE;

static int ast_el_add_history(char *);
static int ast_el_read_history(char *);
static int ast_el_write_history(char *);

char ast_config_AST_CONFIG_DIR[AST_CONFIG_MAX_PATH];
char ast_config_AST_CONFIG_FILE[AST_CONFIG_MAX_PATH];
char ast_config_AST_MODULE_DIR[AST_CONFIG_MAX_PATH];
char ast_config_AST_SPOOL_DIR[AST_CONFIG_MAX_PATH];
char ast_config_AST_MONITOR_DIR[AST_CONFIG_MAX_PATH];
char ast_config_AST_VAR_DIR[AST_CONFIG_MAX_PATH];
char ast_config_AST_LOG_DIR[AST_CONFIG_MAX_PATH];
char ast_config_AST_AGI_DIR[AST_CONFIG_MAX_PATH];
char ast_config_AST_DB[AST_CONFIG_MAX_PATH];
char ast_config_AST_KEY_DIR[AST_CONFIG_MAX_PATH];
char ast_config_AST_PID[AST_CONFIG_MAX_PATH];
char ast_config_AST_SOCKET[AST_CONFIG_MAX_PATH];
char ast_config_AST_RUN_DIR[AST_CONFIG_MAX_PATH];
char ast_config_AST_CTL_PERMISSIONS[AST_CONFIG_MAX_PATH];
char ast_config_AST_CTL_OWNER[AST_CONFIG_MAX_PATH] = "\0";
char ast_config_AST_CTL_GROUP[AST_CONFIG_MAX_PATH] = "\0";
char ast_config_AST_CTL[AST_CONFIG_MAX_PATH] = "asterisk.ctl";

static char *_argv[256];
static int shuttingdown = 0;
static int restartnow = 0;
static pthread_t consolethread = AST_PTHREADT_NULL;

#if !defined(LOW_MEMORY)
struct file_version {
	AST_LIST_ENTRY(file_version) list;
	const char *file;
	char *version;
};

static AST_LIST_HEAD_STATIC(file_versions, file_version);

void ast_register_file_version(const char *file, const char *version)
{
	struct file_version *new;
	char *work;
	size_t version_length;

	work = ast_strdupa(version);
	work = ast_strip(ast_strip_quoted(work, "$", "$"));
	version_length = strlen(work) + 1;

	new = calloc(1, sizeof(*new) + version_length);
	if (!new)
		return;

	new->file = file;
	new->version = (char *) new + sizeof(*new);
	memcpy(new->version, work, version_length);
	AST_LIST_LOCK(&file_versions);
	AST_LIST_INSERT_HEAD(&file_versions, new, list);
	AST_LIST_UNLOCK(&file_versions);
}

void ast_unregister_file_version(const char *file)
{
	struct file_version *find;

	AST_LIST_LOCK(&file_versions);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&file_versions, find, list) {
		if (!strcasecmp(find->file, file)) {
			AST_LIST_REMOVE_CURRENT(&file_versions, list);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
	AST_LIST_UNLOCK(&file_versions);
	if (find)
		free(find);
}

static char show_version_files_help[] = 
"Usage: show version files [like <pattern>]\n"
"       Shows the revision numbers of the files used to build this copy of Asterisk.\n"
"       Optional regular expression pattern is used to filter the file list.\n";

/*! CLI command to list module versions */
static int handle_show_version_files(int fd, int argc, char *argv[])
{
#define FORMAT "%-25.25s %-40.40s\n"
	struct file_version *iterator;
	regex_t regexbuf;
	int havepattern = 0;
	int havename = 0;
	int count_files = 0;

	switch (argc) {
	case 5:
		if (!strcasecmp(argv[3], "like")) {
			if (regcomp(&regexbuf, argv[4], REG_EXTENDED | REG_NOSUB))
				return RESULT_SHOWUSAGE;
			havepattern = 1;
		} else
			return RESULT_SHOWUSAGE;
		break;
	case 4:
		havename = 1;
		break;
	case 3:
		break;
	default:
		return RESULT_SHOWUSAGE;
	}

	ast_cli(fd, FORMAT, "File", "Revision");
	ast_cli(fd, FORMAT, "----", "--------");
	AST_LIST_LOCK(&file_versions);
	AST_LIST_TRAVERSE(&file_versions, iterator, list) {
		if (havename && strcasecmp(iterator->file, argv[3]))
			continue;

		if (havepattern && regexec(&regexbuf, iterator->file, 0, NULL, 0))
			continue;

		ast_cli(fd, FORMAT, iterator->file, iterator->version);
		count_files++;
		if (havename)
			break;
	}
	AST_LIST_UNLOCK(&file_versions);
	if (!havename) {
		ast_cli(fd, "%d files listed.\n", count_files);
	}

	if (havepattern)
		regfree(&regexbuf);

	return RESULT_SUCCESS;
#undef FORMAT
}

static char *complete_show_version_files(char *line, char *word, int pos, int state)
{
	struct file_version *find;
	int which = 0;
	char *ret = NULL;
	int matchlen = strlen(word);

	if (pos != 3)
		return NULL;

	AST_LIST_LOCK(&file_versions);
	AST_LIST_TRAVERSE(&file_versions, find, list) {
		if (!strncasecmp(word, find->file, matchlen)) {
			if (++which > state) {
				ret = strdup(find->file);
				break;
			}
		}
	}
	AST_LIST_UNLOCK(&file_versions);

	return ret;
}
#endif /* ! LOW_MEMORY */

int ast_register_atexit(void (*func)(void))
{
	int res = -1;
	struct ast_atexit *ae;
	ast_unregister_atexit(func);
	ae = malloc(sizeof(struct ast_atexit));
	ast_mutex_lock(&atexitslock);
	if (ae) {
		memset(ae, 0, sizeof(struct ast_atexit));
		ae->next = atexits;
		ae->func = func;
		atexits = ae;
		res = 0;
	}
	ast_mutex_unlock(&atexitslock);
	return res;
}

void ast_unregister_atexit(void (*func)(void))
{
	struct ast_atexit *ae, *prev = NULL;
	ast_mutex_lock(&atexitslock);
	ae = atexits;
	while(ae) {
		if (ae->func == func) {
			if (prev)
				prev->next = ae->next;
			else
				atexits = ae->next;
			break;
		}
		prev = ae;
		ae = ae->next;
	}
	ast_mutex_unlock(&atexitslock);
}

static int fdprint(int fd, const char *s)
{
	return write(fd, s, strlen(s) + 1);
}

/*! NULL handler so we can collect the child exit status */
static void null_sig_handler(int signal)
{

}

AST_MUTEX_DEFINE_STATIC(safe_system_lock);
static unsigned int safe_system_level = 0;
static void *safe_system_prev_handler;

int ast_safe_system(const char *s)
{
	pid_t pid;
	int x;
	int res;
	struct rusage rusage;
	int status;
	unsigned int level;

	/* keep track of how many ast_safe_system() functions
	   are running at this moment
	*/
	ast_mutex_lock(&safe_system_lock);
	level = safe_system_level++;

	/* only replace the handler if it has not already been done */
	if (level == 0)
		safe_system_prev_handler = signal(SIGCHLD, null_sig_handler);

	ast_mutex_unlock(&safe_system_lock);

	pid = fork();

	if (pid == 0) {
		/* Close file descriptors and launch system command */
		for (x = STDERR_FILENO + 1; x < 4096; x++)
			close(x);
		execl("/bin/sh", "/bin/sh", "-c", s, NULL);
		exit(1);
	} else if (pid > 0) {
		for(;;) {
			res = wait4(pid, &status, 0, &rusage);
			if (res > -1) {
				res = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
				break;
			} else if (errno != EINTR) 
				break;
		}
	} else {
		ast_log(LOG_WARNING, "Fork failed: %s\n", strerror(errno));
		res = -1;
	}

	ast_mutex_lock(&safe_system_lock);
	level = --safe_system_level;

	/* only restore the handler if we are the last one */
	if (level == 0)
		signal(SIGCHLD, safe_system_prev_handler);

	ast_mutex_unlock(&safe_system_lock);

	return res;
}

/*!
 * write the string to all attached console clients
 */
static void ast_network_puts(const char *string)
{
	int x;
	for (x=0;x<AST_MAX_CONNECTS; x++) {
		if (consoles[x].fd > -1) 
			fdprint(consoles[x].p[1], string);
	}
}

/*!
 * write the string to the console, and all attached
 * console clients
 */
void ast_console_puts(const char *string)
{
	fputs(string, stdout);
	fflush(stdout);
	ast_network_puts(string);
}

static void network_verboser(const char *s, int pos, int replace, int complete)
	/* ARGUSED */
{
	if (replace) {
		char *t = alloca(strlen(s) + 2);
		if (t) {
			sprintf(t, "\r%s", s);
			if (complete)
				ast_network_puts(t);
		} else {
			ast_log(LOG_ERROR, "Out of memory\n");
			ast_network_puts(s);
		}
	} else {
		if (complete)
			ast_network_puts(s);
	}
}

static pthread_t lthread;

static void *netconsole(void *vconsole)
{
	struct console *con = vconsole;
	char hostname[MAXHOSTNAMELEN]="";
	char tmp[512];
	int res;
	struct pollfd fds[2];
	
	if (gethostname(hostname, sizeof(hostname)-1))
		ast_copy_string(hostname, "<Unknown>", sizeof(hostname));
	snprintf(tmp, sizeof(tmp), "%s/%d/%s\n", hostname, ast_mainpid, ASTERISK_VERSION);
	fdprint(con->fd, tmp);
	for(;;) {
		fds[0].fd = con->fd;
		fds[0].events = POLLIN;
		fds[0].revents = 0;
		fds[1].fd = con->p[0];
		fds[1].events = POLLIN;
		fds[1].revents = 0;

		res = poll(fds, 2, -1);
		if (res < 0) {
			if (errno != EINTR)
				ast_log(LOG_WARNING, "poll returned < 0: %s\n", strerror(errno));
			continue;
		}
		if (fds[0].revents) {
			res = read(con->fd, tmp, sizeof(tmp));
			if (res < 1) {
				break;
			}
			tmp[res] = 0;
			ast_cli_command(con->fd, tmp);
		}
		if (fds[1].revents) {
			res = read(con->p[0], tmp, sizeof(tmp));
			if (res < 1) {
				ast_log(LOG_ERROR, "read returned %d\n", res);
				break;
			}
			res = write(con->fd, tmp, res);
			if (res < 1)
				break;
		}
	}
	if (option_verbose > 2) 
		ast_verbose(VERBOSE_PREFIX_3 "Remote UNIX connection disconnected\n");
	close(con->fd);
	close(con->p[0]);
	close(con->p[1]);
	con->fd = -1;
	
	return NULL;
}

static void *listener(void *unused)
{
	struct sockaddr_un sunaddr;
	int s;
	socklen_t len;
	int x;
	int flags;
	struct pollfd fds[1];
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	for(;;) {
		if (ast_socket < 0)
			return NULL;
		fds[0].fd = ast_socket;
		fds[0].events= POLLIN;
		s = poll(fds, 1, -1);
		if (s < 0) {
			if (errno != EINTR)
				ast_log(LOG_WARNING, "poll returned error: %s\n", strerror(errno));
			continue;
		}
		len = sizeof(sunaddr);
		s = accept(ast_socket, (struct sockaddr *)&sunaddr, &len);
		if (s < 0) {
			if (errno != EINTR)
				ast_log(LOG_WARNING, "Accept returned %d: %s\n", s, strerror(errno));
		} else {
			for (x=0;x<AST_MAX_CONNECTS;x++) {
				if (consoles[x].fd < 0) {
					if (socketpair(AF_LOCAL, SOCK_STREAM, 0, consoles[x].p)) {
						ast_log(LOG_ERROR, "Unable to create pipe: %s\n", strerror(errno));
						consoles[x].fd = -1;
						fdprint(s, "Server failed to create pipe\n");
						close(s);
						break;
					}
					flags = fcntl(consoles[x].p[1], F_GETFL);
					fcntl(consoles[x].p[1], F_SETFL, flags | O_NONBLOCK);
					consoles[x].fd = s;
					if (ast_pthread_create(&consoles[x].t, &attr, netconsole, &consoles[x])) {
						ast_log(LOG_ERROR, "Unable to spawn thread to handle connection: %s\n", strerror(errno));
						consoles[x].fd = -1;
						fdprint(s, "Server failed to spawn thread\n");
						close(s);
					}
					break;
				}
			}
			if (x >= AST_MAX_CONNECTS) {
				fdprint(s, "No more connections allowed\n");
				ast_log(LOG_WARNING, "No more connections allowed\n");
				close(s);
			} else if (consoles[x].fd > -1) {
				if (option_verbose > 2) 
					ast_verbose(VERBOSE_PREFIX_3 "Remote UNIX connection\n");
			}
		}
	}
	return NULL;
}

static int ast_makesocket(void)
{
	struct sockaddr_un sunaddr;
	int res;
	int x;
	uid_t uid = -1;
	gid_t gid = -1;

	for (x = 0; x < AST_MAX_CONNECTS; x++)	
		consoles[x].fd = -1;
	unlink(ast_config_AST_SOCKET);
	ast_socket = socket(PF_LOCAL, SOCK_STREAM, 0);
	if (ast_socket < 0) {
		ast_log(LOG_WARNING, "Unable to create control socket: %s\n", strerror(errno));
		return -1;
	}		
	memset(&sunaddr, 0, sizeof(sunaddr));
	sunaddr.sun_family = AF_LOCAL;
	ast_copy_string(sunaddr.sun_path, ast_config_AST_SOCKET, sizeof(sunaddr.sun_path));
	res = bind(ast_socket, (struct sockaddr *)&sunaddr, sizeof(sunaddr));
	if (res) {
		ast_log(LOG_WARNING, "Unable to bind socket to %s: %s\n", ast_config_AST_SOCKET, strerror(errno));
		close(ast_socket);
		ast_socket = -1;
		return -1;
	}
	res = listen(ast_socket, 2);
	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to listen on socket %s: %s\n", ast_config_AST_SOCKET, strerror(errno));
		close(ast_socket);
		ast_socket = -1;
		return -1;
	}
	ast_register_verbose(network_verboser);
	ast_pthread_create(&lthread, NULL, listener, NULL);

	if (!ast_strlen_zero(ast_config_AST_CTL_OWNER)) {
		struct passwd *pw;
		if ((pw = getpwnam(ast_config_AST_CTL_OWNER)) == NULL) {
			ast_log(LOG_WARNING, "Unable to find uid of user %s\n", ast_config_AST_CTL_OWNER);
		} else {
			uid = pw->pw_uid;
		}
	}
		
	if (!ast_strlen_zero(ast_config_AST_CTL_GROUP)) {
		struct group *grp;
		if ((grp = getgrnam(ast_config_AST_CTL_GROUP)) == NULL) {
			ast_log(LOG_WARNING, "Unable to find gid of group %s\n", ast_config_AST_CTL_GROUP);
		} else {
			gid = grp->gr_gid;
		}
	}

	if (chown(ast_config_AST_SOCKET, uid, gid) < 0)
		ast_log(LOG_WARNING, "Unable to change ownership of %s: %s\n", ast_config_AST_SOCKET, strerror(errno));

	if (!ast_strlen_zero(ast_config_AST_CTL_PERMISSIONS)) {
		mode_t p;
		sscanf(ast_config_AST_CTL_PERMISSIONS, "%o", (int *) &p);
		if ((chmod(ast_config_AST_SOCKET, p)) < 0)
			ast_log(LOG_WARNING, "Unable to change file permissions of %s: %s\n", ast_config_AST_SOCKET, strerror(errno));
	}

	return 0;
}

static int ast_tryconnect(void)
{
	struct sockaddr_un sunaddr;
	int res;
	ast_consock = socket(PF_LOCAL, SOCK_STREAM, 0);
	if (ast_consock < 0) {
		ast_log(LOG_WARNING, "Unable to create socket: %s\n", strerror(errno));
		return 0;
	}
	memset(&sunaddr, 0, sizeof(sunaddr));
	sunaddr.sun_family = AF_LOCAL;
	ast_copy_string(sunaddr.sun_path, (char *)ast_config_AST_SOCKET, sizeof(sunaddr.sun_path));
	res = connect(ast_consock, (struct sockaddr *)&sunaddr, sizeof(sunaddr));
	if (res) {
		close(ast_consock);
		ast_consock = -1;
		return 0;
	} else
		return 1;
}

/*! Urgent handler
 Called by soft_hangup to interrupt the poll, read, or other
 system call.  We don't actually need to do anything though.  
 Remember: Cannot EVER ast_log from within a signal handler 
 SLD: seems to be some pthread activity relating to the printf anyway:
 which is leading to a deadlock? 
 */
static void urg_handler(int num)
{
#if 0
	if (option_debug > 2) 
		printf("-- Asterisk Urgent handler\n");
#endif
	signal(num, urg_handler);
	return;
}

static void hup_handler(int num)
{
	if (option_verbose > 1) 
		printf("Received HUP signal -- Reloading configs\n");
	if (restartnow)
		execvp(_argv[0], _argv);
	/* XXX This could deadlock XXX */
	ast_module_reload(NULL);
	signal(num, hup_handler);
}

static void child_handler(int sig)
{
	/* Must not ever ast_log or ast_verbose within signal handler */
	int n, status;

	/*
	 * Reap all dead children -- not just one
	 */
	for (n = 0; wait4(-1, &status, WNOHANG, NULL) > 0; n++)
		;
	if (n == 0 && option_debug)	
		printf("Huh?  Child handler, but nobody there?\n");
	signal(sig, child_handler);
}

/*! Set an X-term or screen title */
static void set_title(char *text)
{
	if (getenv("TERM") && strstr(getenv("TERM"), "xterm"))
		fprintf(stdout, "\033]2;%s\007", text);
}

static void set_icon(char *text)
{
	if (getenv("TERM") && strstr(getenv("TERM"), "xterm"))
		fprintf(stdout, "\033]1;%s\007", text);
}

/*! We set ourselves to a high priority, that we might pre-empt everything
   else.  If your PBX has heavy activity on it, this is a good thing.  */
int ast_set_priority(int pri)
{
	struct sched_param sched;
	memset(&sched, 0, sizeof(sched));
#ifdef __linux__
	if (pri) {  
		sched.sched_priority = 10;
		if (sched_setscheduler(0, SCHED_RR, &sched)) {
			ast_log(LOG_WARNING, "Unable to set high priority\n");
			return -1;
		} else
			if (option_verbose)
				ast_verbose("Set to realtime thread\n");
	} else {
		sched.sched_priority = 0;
		if (sched_setscheduler(0, SCHED_OTHER, &sched)) {
			ast_log(LOG_WARNING, "Unable to set normal priority\n");
			return -1;
		}
	}
#else
	if (pri) {
		if (setpriority(PRIO_PROCESS, 0, -10) == -1) {
			ast_log(LOG_WARNING, "Unable to set high priority\n");
			return -1;
		} else
			if (option_verbose)
				ast_verbose("Set to high priority\n");
	} else {
		if (setpriority(PRIO_PROCESS, 0, 0) == -1) {
			ast_log(LOG_WARNING, "Unable to set normal priority\n");
			return -1;
		}
	}
#endif
	return 0;
}

static void ast_run_atexits(void)
{
	struct ast_atexit *ae;
	ast_mutex_lock(&atexitslock);
	ae = atexits;
	while(ae) {
		if (ae->func) 
			ae->func();
		ae = ae->next;
	}
	ast_mutex_unlock(&atexitslock);
}

static void quit_handler(int num, int nice, int safeshutdown, int restart)
{
	char filename[80] = "";
	time_t s,e;
	int x;
	/* Try to get as many CDRs as possible submitted to the backend engines (if in batch mode) */
	ast_cdr_engine_term();
	if (safeshutdown) {
		shuttingdown = 1;
		if (!nice) {
			/* Begin shutdown routine, hanging up active channels */
			ast_begin_shutdown(1);
			if (option_verbose && option_console)
				ast_verbose("Beginning asterisk %s....\n", restart ? "restart" : "shutdown");
			time(&s);
			for(;;) {
				time(&e);
				/* Wait up to 15 seconds for all channels to go away */
				if ((e - s) > 15)
					break;
				if (!ast_active_channels())
					break;
				if (!shuttingdown)
					break;
				/* Sleep 1/10 of a second */
				usleep(100000);
			}
		} else {
			if (nice < 2)
				ast_begin_shutdown(0);
			if (option_verbose && option_console)
				ast_verbose("Waiting for inactivity to perform %s...\n", restart ? "restart" : "halt");
			for(;;) {
				if (!ast_active_channels())
					break;
				if (!shuttingdown)
					break;
				sleep(1);
			}
		}

		if (!shuttingdown) {
			if (option_verbose && option_console)
				ast_verbose("Asterisk %s cancelled.\n", restart ? "restart" : "shutdown");
			return;
		}
	}
	if (option_console || option_remote) {
		if (getenv("HOME")) 
			snprintf(filename, sizeof(filename), "%s/.asterisk_history", getenv("HOME"));
		if (!ast_strlen_zero(filename))
			ast_el_write_history(filename);
		if (el != NULL)
			el_end(el);
		if (el_hist != NULL)
			history_end(el_hist);
	}
	if (option_verbose)
		ast_verbose("Executing last minute cleanups\n");
	ast_run_atexits();
	/* Called on exit */
	if (option_verbose && option_console)
		ast_verbose("Asterisk %s ending (%d).\n", ast_active_channels() ? "uncleanly" : "cleanly", num);
	else if (option_debug)
		ast_log(LOG_DEBUG, "Asterisk ending (%d).\n", num);
	manager_event(EVENT_FLAG_SYSTEM, "Shutdown", "Shutdown: %s\r\nRestart: %s\r\n", ast_active_channels() ? "Uncleanly" : "Cleanly", restart ? "True" : "False");
	if (ast_socket > -1) {
		close(ast_socket);
		ast_socket = -1;
	}
	if (ast_consock > -1)
		close(ast_consock);
	if (ast_socket > -1)
		unlink((char *)ast_config_AST_SOCKET);
	if (!option_remote) unlink((char *)ast_config_AST_PID);
	printf(term_quit());
	if (restart) {
		if (option_verbose || option_console)
			ast_verbose("Preparing for Asterisk restart...\n");
		/* Mark all FD's for closing on exec */
		for (x=3;x<32768;x++) {
			fcntl(x, F_SETFD, FD_CLOEXEC);
		}
		if (option_verbose || option_console)
			ast_verbose("Restarting Asterisk NOW...\n");
		restartnow = 1;

		/* close logger */
		close_logger();

		/* If there is a consolethread running send it a SIGHUP 
		   so it can execvp, otherwise we can do it ourselves */
		if ((consolethread != AST_PTHREADT_NULL) && (consolethread != pthread_self())) {
			pthread_kill(consolethread, SIGHUP);
			/* Give the signal handler some time to complete */
			sleep(2);
		} else
			execvp(_argv[0], _argv);
	
	} else {
		/* close logger */
		close_logger();
	}
	exit(0);
}

static void __quit_handler(int num)
{
	quit_handler(num, 0, 1, 0);
}

static const char *fix_header(char *outbuf, int maxout, const char *s, char *cmp)
{
	const char *c;
	if (!strncmp(s, cmp, strlen(cmp))) {
		c = s + strlen(cmp);
		term_color(outbuf, cmp, COLOR_GRAY, 0, maxout);
		return c;
	}
	return NULL;
}

static void console_verboser(const char *s, int pos, int replace, int complete)
{
	char tmp[80];
	const char *c=NULL;
	/* Return to the beginning of the line */
	if (!pos) {
		fprintf(stdout, "\r");
		if ((c = fix_header(tmp, sizeof(tmp), s, VERBOSE_PREFIX_4)) ||
			(c = fix_header(tmp, sizeof(tmp), s, VERBOSE_PREFIX_3)) ||
			(c = fix_header(tmp, sizeof(tmp), s, VERBOSE_PREFIX_2)) ||
			(c = fix_header(tmp, sizeof(tmp), s, VERBOSE_PREFIX_1)))
			fputs(tmp, stdout);
	}
	if (c)
		fputs(c + pos,stdout);
	else
		fputs(s + pos,stdout);
	fflush(stdout);
	if (complete) {
		/* Wake up a poll()ing console */
		if (option_console && consolethread != AST_PTHREADT_NULL)
			pthread_kill(consolethread, SIGURG);
	}
}

static int ast_all_zeros(char *s)
{
	while(*s) {
		if (*s > 32)
			return 0;
		s++;  
	}
	return 1;
}

static void consolehandler(char *s)
{
	printf(term_end());
	fflush(stdout);
	/* Called when readline data is available */
	if (s && !ast_all_zeros(s))
		ast_el_add_history(s);
	/* Give the console access to the shell */
	if (s) {
		/* The real handler for bang */
		if (s[0] == '!') {
			if (s[1])
				ast_safe_system(s+1);
			else
				ast_safe_system(getenv("SHELL") ? getenv("SHELL") : "/bin/sh");
		} else 
		ast_cli_command(STDOUT_FILENO, s);
	} else
		fprintf(stdout, "\nUse \"quit\" to exit\n");
}

static int remoteconsolehandler(char *s)
{
	int ret = 0;
	/* Called when readline data is available */
	if (s && !ast_all_zeros(s))
		ast_el_add_history(s);
	/* Give the console access to the shell */
	if (s) {
		/* The real handler for bang */
		if (s[0] == '!') {
			if (s[1])
				ast_safe_system(s+1);
			else
				ast_safe_system(getenv("SHELL") ? getenv("SHELL") : "/bin/sh");
			ret = 1;
		}
		if ((strncasecmp(s, "quit", 4) == 0 || strncasecmp(s, "exit", 4) == 0) &&
		    (s[4] == '\0' || isspace(s[4]))) {
			quit_handler(0, 0, 0, 0);
			ret = 1;
		}
	} else
		fprintf(stdout, "\nUse \"quit\" to exit\n");

	return ret;
}

static char abort_halt_help[] = 
"Usage: abort shutdown\n"
"       Causes Asterisk to abort an executing shutdown or restart, and resume normal\n"
"       call operations.\n";

static char shutdown_now_help[] = 
"Usage: stop now\n"
"       Shuts down a running Asterisk immediately, hanging up all active calls .\n";

static char shutdown_gracefully_help[] = 
"Usage: stop gracefully\n"
"       Causes Asterisk to not accept new calls, and exit when all\n"
"       active calls have terminated normally.\n";

static char shutdown_when_convenient_help[] = 
"Usage: stop when convenient\n"
"       Causes Asterisk to perform a shutdown when all active calls have ended.\n";

static char restart_now_help[] = 
"Usage: restart now\n"
"       Causes Asterisk to hangup all calls and exec() itself performing a cold\n"
"       restart.\n";

static char restart_gracefully_help[] = 
"Usage: restart gracefully\n"
"       Causes Asterisk to stop accepting new calls and exec() itself performing a cold\n"
"       restart when all active calls have ended.\n";

static char restart_when_convenient_help[] = 
"Usage: restart when convenient\n"
"       Causes Asterisk to perform a cold restart when all active calls have ended.\n";

static char bang_help[] =
"Usage: !<command>\n"
"       Executes a given shell command\n";

#if 0
static int handle_quit(int fd, int argc, char *argv[])
{
	if (argc != 1)
		return RESULT_SHOWUSAGE;
	quit_handler(0, 0, 1, 0);
	return RESULT_SUCCESS;
}
#endif

static int handle_shutdown_now(int fd, int argc, char *argv[])
{
	if (argc != 2)
		return RESULT_SHOWUSAGE;
	quit_handler(0, 0 /* Not nice */, 1 /* safely */, 0 /* not restart */);
	return RESULT_SUCCESS;
}

static int handle_shutdown_gracefully(int fd, int argc, char *argv[])
{
	if (argc != 2)
		return RESULT_SHOWUSAGE;
	quit_handler(0, 1 /* nicely */, 1 /* safely */, 0 /* no restart */);
	return RESULT_SUCCESS;
}

static int handle_shutdown_when_convenient(int fd, int argc, char *argv[])
{
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	quit_handler(0, 2 /* really nicely */, 1 /* safely */, 0 /* don't restart */);
	return RESULT_SUCCESS;
}

static int handle_restart_now(int fd, int argc, char *argv[])
{
	if (argc != 2)
		return RESULT_SHOWUSAGE;
	quit_handler(0, 0 /* not nicely */, 1 /* safely */, 1 /* restart */);
	return RESULT_SUCCESS;
}

static int handle_restart_gracefully(int fd, int argc, char *argv[])
{
	if (argc != 2)
		return RESULT_SHOWUSAGE;
	quit_handler(0, 1 /* nicely */, 1 /* safely */, 1 /* restart */);
	return RESULT_SUCCESS;
}

static int handle_restart_when_convenient(int fd, int argc, char *argv[])
{
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	quit_handler(0, 2 /* really nicely */, 1 /* safely */, 1 /* restart */);
	return RESULT_SUCCESS;
}

static int handle_abort_halt(int fd, int argc, char *argv[])
{
	if (argc != 2)
		return RESULT_SHOWUSAGE;
	ast_cancel_shutdown();
	shuttingdown = 0;
	return RESULT_SUCCESS;
}

static int handle_bang(int fd, int argc, char *argv[])
{
	return RESULT_SUCCESS;
}

#define ASTERISK_PROMPT "*CLI> "

#define ASTERISK_PROMPT2 "%s*CLI> "

static struct ast_cli_entry core_cli[] = {
	{ { "abort", "halt", NULL }, handle_abort_halt,
	  "Cancel a running halt", abort_halt_help },
	{ { "stop", "now", NULL }, handle_shutdown_now,
	  "Shut down Asterisk immediately", shutdown_now_help },
	{ { "stop", "gracefully", NULL }, handle_shutdown_gracefully,
	  "Gracefully shut down Asterisk", shutdown_gracefully_help },
	{ { "stop", "when","convenient", NULL }, handle_shutdown_when_convenient,
	  "Shut down Asterisk at empty call volume", shutdown_when_convenient_help },
	{ { "restart", "now", NULL }, handle_restart_now,
	  "Restart Asterisk immediately", restart_now_help },
	{ { "restart", "gracefully", NULL }, handle_restart_gracefully,
	  "Restart Asterisk gracefully", restart_gracefully_help },
	{ { "restart", "when", "convenient", NULL }, handle_restart_when_convenient,
	  "Restart Asterisk at empty call volume", restart_when_convenient_help },
	{ { "!", NULL }, handle_bang,
	  "Execute a shell command", bang_help },
#if !defined(LOW_MEMORY)
	{ { "show", "version", "files", NULL }, handle_show_version_files,
	  "Show versions of files used to build Asterisk", show_version_files_help, complete_show_version_files },
#endif /* ! LOW_MEMORY */
};

static int ast_el_read_char(EditLine *el, char *cp)
{
	int num_read=0;
	int lastpos=0;
	struct pollfd fds[2];
	int res;
	int max;
	char buf[512];

	for (;;) {
		max = 1;
		fds[0].fd = ast_consock;
		fds[0].events = POLLIN;
		if (!option_exec) {
			fds[1].fd = STDIN_FILENO;
			fds[1].events = POLLIN;
			max++;
		}
		res = poll(fds, max, -1);
		if (res < 0) {
			if (errno == EINTR)
				continue;
			ast_log(LOG_ERROR, "poll failed: %s\n", strerror(errno));
			break;
		}

		if (!option_exec && fds[1].revents) {
			num_read = read(STDIN_FILENO, cp, 1);
			if (num_read < 1) {
				break;
			} else 
				return (num_read);
		}
		if (fds[0].revents) {
			res = read(ast_consock, buf, sizeof(buf) - 1);
			/* if the remote side disappears exit */
			if (res < 1) {
				fprintf(stderr, "\nDisconnected from Asterisk server\n");
				if (!option_reconnect) {
					quit_handler(0, 0, 0, 0);
				} else {
					int tries;
					int reconnects_per_second = 20;
					fprintf(stderr, "Attempting to reconnect for 30 seconds\n");
					for (tries=0;tries<30 * reconnects_per_second;tries++) {
						if (ast_tryconnect()) {
							fprintf(stderr, "Reconnect succeeded after %.3f seconds\n", 1.0 / reconnects_per_second * tries);
							printf(term_quit());
							WELCOME_MESSAGE;
							break;
						} else {
							usleep(1000000 / reconnects_per_second);
						}
					}
					if (tries >= 30 * reconnects_per_second) {
						fprintf(stderr, "Failed to reconnect for 30 seconds.  Quitting.\n");
						quit_handler(0, 0, 0, 0);
					}
				}
			}

			buf[res] = '\0';

			if (!option_exec && !lastpos)
				write(STDOUT_FILENO, "\r", 1);
			write(STDOUT_FILENO, buf, res);
			if ((buf[res-1] == '\n') || (buf[res-2] == '\n')) {
				*cp = CC_REFRESH;
				return(1);
			} else {
				lastpos = 1;
			}
		}
	}

	*cp = '\0';
	return (0);
}

static char *cli_prompt(EditLine *el)
{
	static char prompt[200];
	char *pfmt;
	int color_used=0;
	char term_code[20];

	if ((pfmt = getenv("ASTERISK_PROMPT"))) {
		char *t = pfmt, *p = prompt;
		memset(prompt, 0, sizeof(prompt));
		while (*t != '\0' && *p < sizeof(prompt)) {
			if (*t == '%') {
				char hostname[MAXHOSTNAMELEN]="";
				int i;
				struct timeval tv;
				struct tm tm;
#ifdef linux
				FILE *LOADAVG;
#endif
				int fgcolor = COLOR_WHITE, bgcolor = COLOR_BLACK;

				t++;
				switch (*t) {
					case 'C': /* color */
						t++;
						if (sscanf(t, "%d;%d%n", &fgcolor, &bgcolor, &i) == 2) {
							strncat(p, term_color_code(term_code, fgcolor, bgcolor, sizeof(term_code)),sizeof(prompt) - strlen(prompt) - 1);
							t += i - 1;
						} else if (sscanf(t, "%d%n", &fgcolor, &i) == 1) {
							strncat(p, term_color_code(term_code, fgcolor, 0, sizeof(term_code)),sizeof(prompt) - strlen(prompt) - 1);
							t += i - 1;
						}

						/* If the color has been reset correctly, then there's no need to reset it later */
						if ((fgcolor == COLOR_WHITE) && (bgcolor == COLOR_BLACK)) {
							color_used = 0;
						} else {
							color_used = 1;
						}
						break;
					case 'd': /* date */
						memset(&tm, 0, sizeof(struct tm));
						tv = ast_tvnow();
						if (localtime_r(&(tv.tv_sec), &tm)) {
							strftime(p, sizeof(prompt) - strlen(prompt), "%Y-%m-%d", &tm);
						}
						break;
					case 'h': /* hostname */
						if (!gethostname(hostname, sizeof(hostname) - 1)) {
							strncat(p, hostname, sizeof(prompt) - strlen(prompt) - 1);
						} else {
							strncat(p, "localhost", sizeof(prompt) - strlen(prompt) - 1);
						}
						break;
					case 'H': /* short hostname */
						if (!gethostname(hostname, sizeof(hostname) - 1)) {
							for (i=0;i<sizeof(hostname);i++) {
								if (hostname[i] == '.') {
									hostname[i] = '\0';
									break;
								}
							}
							strncat(p, hostname, sizeof(prompt) - strlen(prompt) - 1);
						} else {
							strncat(p, "localhost", sizeof(prompt) - strlen(prompt) - 1);
						}
						break;
#ifdef linux
					case 'l': /* load avg */
						t++;
						if ((LOADAVG = fopen("/proc/loadavg", "r"))) {
							float avg1, avg2, avg3;
							int actproc, totproc, npid, which;
							fscanf(LOADAVG, "%f %f %f %d/%d %d",
								&avg1, &avg2, &avg3, &actproc, &totproc, &npid);
							if (sscanf(t, "%d", &which) == 1) {
								switch (which) {
									case 1:
										snprintf(p, sizeof(prompt) - strlen(prompt), "%.2f", avg1);
										break;
									case 2:
										snprintf(p, sizeof(prompt) - strlen(prompt), "%.2f", avg2);
										break;
									case 3:
										snprintf(p, sizeof(prompt) - strlen(prompt), "%.2f", avg3);
										break;
									case 4:
										snprintf(p, sizeof(prompt) - strlen(prompt), "%d/%d", actproc, totproc);
										break;
									case 5:
										snprintf(p, sizeof(prompt) - strlen(prompt), "%d", npid);
										break;
								}
							}
						}
						break;
#endif
					case 't': /* time */
						memset(&tm, 0, sizeof(struct tm));
						tv = ast_tvnow();
						if (localtime_r(&(tv.tv_sec), &tm)) {
							strftime(p, sizeof(prompt) - strlen(prompt), "%H:%M:%S", &tm);
						}
						break;
					case '#': /* process console or remote? */
						if (! option_remote) {
							strncat(p, "#", sizeof(prompt) - strlen(prompt) - 1);
						} else {
							strncat(p, ">", sizeof(prompt) - strlen(prompt) - 1);
						}
						break;
					case '%': /* literal % */
						strncat(p, "%", sizeof(prompt) - strlen(prompt) - 1);
						break;
					case '\0': /* % is last character - prevent bug */
						t--;
						break;
				}
				while (*p != '\0') {
					p++;
				}
				t++;
			} else {
				*p = *t;
				p++;
				t++;
			}
		}
		if (color_used) {
			/* Force colors back to normal at end */
			term_color_code(term_code, COLOR_WHITE, COLOR_BLACK, sizeof(term_code));
			if (strlen(term_code) > sizeof(prompt) - strlen(prompt)) {
				strncat(prompt + sizeof(prompt) - strlen(term_code) - 1, term_code, strlen(term_code));
			} else {
				strncat(p, term_code, sizeof(term_code));
			}
		}
	} else if (remotehostname)
		snprintf(prompt, sizeof(prompt), ASTERISK_PROMPT2, remotehostname);
	else
		snprintf(prompt, sizeof(prompt), ASTERISK_PROMPT);

	return(prompt);	
}

static char **ast_el_strtoarr(char *buf)
{
	char **match_list = NULL, *retstr;
	size_t match_list_len;
	int matches = 0;

	match_list_len = 1;
	while ( (retstr = strsep(&buf, " ")) != NULL) {

		if (!strcmp(retstr, AST_CLI_COMPLETE_EOF))
			break;
		if (matches + 1 >= match_list_len) {
			match_list_len <<= 1;
			match_list = realloc(match_list, match_list_len * sizeof(char *));
		}

		match_list[matches++] = strdup(retstr);
	}

	if (!match_list)
		return (char **) NULL;

	if (matches>= match_list_len)
		match_list = realloc(match_list, (match_list_len + 1) * sizeof(char *));

	match_list[matches] = (char *) NULL;

	return match_list;
}

static int ast_el_sort_compare(const void *i1, const void *i2)
{
	char *s1, *s2;

	s1 = ((char **)i1)[0];
	s2 = ((char **)i2)[0];

	return strcasecmp(s1, s2);
}

static int ast_cli_display_match_list(char **matches, int len, int max)
{
	int i, idx, limit, count;
	int screenwidth = 0;
	int numoutput = 0, numoutputline = 0;

	screenwidth = ast_get_termcols(STDOUT_FILENO);

	/* find out how many entries can be put on one line, with two spaces between strings */
	limit = screenwidth / (max + 2);
	if (limit == 0)
		limit = 1;

	/* how many lines of output */
	count = len / limit;
	if (count * limit < len)
		count++;

	idx = 1;

	qsort(&matches[0], (size_t)(len + 1), sizeof(char *), ast_el_sort_compare);

	for (; count > 0; count--) {
		numoutputline = 0;
		for (i=0; i < limit && matches[idx]; i++, idx++) {

			/* Don't print dupes */
			if ( (matches[idx+1] != NULL && strcmp(matches[idx], matches[idx+1]) == 0 ) ) {
				i--;
				free(matches[idx]);
				matches[idx] = NULL;
				continue;
			}

			numoutput++;
			numoutputline++;
			fprintf(stdout, "%-*s  ", max, matches[idx]);
			free(matches[idx]);
			matches[idx] = NULL;
		}
		if (numoutputline > 0)
			fprintf(stdout, "\n");
	}

	return numoutput;
}


static char *cli_complete(EditLine *el, int ch)
{
	int len=0;
	char *ptr;
	int nummatches = 0;
	char **matches;
	int retval = CC_ERROR;
	char buf[2048];
	int res;

	LineInfo *lf = (LineInfo *)el_line(el);

	*(char *)lf->cursor = '\0';
	ptr = (char *)lf->cursor;
	if (ptr) {
		while (ptr > lf->buffer) {
			if (isspace(*ptr)) {
				ptr++;
				break;
			}
			ptr--;
		}
	}

	len = lf->cursor - ptr;

	if (option_remote) {
		snprintf(buf, sizeof(buf),"_COMMAND NUMMATCHES \"%s\" \"%s\"", lf->buffer, ptr); 
		fdprint(ast_consock, buf);
		res = read(ast_consock, buf, sizeof(buf));
		buf[res] = '\0';
		nummatches = atoi(buf);

		if (nummatches > 0) {
			char *mbuf;
			int mlen = 0, maxmbuf = 2048;
			/* Start with a 2048 byte buffer */
			mbuf = malloc(maxmbuf);
			if (!mbuf)
				return (char *)(CC_ERROR);
			snprintf(buf, sizeof(buf),"_COMMAND MATCHESARRAY \"%s\" \"%s\"", lf->buffer, ptr); 
			fdprint(ast_consock, buf);
			res = 0;
			mbuf[0] = '\0';
			while (!strstr(mbuf, AST_CLI_COMPLETE_EOF) && res != -1) {
				if (mlen + 1024 > maxmbuf) {
					/* Every step increment buffer 1024 bytes */
					maxmbuf += 1024;
					mbuf = realloc(mbuf, maxmbuf);
					if (!mbuf)
						return (char *)(CC_ERROR);
				}
				/* Only read 1024 bytes at a time */
				res = read(ast_consock, mbuf + mlen, 1024);
				if (res > 0)
					mlen += res;
			}
			mbuf[mlen] = '\0';

			matches = ast_el_strtoarr(mbuf);
			free(mbuf);
		} else
			matches = (char **) NULL;


	} else {

		nummatches = ast_cli_generatornummatches((char *)lf->buffer,ptr);
		matches = ast_cli_completion_matches((char *)lf->buffer,ptr);
	}

	if (matches) {
		int i;
		int matches_num, maxlen, match_len;

		if (matches[0][0] != '\0') {
			el_deletestr(el, (int) len);
			el_insertstr(el, matches[0]);
			retval = CC_REFRESH;
		}

		if (nummatches == 1) {
			/* Found an exact match */
			el_insertstr(el, " ");
			retval = CC_REFRESH;
		} else {
			/* Must be more than one match */
			for (i=1, maxlen=0; matches[i]; i++) {
				match_len = strlen(matches[i]);
				if (match_len > maxlen)
					maxlen = match_len;
			}
			matches_num = i - 1;
			if (matches_num >1) {
				fprintf(stdout, "\n");
				ast_cli_display_match_list(matches, nummatches, maxlen);
				retval = CC_REDISPLAY;
			} else { 
				el_insertstr(el," ");
				retval = CC_REFRESH;
			}
		}
	free(matches);
	}

	return (char *)(long)retval;
}

static int ast_el_initialize(void)
{
	HistEvent ev;
	char *editor = getenv("AST_EDITOR");

	if (el != NULL)
		el_end(el);
	if (el_hist != NULL)
		history_end(el_hist);

	el = el_init("asterisk", stdin, stdout, stderr);
	el_set(el, EL_PROMPT, cli_prompt);

	el_set(el, EL_EDITMODE, 1);		
	el_set(el, EL_EDITOR, editor ? editor : "emacs");		
	el_hist = history_init();
	if (!el || !el_hist)
		return -1;

	/* setup history with 100 entries */
	history(el_hist, &ev, H_SETSIZE, 100);

	el_set(el, EL_HIST, history, el_hist);

	el_set(el, EL_ADDFN, "ed-complete", "Complete argument", cli_complete);
	/* Bind <tab> to command completion */
	el_set(el, EL_BIND, "^I", "ed-complete", NULL);
	/* Bind ? to command completion */
	el_set(el, EL_BIND, "?", "ed-complete", NULL);
	/* Bind ^D to redisplay */
	el_set(el, EL_BIND, "^D", "ed-redisplay", NULL);

	return 0;
}

static int ast_el_add_history(char *buf)
{
	HistEvent ev;

	if (el_hist == NULL || el == NULL)
		ast_el_initialize();
	if (strlen(buf) > 256)
		return 0;
	return (history(el_hist, &ev, H_ENTER, buf));
}

static int ast_el_write_history(char *filename)
{
	HistEvent ev;

	if (el_hist == NULL || el == NULL)
		ast_el_initialize();

	return (history(el_hist, &ev, H_SAVE, filename));
}

static int ast_el_read_history(char *filename)
{
	char buf[256];
	FILE *f;
	int ret = -1;

	if (el_hist == NULL || el == NULL)
		ast_el_initialize();

	if ((f = fopen(filename, "r")) == NULL)
		return ret;

	while (!feof(f)) {
		fgets(buf, sizeof(buf), f);
		if (!strcmp(buf, "_HiStOrY_V2_\n"))
			continue;
		if (ast_all_zeros(buf))
			continue;
		if ((ret = ast_el_add_history(buf)) == -1)
			break;
	}
	fclose(f);

	return ret;
}

static void ast_remotecontrol(char * data)
{
	char buf[80];
	int res;
	char filename[80] = "";
	char *hostname;
	char *cpid;
	char *version;
	int pid;
	char tmp[80];
	char *stringp=NULL;

	char *ebuf;
	int num = 0;

	read(ast_consock, buf, sizeof(buf));
	if (data)
		write(ast_consock, data, strlen(data) + 1);
	stringp=buf;
	hostname = strsep(&stringp, "/");
	cpid = strsep(&stringp, "/");
	version = strsep(&stringp, "\n");
	if (!version)
		version = "<Version Unknown>";
	stringp=hostname;
	strsep(&stringp, ".");
	if (cpid)
		pid = atoi(cpid);
	else
		pid = -1;
	snprintf(tmp, sizeof(tmp), "set verbose atleast %d", option_verbose);
	fdprint(ast_consock, tmp);
	snprintf(tmp, sizeof(tmp), "set debug atleast %d", option_debug);
	fdprint(ast_consock, tmp);
	ast_verbose("Connected to Asterisk %s currently running on %s (pid = %d)\n", version, hostname, pid);
	remotehostname = hostname;
	if (getenv("HOME")) 
		snprintf(filename, sizeof(filename), "%s/.asterisk_history", getenv("HOME"));
	if (el_hist == NULL || el == NULL)
		ast_el_initialize();

	el_set(el, EL_GETCFN, ast_el_read_char);

	if (!ast_strlen_zero(filename))
		ast_el_read_history(filename);

	if (option_exec && data) {  /* hack to print output then exit if asterisk -rx is used */
		char tempchar;
		struct pollfd fds[0];
		fds[0].fd = ast_consock;
		fds[0].events = POLLIN;
		fds[0].revents = 0;
		while(poll(fds, 1, 100) > 0) {
			ast_el_read_char(el, &tempchar);
		}
		return;
	}
	for(;;) {
		ebuf = (char *)el_gets(el, &num);

		if (!ast_strlen_zero(ebuf)) {
			if (ebuf[strlen(ebuf)-1] == '\n')
				ebuf[strlen(ebuf)-1] = '\0';
			if (!remoteconsolehandler(ebuf)) {
				res = write(ast_consock, ebuf, strlen(ebuf) + 1);
				if (res < 1) {
					ast_log(LOG_WARNING, "Unable to write: %s\n", strerror(errno));
					break;
				}
			}
		}
	}
	printf("\nDisconnected from Asterisk server\n");
}

static int show_version(void)
{
	printf("Asterisk " ASTERISK_VERSION "\n");
	return 0;
}

static int show_cli_help(void) {
	printf("Asterisk " ASTERISK_VERSION ", Copyright (C) 2000 - 2005, Digium.\n");
	printf("Usage: asterisk [OPTIONS]\n");
	printf("Valid Options:\n");
	printf("   -V              Display version number and exit\n");
	printf("   -C <configfile> Use an alternate configuration file\n");
	printf("   -G <group>      Run as a group other than the caller\n");
	printf("   -U <user>       Run as a user other than the caller\n");
	printf("   -c              Provide console CLI\n");
	printf("   -d              Enable extra debugging\n");
	printf("   -f              Do not fork\n");
	printf("   -g              Dump core in case of a crash\n");
	printf("   -h              This help screen\n");
	printf("   -i              Initialize crypto keys at startup\n");
	printf("   -n              Disable console colorization\n");
	printf("   -p              Run as pseudo-realtime thread\n");
	printf("   -q              Quiet mode (suppress output)\n");
	printf("   -r              Connect to Asterisk on this machine\n");
	printf("   -R              Connect to Asterisk, and attempt to reconnect if disconnected\n");
	printf("   -t              Record soundfiles in /var/tmp and move them where they belong after they are done.\n");
	printf("   -T              Display the time in [Mmm dd hh:mm:ss] format for each line of output to the CLI.\n");
	printf("   -v              Increase verbosity (multiple v's = more verbose)\n");
	printf("   -x <cmd>        Execute command <cmd> (only valid with -r)\n");
	printf("\n");
	return 0;
}

static void ast_readconfig(void) {
	struct ast_config *cfg;
	struct ast_variable *v;
	char *config = AST_CONFIG_FILE;

	if (option_overrideconfig == 1) {
		cfg = ast_config_load(ast_config_AST_CONFIG_FILE);
		if (!cfg)
			ast_log(LOG_WARNING, "Unable to open specified master config file '%s', using built-in defaults\n", ast_config_AST_CONFIG_FILE);
	} else {
		cfg = ast_config_load(config);
	}

	/* init with buildtime config */
	ast_copy_string(ast_config_AST_CONFIG_DIR, AST_CONFIG_DIR, sizeof(ast_config_AST_CONFIG_DIR));
	ast_copy_string(ast_config_AST_SPOOL_DIR, AST_SPOOL_DIR, sizeof(ast_config_AST_SPOOL_DIR));
	ast_copy_string(ast_config_AST_MODULE_DIR, AST_MODULE_DIR, sizeof(ast_config_AST_VAR_DIR));
 	snprintf(ast_config_AST_MONITOR_DIR, sizeof(ast_config_AST_MONITOR_DIR) - 1, "%s/monitor", ast_config_AST_SPOOL_DIR);
	ast_copy_string(ast_config_AST_VAR_DIR, AST_VAR_DIR, sizeof(ast_config_AST_VAR_DIR));
	ast_copy_string(ast_config_AST_LOG_DIR, AST_LOG_DIR, sizeof(ast_config_AST_LOG_DIR));
	ast_copy_string(ast_config_AST_AGI_DIR, AST_AGI_DIR, sizeof(ast_config_AST_AGI_DIR));
	ast_copy_string(ast_config_AST_DB, AST_DB, sizeof(ast_config_AST_DB));
	ast_copy_string(ast_config_AST_KEY_DIR, AST_KEY_DIR, sizeof(ast_config_AST_KEY_DIR));
	ast_copy_string(ast_config_AST_PID, AST_PID, sizeof(ast_config_AST_PID));
	ast_copy_string(ast_config_AST_SOCKET, AST_SOCKET, sizeof(ast_config_AST_SOCKET));
	ast_copy_string(ast_config_AST_RUN_DIR, AST_RUN_DIR, sizeof(ast_config_AST_RUN_DIR));

	/* no asterisk.conf? no problem, use buildtime config! */
	if (!cfg) {
		return;
	}
	v = ast_variable_browse(cfg, "files");
	while (v) {
		if (!strcasecmp(v->name, "astctlpermissions")) {
			ast_copy_string(ast_config_AST_CTL_PERMISSIONS, v->value, sizeof(ast_config_AST_CTL_PERMISSIONS));
		} else if (!strcasecmp(v->name, "astctlowner")) {
			ast_copy_string(ast_config_AST_CTL_OWNER, v->value, sizeof(ast_config_AST_CTL_OWNER));
		} else if (!strcasecmp(v->name, "astctlgroup")) {
			ast_copy_string(ast_config_AST_CTL_GROUP, v->value, sizeof(ast_config_AST_CTL_GROUP));
		} else if (!strcasecmp(v->name, "astctl")) {
			ast_copy_string(ast_config_AST_CTL, v->value, sizeof(ast_config_AST_CTL));
		}
		v = v->next;
	}
	v = ast_variable_browse(cfg, "directories");
	while(v) {
		if (!strcasecmp(v->name, "astetcdir")) {
			ast_copy_string(ast_config_AST_CONFIG_DIR, v->value, sizeof(ast_config_AST_CONFIG_DIR));
		} else if (!strcasecmp(v->name, "astspooldir")) {
			ast_copy_string(ast_config_AST_SPOOL_DIR, v->value, sizeof(ast_config_AST_SPOOL_DIR));
			snprintf(ast_config_AST_MONITOR_DIR, sizeof(ast_config_AST_MONITOR_DIR) - 1, "%s/monitor", v->value);
		} else if (!strcasecmp(v->name, "astvarlibdir")) {
			ast_copy_string(ast_config_AST_VAR_DIR, v->value, sizeof(ast_config_AST_VAR_DIR));
			snprintf(ast_config_AST_DB, sizeof(ast_config_AST_DB), "%s/%s", v->value, "astdb");    
		} else if (!strcasecmp(v->name, "astlogdir")) {
			ast_copy_string(ast_config_AST_LOG_DIR, v->value, sizeof(ast_config_AST_LOG_DIR));
		} else if (!strcasecmp(v->name, "astagidir")) {
			ast_copy_string(ast_config_AST_AGI_DIR, v->value, sizeof(ast_config_AST_AGI_DIR));
		} else if (!strcasecmp(v->name, "astrundir")) {
			snprintf(ast_config_AST_PID, sizeof(ast_config_AST_PID), "%s/%s", v->value, "asterisk.pid");
			snprintf(ast_config_AST_SOCKET, sizeof(ast_config_AST_SOCKET), "%s/%s", v->value, ast_config_AST_CTL);
			ast_copy_string(ast_config_AST_RUN_DIR, v->value, sizeof(ast_config_AST_RUN_DIR));
		} else if (!strcasecmp(v->name, "astmoddir")) {
			ast_copy_string(ast_config_AST_MODULE_DIR, v->value, sizeof(ast_config_AST_MODULE_DIR));
		}
		v = v->next;
	}
	v = ast_variable_browse(cfg, "options");
	while(v) {
		/* verbose level (-v at startup) */
		if (!strcasecmp(v->name, "verbose")) {
			option_verbose = atoi(v->value);
		/* whether or not to force timestamping. (-T at startup) */
		} else if (!strcasecmp(v->name, "timestamp")) {
			option_timestamp = ast_true(v->value);
		/* whether or not to support #exec in config files */
		} else if (!strcasecmp(v->name, "execincludes")) {
			option_exec_includes = ast_true(v->value);
		/* debug level (-d at startup) */
		} else if (!strcasecmp(v->name, "debug")) {
			option_debug = 0;
			if (sscanf(v->value, "%d", &option_debug) != 1) {
				option_debug = ast_true(v->value);
			}
		/* Disable forking (-f at startup) */
		} else if (!strcasecmp(v->name, "nofork")) {
			option_nofork = ast_true(v->value);
		/* Run quietly (-q at startup ) */
		} else if (!strcasecmp(v->name, "quiet")) {
			option_quiet = ast_true(v->value);
		/* Run as console (-c at startup, implies nofork) */
		} else if (!strcasecmp(v->name, "console")) {
			option_console = ast_true(v->value);
		/* Run with highg priority if the O/S permits (-p at startup) */
		} else if (!strcasecmp(v->name, "highpriority")) {
			option_highpriority = ast_true(v->value);
		/* Initialize RSA auth keys (IAX2) (-i at startup) */
		} else if (!strcasecmp(v->name, "initcrypto")) {
			option_initcrypto = ast_true(v->value);
		/* Disable ANSI colors for console (-c at startup) */
		} else if (!strcasecmp(v->name, "nocolor")) {
			option_nocolor = ast_true(v->value);
		/* Disable some usage warnings for picky people :p */
		} else if (!strcasecmp(v->name, "dontwarn")) {
			option_dontwarn = ast_true(v->value);
		/* Dump core in case of crash (-g) */
		} else if (!strcasecmp(v->name, "dumpcore")) {
			option_dumpcore = ast_true(v->value);
		/* Cache recorded sound files to another directory during recording */
		} else if (!strcasecmp(v->name, "cache_record_files")) {
			option_cache_record_files = ast_true(v->value);
		/* Specify cache directory */
		}  else if (!strcasecmp(v->name, "record_cache_dir")) {
			ast_copy_string(record_cache_dir, v->value, AST_CACHE_DIR_LEN);
		/* Build transcode paths via SLINEAR, instead of directly */
		} else if (!strcasecmp(v->name, "transcode_via_sln")) {
			option_transcode_slin = ast_true(v->value);
		/* Transmit SLINEAR silence while a channel is being recorded */
		} else if (!strcasecmp(v->name, "transmit_silence_during_record")) {
			option_transmit_silence_during_record = ast_true(v->value);
		} else if (!strcasecmp(v->name, "maxcalls")) {
			if ((sscanf(v->value, "%d", &option_maxcalls) != 1) || (option_maxcalls < 0)) {
				option_maxcalls = 0;
			}
		} else if (!strcasecmp(v->name, "maxload")) {
			double test[1];

			if (getloadavg(test, 1) == -1) {
				ast_log(LOG_ERROR, "Cannot obtain load average on this system. 'maxload' option disabled.\n");
				option_maxload = 0.0;
			} else if ((sscanf(v->value, "%lf", &option_maxload) != 1) || (option_maxload < 0.0)) {
				option_maxload = 0.0;
			}
		}
		v = v->next;
	}
	ast_config_destroy(cfg);
}

int main(int argc, char *argv[])
{
	int c;
	char filename[80] = "";
	char hostname[MAXHOSTNAMELEN]="";
	char tmp[80];
	char * xarg = NULL;
	int x;
	FILE *f;
	sigset_t sigs;
	int num;
	int is_child_of_nonroot=0;
	char *buf;
	char *runuser=NULL, *rungroup=NULL;

	/* Remember original args for restart */
	if (argc > sizeof(_argv) / sizeof(_argv[0]) - 1) {
		fprintf(stderr, "Truncating argument size to %d\n", (int)(sizeof(_argv) / sizeof(_argv[0])) - 1);
		argc = sizeof(_argv) / sizeof(_argv[0]) - 1;
	}
	for (x=0;x<argc;x++)
		_argv[x] = argv[x];
	_argv[x] = NULL;

	/* if the progname is rasterisk consider it a remote console */
	if (argv[0] && (strstr(argv[0], "rasterisk")) != NULL) {
		option_remote++;
		option_nofork++;
	}
	if (gethostname(hostname, sizeof(hostname)-1))
		ast_copy_string(hostname, "<Unknown>", sizeof(hostname));
	ast_mainpid = getpid();
	ast_ulaw_init();
	ast_alaw_init();
	callerid_init();
	ast_utils_init();
	tdd_init();
	/* When Asterisk restarts after it has dropped the root privileges,
	 * it can't issue setuid(), setgid(), setgroups() or set_priority() 
	 * */
	if (getenv("ASTERISK_ALREADY_NONROOT"))
		is_child_of_nonroot=1;
	if (getenv("HOME")) 
		snprintf(filename, sizeof(filename), "%s/.asterisk_history", getenv("HOME"));
	/* Check if we're root */
	/*
	if (geteuid()) {
		ast_log(LOG_ERROR, "Must be run as root\n");
		exit(1);
	}
	*/
	/* Check for options */
	while((c=getopt(argc, argv, "tThfdvVqprRgcinx:U:G:C:L:M:")) != -1) {
		switch(c) {
		case 'd':
			option_debug++;
			option_nofork++;
			break;
		case 'c':
			option_console++;
			option_nofork++;
			break;
		case 'f':
			option_nofork++;
			break;
		case 'n':
			option_nocolor++;
			break;
		case 'r':
			option_remote++;
			option_nofork++;
			break;
		case 'R':
			option_remote++;
			option_nofork++;
			option_reconnect++;
			break;
		case 'p':
			option_highpriority++;
			break;
		case 'v':
			option_verbose++;
			option_nofork++;
			break;
		case 'M':
			if ((sscanf(optarg, "%d", &option_maxcalls) != 1) || (option_maxcalls < 0))
				option_maxcalls = 0;
			break;
		case 'L':
			if ((sscanf(optarg, "%lf", &option_maxload) != 1) || (option_maxload < 0.0))
				option_maxload = 0.0;
			break;
		case 'q':
			option_quiet++;
			break;
		case 't':
			option_cache_record_files++;
			break;
		case 'T':
			option_timestamp++;
			break;
		case 'x':
			option_exec++;
			xarg = optarg;
			break;
		case 'C':
			ast_copy_string((char *)ast_config_AST_CONFIG_FILE,optarg,sizeof(ast_config_AST_CONFIG_FILE));
			option_overrideconfig++;
			break;
		case 'i':
			option_initcrypto++;
			break;
		case'g':
			option_dumpcore++;
			break;
		case 'h':
			show_cli_help();
			exit(0);
		case 'V':
			show_version();
			exit(0);
		case 'U':
			runuser = optarg;
			break;
		case 'G':
			rungroup = optarg;
			break;
		case '?':
			exit(1);
		}
	}

	/* For remote connections, change the name of the remote connection.
	 * We do this for the benefit of init scripts (which need to know if/when
	 * the main asterisk process has died yet). */
	if (option_remote) {
		strcpy(argv[0], "rasterisk");
		for (x = 1; x < argc; x++) {
			argv[x] = argv[0] + 10;
		}
	}

	if (option_dumpcore) {
		struct rlimit l;
		memset(&l, 0, sizeof(l));
		l.rlim_cur = RLIM_INFINITY;
		l.rlim_max = RLIM_INFINITY;
		if (setrlimit(RLIMIT_CORE, &l)) {
			ast_log(LOG_WARNING, "Unable to disable core size resource limit: %s\n", strerror(errno));
		}
	}

	if (option_console && !option_verbose) 
		ast_verbose("[ Reading Master Configuration ]");
	ast_readconfig();

	if (!is_child_of_nonroot && ast_set_priority(option_highpriority)) {
		exit(1);
	}

	if (!is_child_of_nonroot && rungroup) {
		struct group *gr;
		gr = getgrnam(rungroup);
		if (!gr) {
			ast_log(LOG_WARNING, "No such group '%s'!\n", rungroup);
			exit(1);
		}
		if (setgid(gr->gr_gid)) {
			ast_log(LOG_WARNING, "Unable to setgid to %d (%s)\n", gr->gr_gid, rungroup);
			exit(1);
		}
		if (option_verbose)
			ast_verbose("Running as group '%s'\n", rungroup);
	}

	if (!is_child_of_nonroot && runuser) {
		struct passwd *pw;
		pw = getpwnam(runuser);
		if (!pw) {
			ast_log(LOG_WARNING, "No such user '%s'!\n", runuser);
			exit(1);
		}
		if (setuid(pw->pw_uid)) {
			ast_log(LOG_WARNING, "Unable to setuid to %d (%s)\n", pw->pw_uid, runuser);
			exit(1);
		}
		setenv("ASTERISK_ALREADY_NONROOT","yes",1);
		if (option_verbose)
			ast_verbose("Running as user '%s'\n", runuser);
	}

	term_init();
	printf(term_end());
	fflush(stdout);

	if (option_console && !option_verbose) 
		ast_verbose("[ Initializing Custom Configuration Options ]");
	/* custom config setup */
	register_config_cli();
	read_config_maps();
	

	if (option_console) {
		if (el_hist == NULL || el == NULL)
			ast_el_initialize();

		if (!ast_strlen_zero(filename))
			ast_el_read_history(filename);
	}

	if (ast_tryconnect()) {
		/* One is already running */
		if (option_remote) {
			if (option_exec) {
				ast_remotecontrol(xarg);
				quit_handler(0, 0, 0, 0);
				exit(0);
			}
			printf(term_quit());
			ast_register_verbose(console_verboser);
			WELCOME_MESSAGE;
			ast_remotecontrol(NULL);
			quit_handler(0, 0, 0, 0);
			exit(0);
		} else {
			ast_log(LOG_ERROR, "Asterisk already running on %s.  Use 'asterisk -r' to connect.\n", (char *)ast_config_AST_SOCKET);
			printf(term_quit());
			exit(1);
		}
	} else if (option_remote || option_exec) {
		ast_log(LOG_ERROR, "Unable to connect to remote asterisk (does %s exist?)\n",ast_config_AST_SOCKET);
		printf(term_quit());
		exit(1);
	}
	/* Blindly write pid file since we couldn't connect */
	unlink((char *)ast_config_AST_PID);
	f = fopen((char *)ast_config_AST_PID, "w");
	if (f) {
		fprintf(f, "%d\n", getpid());
		fclose(f);
	} else
		ast_log(LOG_WARNING, "Unable to open pid file '%s': %s\n", (char *)ast_config_AST_PID, strerror(errno));

	if (!option_verbose && !option_debug && !option_nofork && !option_console) {
		daemon(0,0);
		/* Blindly re-write pid file since we are forking */
		unlink((char *)ast_config_AST_PID);
		f = fopen((char *)ast_config_AST_PID, "w");
		if (f) {
			fprintf(f, "%d\n", getpid());
			fclose(f);
		} else
			ast_log(LOG_WARNING, "Unable to open pid file '%s': %s\n", (char *)ast_config_AST_PID, strerror(errno));
	}

	/* Test recursive mutex locking. */
	if (test_for_thread_safety())
		ast_verbose("Warning! Asterisk is not thread safe.\n");

	ast_makesocket();
	sigemptyset(&sigs);
	sigaddset(&sigs, SIGHUP);
	sigaddset(&sigs, SIGTERM);
	sigaddset(&sigs, SIGINT);
	sigaddset(&sigs, SIGPIPE);
	sigaddset(&sigs, SIGWINCH);
	pthread_sigmask(SIG_BLOCK, &sigs, NULL);
	if (option_console || option_verbose || option_remote)
		ast_register_verbose(console_verboser);
	/* Print a welcome message if desired */
	if (option_verbose || option_console) {
		WELCOME_MESSAGE;
	}
	if (option_console && !option_verbose) 
		ast_verbose("[ Booting...");

	signal(SIGURG, urg_handler);
	signal(SIGINT, __quit_handler);
	signal(SIGTERM, __quit_handler);
	signal(SIGHUP, hup_handler);
	signal(SIGCHLD, child_handler);
	signal(SIGPIPE, SIG_IGN);

	/* ensure that the random number generators are seeded with a different value every time
	   Asterisk is started
	*/
	srand((unsigned int) getpid() + (unsigned int) time(NULL));
	srandom((unsigned int) getpid() + (unsigned int) time(NULL));

	if (init_logger()) {
		printf(term_quit());
		exit(1);
	}
	if (dnsmgr_init()) {
		printf(term_quit());
		exit(1);
	}
	/* load 'preload' modules, required for access to Realtime-mapped configuration files */
	if (load_modules(1)) {
		printf(term_quit());
		exit(1);
	}
	ast_channels_init();
	if (init_manager()) {
		printf(term_quit());
		exit(1);
	}
	if (ast_cdr_engine_init()) {
		printf(term_quit());
		exit(1);
	}
	if (ast_device_state_engine_init()) {
		printf(term_quit());
		exit(1);
	}
	ast_rtp_init();
	if (ast_image_init()) {
		printf(term_quit());
		exit(1);
	}
	if (ast_file_init()) {
		printf(term_quit());
		exit(1);
	}
	if (load_pbx()) {
		printf(term_quit());
		exit(1);
	}
	if (load_modules(0)) {
		printf(term_quit());
		exit(1);
	}
	if (init_framer()) {
		printf(term_quit());
		exit(1);
	}
	if (astdb_init()) {
		printf(term_quit());
		exit(1);
	}
	if (ast_enum_init()) {
		printf(term_quit());
		exit(1);
	}
#if 0
	/* This should no longer be necessary */
	/* sync cust config and reload some internals in case a custom config handler binded to them */
	read_ast_cust_config();
	reload_logger(0);
	reload_manager();
	ast_enum_reload();
	ast_rtp_reload();
#endif


	/* We might have the option of showing a console, but for now just
	   do nothing... */
	if (option_console && !option_verbose)
		ast_verbose(" ]\n");
	if (option_verbose || option_console)
		ast_verbose(term_color(tmp, "Asterisk Ready.\n", COLOR_BRWHITE, COLOR_BLACK, sizeof(tmp)));
	if (option_nofork)
		consolethread = pthread_self();
	fully_booted = 1;
	pthread_sigmask(SIG_UNBLOCK, &sigs, NULL);
#ifdef __AST_DEBUG_MALLOC
	__ast_mm_init();
#endif	
	time(&ast_startuptime);
	ast_cli_register_multiple(core_cli, sizeof(core_cli) / sizeof(core_cli[0]));
	if (option_console) {
		/* Console stuff now... */
		/* Register our quit function */
		char title[256];
		set_icon("Asterisk");
		snprintf(title, sizeof(title), "Asterisk Console on '%s' (pid %d)", hostname, ast_mainpid);
		set_title(title);

		for (;;) {
			buf = (char *)el_gets(el, &num);
			if (buf) {
				if (buf[strlen(buf)-1] == '\n')
					buf[strlen(buf)-1] = '\0';

				consolehandler((char *)buf);
			} else {
				if (write(STDOUT_FILENO, "\nUse EXIT or QUIT to exit the asterisk console\n",
								  strlen("\nUse EXIT or QUIT to exit the asterisk console\n")) < 0) {
					/* Whoa, stdout disappeared from under us... Make /dev/null's */
					int fd;
					fd = open("/dev/null", O_RDWR);
					if (fd > -1) {
						dup2(fd, STDOUT_FILENO);
						dup2(fd, STDIN_FILENO);
					} else
						ast_log(LOG_WARNING, "Failed to open /dev/null to recover from dead console.  Bad things will happen!\n");
					break;
				}
			}
		}

	}
	/* Do nothing */
	for(;;)  {	/* apparently needed for the MACos */
		struct pollfd p = { -1 /* no descriptor */, 0, 0 };
		poll(&p, 0, -1);
	}
	return 0;
}
