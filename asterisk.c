/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Top level source file for asterisk
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <unistd.h>
#include <stdlib.h>
#include <asterisk/logger.h>
#include <asterisk/options.h>
#include <asterisk/cli.h>
#include <asterisk/channel.h>
#include <asterisk/ulaw.h>
#include <asterisk/alaw.h>
#include <asterisk/callerid.h>
#include <asterisk/module.h>
#include <asterisk/image.h>
#include <asterisk/tdd.h>
#include <fcntl.h>
#include <stdio.h>
#include <signal.h>
#include <sched.h>
#include <asterisk/io.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <string.h>
#include <errno.h>
#include <readline/readline.h>
#include <readline/history.h>
#include "asterisk.h"

#define AST_MAX_CONNECTS 128
#define NUM_MSGS 64

int option_verbose=0;
int option_debug=0;
int option_nofork=0;
int option_quiet=0;
int option_console=0;
int option_highpriority=0;
int option_remote=0;
int option_exec=0;
int option_initcrypto=0;
int fully_booted = 0;

static int ast_socket = -1;		/* UNIX Socket for allowing remote control */
static int ast_consock = -1;		/* UNIX Socket for controlling another asterisk */
static int mainpid;
struct console {
	int fd;					/* File descriptor */
	int p[2];				/* Pipe */
	pthread_t t;			/* Thread of handler */
};

struct console consoles[AST_MAX_CONNECTS];

char defaultlanguage[MAX_LANGUAGE] = DEFAULT_LANGUAGE;

static int fdprint(int fd, char *s)
{
	return write(fd, s, strlen(s) + 1);
}

static void network_verboser(char *s, int pos, int replace, int complete)
{
	int x;
	for (x=0;x<AST_MAX_CONNECTS; x++) {
		if (consoles[x].fd > -1) 
			fdprint(consoles[x].p[1], s);
	}
}

static pthread_t lthread;

static void *netconsole(void *vconsole)
{
	struct console *con = vconsole;
	char hostname[256];
	char tmp[512];
	int res;
	int max;
	fd_set rfds;
	
	if (gethostname(hostname, sizeof(hostname)))
		strncpy(hostname, "<Unknown>", sizeof(hostname)-1);
	snprintf(tmp, sizeof(tmp), "%s/%d/%s\n", hostname, mainpid, ASTERISK_VERSION);
	fdprint(con->fd, tmp);
	for(;;) {
		FD_ZERO(&rfds);	
		FD_SET(con->fd, &rfds);
		FD_SET(con->p[0], &rfds);
		max = con->fd;
		if (con->p[0] > max)
			max = con->p[0];
		res = select(max + 1, &rfds, NULL, NULL, NULL);
		if (res < 0) {
			ast_log(LOG_WARNING, "select returned < 0: %s\n", strerror(errno));
			continue;
		}
		if (FD_ISSET(con->fd, &rfds)) {
			res = read(con->fd, tmp, sizeof(tmp));
			if (res < 1) {
				break;
			}
			tmp[res] = 0;
			ast_cli_command(con->fd, tmp);
		}
		if (FD_ISSET(con->p[0], &rfds)) {
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
	struct sockaddr_un sun;
	int s;
	int len;
	int x;
	int flags;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	for(;;) {
		len = sizeof(sun);
		s = accept(ast_socket, (struct sockaddr *)&sun, &len);
		if (s < 0) {
			ast_log(LOG_WARNING, "Accept retured %d: %s\n", s, strerror(errno));
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
					if (pthread_create(&consoles[x].t, &attr, netconsole, &consoles[x])) {
						ast_log(LOG_ERROR, "Unable to spawn thread to handle connection\n");
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
	struct sockaddr_un sun;
	int res;
	int x;
	for (x=0;x<AST_MAX_CONNECTS;x++)	
		consoles[x].fd = -1;
	unlink(AST_SOCKET);
	ast_socket = socket(PF_LOCAL, SOCK_STREAM, 0);
	if (ast_socket < 0) {
		ast_log(LOG_WARNING, "Unable to create control socket: %s\n", strerror(errno));
		return -1;
	}		
	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_LOCAL;
	strncpy(sun.sun_path, AST_SOCKET, sizeof(sun.sun_path)-1);
	res = bind(ast_socket, (struct sockaddr *)&sun, sizeof(sun));
	if (res) {
		ast_log(LOG_WARNING, "Unable to bind socket to %s: %s\n", AST_SOCKET, strerror(errno));
		close(ast_socket);
		ast_socket = -1;
		return -1;
	}
	res = listen(ast_socket, 2);
	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to listen on socket %s: %s\n", AST_SOCKET, strerror(errno));
		close(ast_socket);
		ast_socket = -1;
		return -1;
	}
	ast_register_verbose(network_verboser);
	pthread_create(&lthread, NULL, listener, NULL);
	return 0;
}

static int ast_tryconnect(void)
{
	struct sockaddr_un sun;
	int res;
	ast_consock = socket(PF_LOCAL, SOCK_STREAM, 0);
	if (ast_consock < 0) {
		ast_log(LOG_WARNING, "Unable to create socket: %s\n", strerror(errno));
		return 0;
	}
	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_LOCAL;
	strncpy(sun.sun_path, AST_SOCKET, sizeof(sun.sun_path)-1);
	res = connect(ast_consock, (struct sockaddr *)&sun, sizeof(sun));
	if (res) {
		close(ast_consock);
		ast_consock = -1;
		return 0;
	} else
		return 1;
}

static void urg_handler(int num)
{
	/* Called by soft_hangup to interrupt the select, read, or other
	   system call.  We don't actually need to do anything though.  */
	if (option_debug) 
		ast_log(LOG_DEBUG, "Urgent handler\n");
	signal(num, urg_handler);
	return;
}

static void hup_handler(int num)
{
	if (option_verbose > 1) 
		ast_verbose(VERBOSE_PREFIX_2 "Received HUP signal -- Reloading configs\n");
	ast_module_reload();
}


static void pipe_handler(int num)
{
	/* Ignore sigpipe */
}
static void set_title(char *text)
{
	/* Set an X-term or screen title */
	if (getenv("TERM") && strstr(getenv("TERM"), "xterm"))
		fprintf(stdout, "\033]2;%s\007", text);
}

static void set_icon(char *text)
{
	if (getenv("TERM") && strstr(getenv("TERM"), "xterm"))
		fprintf(stdout, "\033]1;%s\007", text);
}

static int set_priority(int pri)
{
	struct sched_param sched;
	memset(&sched, 0, sizeof(sched));
	/* We set ourselves to a high priority, that we might pre-empt everything
	   else.  If your PBX has heavy activity on it, this is a good thing.  */
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
	return 0;
}

static void quit_handler(int num)
{
	char filename[80] = "";
	if (option_console || option_remote) {
		if (getenv("HOME")) 
			snprintf(filename, sizeof(filename), "%s/.asterisk_history", getenv("HOME"));
		if (strlen(filename))
			write_history(filename);
		rl_callback_handler_remove();
	}
	/* Called on exit */
	if (option_verbose && option_console)
		ast_verbose("Asterisk ending (%d).\n", num);
	else if (option_debug)
		ast_log(LOG_DEBUG, "Asterisk ending (%d).\n", num);
	if (ast_socket > -1)
		close(ast_socket);
	if (ast_consock > -1)
		close(ast_consock);
	if (ast_socket > -1)
		unlink(AST_SOCKET);
	
	exit(0);
}

static pthread_t consolethread = -1;

static void console_verboser(char *s, int pos, int replace, int complete)
{
	/* Return to the beginning of the line */
	if (!pos)
		fprintf(stdout, "\r");
	fputs(s + pos,stdout);
	fflush(stdout);
	if (complete)
	/* Wake up a select()ing console */
		pthread_kill(consolethread, SIGURG);
}

static void consolehandler(char *s)
{
	/* Called when readline data is available */
	if (s && strlen(s))
		add_history(s);
	/* Give the console access to the shell */
	if (s) {
		if (s[0] == '!') {
			if (s[1])
				system(s+1);
			else
				system(getenv("SHELL") ? getenv("SHELL") : "/bin/sh");
		} else 
		ast_cli_command(STDOUT_FILENO, s);
		if (!strcasecmp(s, "help"))
			fprintf(stdout, "          !<command>   Executes a given shell command\n");
	} else
		fprintf(stdout, "\nUse \"quit\" to exit\n");
}


static char cmd[1024];

static void remoteconsolehandler(char *s)
{
	/* Called when readline data is available */
	if (s && strlen(s))
		add_history(s);
	/* Give the console access to the shell */
	if (s) {
		if (s[0] == '!') {
			if (s[1])
				system(s+1);
			else
				system(getenv("SHELL") ? getenv("SHELL") : "/bin/sh");
		} else 
		strncpy(cmd, s, sizeof(cmd)-1);
		if (!strcasecmp(s, "help"))
			fprintf(stdout, "          !<command>   Executes a given shell command\n");
		if (!strcasecmp(s, "quit"))
			quit_handler(0);
	} else
		fprintf(stdout, "\nUse \"quit\" to exit\n");
}

static char quit_help[] = 
"Usage: quit\n"
"       Exits Asterisk.\n";

static char shutdown_help[] = 
"Usage: shutdown\n"
"       Shuts down a running Asterisk PBX.\n";

static int handle_quit(int fd, int argc, char *argv[])
{
	if (argc != 1)
		return RESULT_SHOWUSAGE;
	quit_handler(0);
	return RESULT_SUCCESS;
}

#define ASTERISK_PROMPT "*CLI> "

#define ASTERISK_PROMPT2 "%s*CLI> "

static struct ast_cli_entry quit = 	{ { "quit", NULL }, handle_quit, "Exit Asterisk", quit_help };

static struct ast_cli_entry astshutdown = 	{ { "shutdown", NULL }, handle_quit, "Shut down an Asterisk PBX", shutdown_help };

static char *cli_generator(char *text, int state)
{
	return ast_cli_generator(rl_line_buffer, text, state);
}

static char *console_cli_generator(char *text, int state)
{
	char buf[1024];
	int res;
#if 0
	fprintf(stderr, "Searching for '%s', %s %d\n", rl_line_buffer, text, state);
#endif	
	snprintf(buf, sizeof(buf),"_COMMAND COMPLETE \"%s\" \"%s\" %d", rl_line_buffer, text, state); 
	fdprint(ast_consock, buf);
	res = read(ast_consock, buf, sizeof(buf));
	buf[res] = '\0';
#if 0
	printf("res is %d, buf is '%s'\n", res, buf);
#endif	
	if (strncmp(buf, "NULL", 4))
		return strdup(buf);
	else
		return NULL;
}

static void ast_remotecontrol(char * data)
{
	char buf[80];
	int res;
	int max;
	int lastpos = 0;
	fd_set rfds;
	char filename[80] = "";
	char *hostname;
	char *cpid;
	char *version;
	int pid;
	int lastclear=0;
	int oldstatus=0;
	char tmp[80];
	read(ast_consock, buf, sizeof(buf));
	if (data) {
			write(ast_consock, data, strlen(data) + 1);
			return;
	}
	hostname = strtok(buf, "/");
	cpid = strtok(NULL, "/");
	version = strtok(NULL, "/");
	if (!version)
		version = "<Version Unknown>";
	strtok(hostname, ".");
	if (cpid)
		pid = atoi(cpid);
	else
		pid = -1;
	snprintf(tmp, sizeof(tmp), "set verbose atleast %d", option_verbose);
	fdprint(ast_consock, tmp);
	ast_verbose("Connected to Asterisk %s currently running on %s (pid = %d)\n", version, hostname, pid);
	snprintf(tmp, sizeof(tmp), ASTERISK_PROMPT2, hostname);
	if (getenv("HOME")) 
		snprintf(filename, sizeof(filename), "%s/.asterisk_history", getenv("HOME"));
	if (strlen(filename))
		read_history(filename);
    ast_cli_register(&quit);
    ast_cli_register(&astshutdown);
	rl_callback_handler_install(tmp, remoteconsolehandler);
	rl_completion_entry_function = (Function *)console_cli_generator;
	for(;;) {
		FD_ZERO(&rfds);
		FD_SET(ast_consock, &rfds);
		FD_SET(STDIN_FILENO, &rfds);
		max = ast_consock;
		if (STDIN_FILENO > max)
			max = STDIN_FILENO;
		res = select(max + 1, &rfds, NULL, NULL, NULL);
		if (res < 0) {
			if (errno == EINTR)
				continue;
			ast_log(LOG_ERROR, "select failed: %s\n", strerror(errno));
			break;
		}
		if (FD_ISSET(STDIN_FILENO, &rfds)) {
			rl_callback_read_char();
			if (strlen(cmd)) {
				res = write(ast_consock, cmd, strlen(cmd) + 1);
				if (res < 1) {
					ast_log(LOG_WARNING, "Unable to write: %s\n", strerror(errno));
					break;
				}
				strcpy(cmd, "");
			}
		}
		if (FD_ISSET(ast_consock, &rfds)) {
			res = read(ast_consock, buf, sizeof(buf));
			if (res < 1)
				break;
			buf[res] = 0;
			/* If someone asks for a pass code, hide the password */
			if (!memcmp(buf, ">>>>", 4)) {
				printf("Ooh, i should hide password!\n");
				if (!lastclear) {
					oldstatus = ast_hide_password(STDIN_FILENO);
					printf("Oldstatus = %d\n", oldstatus);
				}
				lastclear = 1;
			} else if (lastclear) {
				ast_restore_tty(STDIN_FILENO, oldstatus);
				lastclear = 0;
			}
			if (!lastpos)
				write(STDOUT_FILENO, "\r", 2);
			write(STDOUT_FILENO, buf, res);
			if ((buf[res-1] == '\n') || (buf[res-2] == '\n')) {
				rl_forced_update_display();
				lastpos = 0;
			} else {
				lastpos = 1;
			}
		}
	}
	printf("\nDisconnected from Asterisk server\n");
}

int main(int argc, char *argv[])
{
	char c;
	fd_set rfds;
	int res;
	int pid;
	char filename[80] = "";
	char hostname[256];
	char * xarg = NULL;
	sigset_t sigs;

	if (gethostname(hostname, sizeof(hostname)))
		strncpy(hostname, "<Unknown>", sizeof(hostname)-1);
	mainpid = getpid();
	ast_ulaw_init();
	ast_alaw_init();
	callerid_init();
	tdd_init();
	if (getenv("HOME")) 
		snprintf(filename, sizeof(filename), "%s/.asterisk_history", getenv("HOME"));
	/* Check if we're root */
	if (geteuid()) {
		ast_log(LOG_ERROR, "Must be run as root\n");
		exit(1);
	}
	/* Check for options */
	while((c=getopt(argc, argv, "fdvqprcix:")) != EOF) {
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
		case 'r':
			option_remote++;
			option_nofork++;
			break;
		case 'p':
			option_highpriority++;
			break;
		case 'v':
			option_verbose++;
			option_nofork++;
			break;
		case 'q':
			option_quiet++;
			break;
		case 'x':
			option_exec++;
			xarg = optarg;
			break;
		case 'i':
			option_initcrypto++;
			break;
		case '?':
			exit(1);
		}
	}
	
	if (ast_tryconnect()) {
		/* One is already running */
		if (option_remote) {
			if (option_exec) {
				ast_remotecontrol(xarg);
				quit_handler(0);
				exit(0);
			}
			ast_register_verbose(console_verboser);
			ast_verbose( "Asterisk " ASTERISK_VERSION ", Copyright (C) 1999-2001 Linux Support Services, Inc.\n");
			ast_verbose( "Written by Mark Spencer <markster@linux-support.net>\n");
			ast_verbose( "=========================================================================\n");
			ast_remotecontrol(NULL);
			quit_handler(0);
			exit(0);
		} else {
			ast_log(LOG_ERROR, "Asterisk already running on %s.  Use 'asterisk -r' to connect.\n", AST_SOCKET);
			exit(1);
		}
	} else if (option_remote || option_exec) {
		ast_log(LOG_ERROR, "Unable to connect to remote asterisk\n");
		exit(1);
	}

	if (!option_verbose && !option_debug && !option_nofork && !option_console) {
		pid = fork();
		if (pid < 0) {
			ast_log(LOG_ERROR, "Unable to fork(): %s\n", strerror(errno));
			exit(1);
		}
		if (pid) 
			exit(0);
	}

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
		ast_verbose( "Asterisk " ASTERISK_VERSION ", Copyright (C) 1999-2001 Linux Support Services, Inc.\n");
		ast_verbose( "Written by Mark Spencer <markster@linux-support.net>\n");
		ast_verbose( "=========================================================================\n");
	}
	if (option_console && !option_verbose) 
		ast_verbose("[ Booting...");
	signal(SIGURG, urg_handler);
	signal(SIGINT, quit_handler);
	signal(SIGTERM, quit_handler);
	signal(SIGHUP, hup_handler);
	signal(SIGPIPE, pipe_handler);
	if (set_priority(option_highpriority))
		exit(1);
	if (init_logger())
		exit(1);
	if (ast_image_init())
		exit(1);
	if (load_pbx())
		exit(1);
	if (load_modules())
		exit(1);
	if (init_framer())
		exit(1);
	/* We might have the option of showing a console, but for now just
	   do nothing... */
	if (option_console && !option_verbose)
		ast_verbose(" ]\n");
	if (option_verbose || option_console)
		ast_verbose( "Asterisk Ready.\n");
	fully_booted = 1;
	pthread_sigmask(SIG_UNBLOCK, &sigs, NULL);
	ast_cli_register(&astshutdown);
	if (option_console) {
		/* Console stuff now... */
		/* Register our quit function */
		char title[256];
		set_icon("Asterisk");
		snprintf(title, sizeof(title), "Asterisk Console on '%s' (pid %d)", hostname, mainpid);
		set_title(title);
	    ast_cli_register(&quit);
		consolethread = pthread_self();
		if (strlen(filename))
			read_history(filename);
		rl_callback_handler_install(ASTERISK_PROMPT, consolehandler);
		rl_completion_entry_function = (Function *)cli_generator;
		for(;;) {
			FD_ZERO(&rfds);
			FD_SET(STDIN_FILENO, &rfds);
			res = select(STDIN_FILENO + 1, &rfds, NULL, NULL, NULL);
			if (res > 0) {
				rl_callback_read_char();
			} else if (res < 1) {
				rl_forced_update_display();
			}
	
		}	
	} else {
 		/* Do nothing */
		select(0,NULL,NULL,NULL,NULL);
	}
	return 0;
}
