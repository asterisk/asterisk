/*
 * Extended AGI test application
 *
 * This code is released into the public domain
 * with no warranty of any kind
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/select.h>

#include "asterisk.h"

#include "asterisk/compat.h"

#define AUDIO_FILENO (STDERR_FILENO + 1)

static int read_environment(void)
{
	char buf[256];
	char *val;
	/* Read environment */
	for(;;) {
		if (!fgets(buf, sizeof(buf), stdin)) {
			return -1;
		}
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
	int bytes = 0;
	static char astresp[256];
	char audiobuf[4096];
	for (;;) {
		FD_ZERO(&fds);
		FD_SET(STDIN_FILENO, &fds);
		FD_SET(AUDIO_FILENO, &fds);
		/* Wait for *some* sort of I/O */
		res = select(AUDIO_FILENO + 1, &fds, NULL, NULL, NULL);
		if (res < 0) {
			fprintf(stderr, "Error in select: %s\n", strerror(errno));
			return NULL;
		}
		if (FD_ISSET(STDIN_FILENO, &fds)) {
			if (!fgets(astresp, sizeof(astresp), stdin)) {
				return NULL;
			}
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
				/* XXX Process the audio with sphinx here XXX */
#if 0
				fprintf(stderr, "Got %d/%d bytes of audio\n", res, bytes);
#endif
				bytes += res;
				/* Prentend we detected some audio after 3 seconds */
				if (bytes > 16000 * 3) {
					return "Sample Message";
					bytes = 0;
				}
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
	tmp = getenv("agi_enhanced");
	if (tmp) {
		if (sscanf(tmp, "%30d.%30d", &ver, &subver) != 2)
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
