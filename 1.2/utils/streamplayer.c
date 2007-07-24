/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Russell Bryant <russell@digium.com>
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

/*
*
* streamplayer.c
*
* A utility for reading from a stream
* 
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__Darwin__) || defined(__CYGWIN__)
#include <netinet/in.h>
#endif
#include <sys/time.h>


int main(int argc, char *argv[])
{
	struct sockaddr_in sin;
	struct hostent *hp;
	int s;
	int res;
	char buf[2048];
	fd_set wfds;
	struct timeval tv;

	if (argc != 3) {
		fprintf(stderr, "streamplayer -- A utility for reading from a stream.\n");
		fprintf(stderr, "Written for use with Asterisk (http://www.asterisk.org)\n");
		fprintf(stderr, "Copyright (C) 2005 -- Russell Bryant -- Digium, Inc.\n\n");
		fprintf(stderr, "Usage: ./streamplayer <ip> <port>\n");
		exit(1);
	}

	hp = gethostbyname(argv[1]);
	if (!hp) {
		fprintf(stderr, "Unable to lookup IP for host '%s'\n", argv[1]);
		exit(1);
	}

	memset(&sin, 0, sizeof(sin));
	
	sin.sin_family = AF_INET;
	sin.sin_port = htons(atoi(argv[2]));
	memcpy(&sin.sin_addr, hp->h_addr, sizeof(sin.sin_addr));
	
	s = socket(AF_INET, SOCK_STREAM, 0);
	
	if (s < 0) {
		fprintf(stderr, "Unable to allocate socket!\n");
		exit(1);
	}	

	res = connect(s, (struct sockaddr *)&sin, sizeof(sin));
	
	if (res) {
		fprintf(stderr, "Unable to connect to host!\n");
		close(s);
		exit(1);	
	}

	while (1) {
		res = read(s, buf, sizeof(buf));

		if (res < 1)
			break;		
	
		memset(&tv, 0, sizeof(tv));		
		FD_ZERO(&wfds);
		FD_SET(1, &wfds);

		select(2, NULL, &wfds, NULL, &tv);

		if (FD_ISSET(1, &wfds))
			write(1, buf, res);
	}

	close(s);
	exit(res);
}
