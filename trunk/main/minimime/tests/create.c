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
 * MiniMIME test program - create.c
 *
 * Creates a MIME message of the given MIME parts
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
	    "USAGE: %s <part> [<part_2>[<part_N>[...]]]\n",
	    progname
	    );
}

void
print_error(void)
{
	fprintf(stderr, "ERROR: %s\n", mm_error_string());
}

int
main(int argc, char **argv)
{
	MM_CTX *ctx;
	struct mm_mimepart *part;
	char *data;
	size_t length;
	int i;
	
	progname = argv[0];

	if (argc < 2) {
		usage();
		exit(1);
	}	

	mm_library_init();

	ctx = mm_context_new();

	part = mm_mimepart_new();
	mm_context_attachpart(ctx, part);
	mm_envelope_setheader(ctx, "From", "foo@bar.com");

	for (i=1; i < argc; i++) {
		part = mm_mimepart_fromfile(argv[i]);
		if (part == NULL) {
			print_error();
			exit(1);
		}
		mm_context_attachpart(ctx, part);
	}

	if (mm_context_flatten(ctx, &data, &length, 0) == -1) {
		print_error();
		exit(1);
	}

	printf("%s", data);

	exit(0);
}
