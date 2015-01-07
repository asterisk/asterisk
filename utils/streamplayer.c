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

/*!
 * \file
 * \author Russell Bryant <russell@digium.com>
 * 
 * \brief A utility for reading from a raw TCP stream
 *
 * This application is intended for use when a raw TCP stream is desired to be
 * used as a music on hold source for Asterisk.  Some devices are capable of
 * taking some kind of audio input and provide it as a raw TCP stream over the
 * network, which is what inspired someone to fund this to be written.
 * However, it would certainly be possible to write your own server application
 * to provide music over a TCP stream from a centralized location.
 *
 * This application is quite simple.  It just reads the data from the TCP
 * stream and dumps it straight to stdout.  Due to the way Asterisk handles
 * music on hold sources, this application checks to make sure writing
 * to stdout will not be a blocking operation before doing so.  If so, the data
 * is just thrown away.  This ensures that the stream will continue to be
 * serviced, even if Asterisk is not currently using the source.
 *
 * \todo Update this application to be able to connect to a stream via HTTP,
 * since that is the #1 most requested feature, and it would be quite useful.
 * A lot of people think that is what this is for and email me when it does
 * not work.  :)
 */

/*** MODULEINFO
	<defaultenabled>no</defaultenabled>
	<support_level>extended</support_level>
 ***/

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
		fprintf(stderr, "streamplayer -- A utility for reading from a raw TCP stream.\n");
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

		if (FD_ISSET(1, &wfds)) {
			if (write(1, buf, res) < 1) {
				break;
			}
		}
	}

	close(s);
	exit(res);
}
