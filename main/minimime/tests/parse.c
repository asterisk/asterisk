/*
 * Copyright (c) 2004 Jann Fischer. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * MiniMIME test program - parse.c
 *
 * Parses any given messages
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>

#include "mm.h"

const char *progname;

void
usage(void)
{
	fprintf(stderr,
	    "MiniMIME test suite\n"
	    "Usage: %s [-m] <filename>\n\n"
	    "   -m            : use memory based scanning\n\n",
	    progname
	);
	exit(1);
}

int
main(int argc, char **argv)
{
	MM_CTX *ctx;
	struct mm_mimeheader *header, *lastheader = NULL;
	struct mm_mimepart *part;
	struct mm_content *ct;
	int parts, i;
	struct stat st;
	int fd;
	char *buf;
	int scan_mode = 0;

	progname = strdup(argv[0]);

	while ((i = getopt(argc, argv, "m")) != -1) {
		switch(i) {
		case 'm':
			scan_mode = 1;
			break;
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		usage();
	}
	
#ifdef __HAVE_LEAK_DETECTION
	/* Initialize memory leak detection if compiled in */
	MM_leakd_init();
#endif

	/* Initialize MiniMIME library */
	mm_library_init();

	/* Register all default codecs (base64/qp) */
	mm_codec_registerdefaultcodecs();

	do {
		/* Create a new context */
		ctx = mm_context_new();

		/* Parse a file into our context */
		if (scan_mode == 0) {
			i = mm_parse_file(ctx, argv[0], MM_PARSE_LOOSE, 0);
		} else {
			if (stat(argv[0], &st) == -1) {
				err(1, "stat");
			}
	
			if ((fd = open(argv[0], O_RDONLY)) == -1) {
				err(1, "open");
			}

			buf = (char *)malloc(st.st_size);
			if (buf == NULL) {
				err(1, "malloc");
			}	

			if (read(fd, buf, st.st_size) != st.st_size) {
				err(1, "read");
			}

			close(fd);
			buf[st.st_size] = '\0';
			
			i = mm_parse_mem(ctx, buf, MM_PARSE_LOOSE, 0);
		}

		if (i == -1 || mm_errno != MM_ERROR_NONE) {	
			printf("ERROR: %s at line %d\n", mm_error_string(), mm_error_lineno());
			exit(1);
		}

		/* Get the number of MIME parts */
		parts = mm_context_countparts(ctx);
		if (parts == 0) {
			printf("ERROR: got zero MIME parts, huh\n");
			exit(1);
		} else {
			if (mm_context_iscomposite(ctx)) {
				printf("Got %d MIME parts\n", parts - 1);
			} else {
				printf("Flat message (not multipart)\n");
			}
		}

		/* Get the main MIME part */
		part = mm_context_getpart(ctx, 0);
		if (part == NULL) {
			fprintf(stderr, "Could not get envelope part\n");
			exit(1);
		}

		printf("Printing envelope headers:\n");
		/* Print all headers */
		lastheader = NULL;
		while ((header = mm_mimepart_headers_next(part, &lastheader)) != NULL)
			printf("%s: %s\n", header->name, header->value);

		printf("%s\n", mm_content_tostring(part->type));
		printf("\n");
		
		ct = part->type;
		assert(ct != NULL);

		if (mm_context_iscomposite(ctx) == 0) {
			printf("Printing body part for FLAT message:\n");
			part = mm_context_getpart(ctx, 0);
			printf("%s", part->body);
		}	

		/* Loop through all MIME parts beginning with 1 */
		for (i = 1; i < mm_context_countparts(ctx); i++) {
			char *decoded;

			printf("Printing headers for MIME part %d\n", i);

			/* Get the current MIME entity */
			part = mm_context_getpart(ctx, i);
			if (part == NULL) {
				fprintf(stderr, "Should have %d parts but "
				    "couldn't retrieve part %d",
				    mm_context_countparts(ctx), i);
				exit(1);
			}

			/* Print all headers */
			lastheader = NULL;
			while ((header = mm_mimepart_headers_next(part, &lastheader)) != NULL)
				printf("%s: %s\n", header->name, header->value);

			printf("%s\n", mm_content_tostring(part->type));

			/* Print MIME part body */
			printf("\nPRINTING MESSAGE BODY (%d):\n%s\n", i, part->opaque_body);
			decoded = mm_mimepart_decode(part);
			if (decoded != NULL) {
				printf("DECODED:\n%s\n", decoded);
				free(decoded);
			}
		}
		
		printf("RECONSTRUCTED MESSAGE:\n");

		do {
			char *env;
			size_t env_len;

			mm_context_flatten(ctx, &env, &env_len, 0);
			printf("%s", env);
			free(env);

		} while (0);	

		mm_context_free(ctx);
		ctx = NULL;

#ifdef __HAVE_LEAK_DETECTION
		MM_leakd_printallocated();
#endif

	} while (0);

	return 0;
}
