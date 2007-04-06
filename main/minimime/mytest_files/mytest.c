#include <stdlib.h>
#include <stdio.h>

#include "mm.h"

int main(int argc, char *argv[])
{
	const char *filename = "mytest_files/ast_postdata3";
	MM_CTX *ctx;
	struct mm_mimepart *part;
	struct mm_content *cont;
	
	int res = 0;
	const char *disp;
	int i;

	mm_library_init();
	mm_codec_registerdefaultcodecs();

	printf("\nThe test should run 2 times with the same results.\n\n");

	for (i = 0; i < 2; i++) {
		printf("\nTest run #%d ...\n", i + 1);

		if (!(ctx = mm_context_new())) {
			printf("Failed to create MiniMIME context!\n\n");
			break;
		}
	
		res = mm_parse_file(ctx, filename, MM_PARSE_LOOSE, 0);
		if (res == -1) {
			printf("Error parsing file %s\n\n", filename);
			mm_context_free(ctx);
			break;
		}
	
		res = mm_context_countparts(ctx);
		if (res != 3) {
			printf("This file should have 3 parts, but parser says %d\n\n", res);
			res = -1;
			mm_context_free(ctx);
			break;
		}
	
		/* Part 2 is the file */
		if (!(part = mm_context_getpart(ctx, 2))) {
			printf("Failed to get a reference to part 2 of the MIME data\n\n");
			res = -1;
			mm_context_free(ctx);
			break;
		}
	
		/* This is where the problems are demonstrated. */
		cont =  mm_mimepart_getcontent(part);

		if ((disp = mm_content_getdispositiontype(cont)))
			printf("SUCCESS: Got the Content-Disposition: %s\n", disp);
		else
			printf("FAILURE: Could not get the Content-Disposition value!\n");

		res = mm_mimepart_getlength(part);
		if (res == 1279)
			printf("SUCCESS: Got the correct value for the body length: %d\n\n", res);
		else
			printf("FAILURE: The parser says this MIME part has %d length, but it should be 1279\n\n", res);

		mm_context_free(ctx);
	}

	exit(res);
}
