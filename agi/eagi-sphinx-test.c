/*
 * Extended AGI test application
 *
 * This code is released into public domain
 * without any warranty of any kind.
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/select.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#ifdef SOLARIS
#include <solaris-compat/compat.h>
#endif

#define AUDIO_FILENO (STDERR_FILENO + 1)

#define SPHINX_HOST "192.168.1.108"
#define SPHINX_PORT 3460

static int sphinx_sock = -1;

static int connect_sphinx(void)
{
	struct hostent *hp;
	struct sockaddr_in sin;
	int res;
	hp = gethostbyname(SPHINX_HOST);
	if (!hp) {
		fprintf(stderr, "Unable to resolve '%s'\n", SPHINX_HOST);
		return -1;
	}
	sphinx_sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sphinx_sock < 0) {
		fprintf(stderr, "Unable to allocate socket: %s\n", strerror(errno));
		return -1;
	}
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(SPHINX_PORT);
	memcpy(&sin.sin_addr, hp->h_addr, sizeof(sin.sin_addr));
	if (connect(sphinx_sock, (struct sockaddr *)&sin, sizeof(sin))) {
		fprintf(stderr, "Unable to connect on socket: %s\n", strerror(errno));
		close(sphinx_sock);
		sphinx_sock = -1;
		return -1;
	}
	res = fcntl(sphinx_sock, F_GETFL);
	if ((res < 0) || (fcntl(sphinx_sock, F_SETFL, res | O_NONBLOCK) < 0)) {
		fprintf(stderr, "Unable to set flags on socket: %s\n", strerror(errno));
		close(sphinx_sock);
		sphinx_sock = -1;
		return -1;
	}
	return 0;
}

static int read_environment(void)
{
	char buf[256];
	char *val;
	/* Read environment */
	for(;;) {
		fgets(buf, sizeof(buf), stdin);
		if (feof(stdin))
			return -1;
		buf[strlen(buf) - 1] = '\0';
		/* Check for end of environment */
		if (!strlen(buf))
			return 0;
		val = strchr(buf, ':');
		if (!val) {
			fprintf(stderr, "Invalid environment: '%s'\n", buf);
			return -1;
		}
		*val = '\0';
		val++;
		val++;
		/* Skip space */
		fprintf(stderr, "Environment: '%s' is '%s'\n", buf, val);

		/* Load into normal environment */
		setenv(buf, val, 1);
		
	}
	/* Never reached */
	return 0;
}

static char *wait_result(void)
{
	fd_set fds;
	int res;
	int max;
	static char astresp[256];
	static char sphinxresp[256];
	char audiobuf[4096];
	for (;;) {
		FD_ZERO(&fds);
		FD_SET(STDIN_FILENO, &fds);
		FD_SET(AUDIO_FILENO, &fds);
		max = AUDIO_FILENO;
		if (sphinx_sock > -1) {
			FD_SET(sphinx_sock, &fds);
			if (sphinx_sock > max)
				max = sphinx_sock;
		}
		/* Wait for *some* sort of I/O */
		res = select(max + 1, &fds, NULL, NULL, NULL);
		if (res < 0) {
			fprintf(stderr, "Error in select: %s\n", strerror(errno));
			return NULL;
		}
		if (FD_ISSET(STDIN_FILENO, &fds)) {
			fgets(astresp, sizeof(astresp), stdin);
			if (feof(stdin)) {
				fprintf(stderr, "Got hungup on apparently\n");
				return NULL;
			}
			astresp[strlen(astresp) - 1] = '\0';
			fprintf(stderr, "Ooh, got a response from Asterisk: '%s'\n", astresp);
			return astresp;
		}
		if (FD_ISSET(AUDIO_FILENO, &fds)) {
			res = read(AUDIO_FILENO, audiobuf, sizeof(audiobuf));
			if (res > 0) {
				if (sphinx_sock > -1) 
					write(sphinx_sock, audiobuf, res);
			}
		}
		if ((sphinx_sock > -1) && FD_ISSET(sphinx_sock, &fds)) {
			res = read(sphinx_sock, sphinxresp, sizeof(sphinxresp));
			if (res > 0) {
				fprintf(stderr, "Oooh, Sphinx found a token: '%s'\n", sphinxresp);
				return sphinxresp;
			} else if (res == 0) {
				fprintf(stderr, "Hrm, lost sphinx, guess we're on our own\n");
				close(sphinx_sock);
				sphinx_sock = -1;
			}
		}
	}
		
}

static char *run_command(char *command)
{
	fprintf(stdout, "%s\n", command);
	return wait_result();
}

static int run_script(void)
{
	char *res;
	res = run_command("STREAM FILE demo-enterkeywords 0123456789*#");
	if (!res) {
		fprintf(stderr, "Failed to execute command\n");
		return -1;
	}
	fprintf(stderr, "1. Result is '%s'\n", res);
	res = run_command("STREAM FILE demo-nomatch 0123456789*#");
	if (!res) {
		fprintf(stderr, "Failed to execute command\n");
		return -1;
	}
	fprintf(stderr, "2. Result is '%s'\n", res);
	res = run_command("SAY NUMBER 23452345 0123456789*#");
	if (!res) {
		fprintf(stderr, "Failed to execute command\n");
		return -1;
	}
	fprintf(stderr, "3. Result is '%s'\n", res);
	res = run_command("GET DATA demo-enterkeywords");
	if (!res) {
		fprintf(stderr, "Failed to execute command\n");
		return -1;
	}
	fprintf(stderr, "4. Result is '%s'\n", res);
	res = run_command("STREAM FILE auth-thankyou \"\"");
	if (!res) {
		fprintf(stderr, "Failed to execute command\n");
		return -1;
	}
	fprintf(stderr, "5. Result is '%s'\n", res);
	return 0;
}

int main(int argc, char *argv[])
{
	char *tmp;
	int ver = 0;
	int subver = 0;
	/* Setup stdin/stdout for line buffering */
	setlinebuf(stdin);
	setlinebuf(stdout);
	if (read_environment()) {
		fprintf(stderr, "Failed to read environment: %s\n", strerror(errno));
		exit(1);
	}
	connect_sphinx();
	tmp = getenv("agi_enhanced");
	if (tmp) {
		if (sscanf(tmp, "%d.%d", &ver, &subver) != 2)
			ver = 0;
	}
	if (ver < 1) {
		fprintf(stderr, "No enhanced AGI services available.  Use EAGI, not AGI\n");
		exit(1);
	}
	if (run_script())
		return -1;
	exit(0);
}
