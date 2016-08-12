/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2016, Frank Haase, Dennis Guse
 *
 * Frank Haase <fra.haase@gmail.com>
 * Dennis Guse <dennis.guse@alumni.tu-berlin.de>
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
 * Converts a Head Related Impulse Response (HRIR) database (a multi-channel wave) into a C header file.
 * HRIR for the left ear and HRIR for right ear have to be interleaved.
 * No further signal processing is applied (e.g., resampling).
 *
 * Info messages are printed to stderror and the generated header file to output.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sndfile.h>
#include "conf_bridge_binaural_hrir_importer.h"

int main (int argc, char **argv)
{
	char *hrir_filename;
	unsigned int binaural_index_start;
	unsigned int binaural_index_end;

	SNDFILE *hrir_file;
	SF_INFO hrir_info;
	float *hrir_data;

	unsigned int impulse_response_index_start;
	unsigned int impulse_response_index_end;

	int j;
	int ir_current;

	if(argc != 4) {
		puts("HRIR database to C header file converter.");
		puts("Usage: conf_bridge_binaural_hrir_importer HRIR.wav INDEX_START INDEX_END > OUTPUT.h");
		puts("Example: conf_bridge_binaural_hrir_importer hrirs.wav 0 180 > ../bridges/bridge_softmix/include/hrirs.h");

		return -1;
	}

	/* Parse arguments */
	hrir_filename = argv[1];
	binaural_index_start = atoi(argv[2]);
	binaural_index_end = atoi(argv[3]);

	/* Read HRIR database */
	hrir_file = sf_open(hrir_filename, SFM_READ, &hrir_info);
	if(hrir_file == NULL) {
		fprintf(stderr, "ERROR: Could not open HRIR database (%s).\n", hrir_filename);

		return -1;
	}
	fprintf(stderr, "INFO: Opened HRIR database (%s) with: number channels: %d; samplerate: %d; samples per channel: %ld\n", hrir_filename, hrir_info.channels, hrir_info.samplerate, hrir_info.frames);

	hrir_data = (float *)malloc(hrir_info.channels * hrir_info.frames * sizeof(float));
	if(hrir_data == NULL) {
		fprintf(stderr, "ERROR: Out of memory!");

		return -1;
	}

	/* Channels are interleaved */
	sf_read_float(hrir_file, hrir_data, hrir_info.channels * hrir_info.frames);
	sf_close(hrir_file);

	if(binaural_index_start >= binaural_index_end) {
		fprintf(stderr, "ERROR: INDEX_START (%d) must be smaller than INDEX_END (%d).", binaural_index_start, binaural_index_end);
		free(hrir_data);

		return -1;
	}

	if (binaural_index_end * 2 >= hrir_info.channels) {
		fprintf(stderr, "ERROR: END_INDEX (%d) is out of range for HRIR database (%s).\n", binaural_index_end, hrir_filename);
		free(hrir_data);

		return -1;
	}

	/* Convert indices */
	impulse_response_index_start = 2 * binaural_index_start;
	impulse_response_index_end = (binaural_index_end + 1) * 2;

	/* Write header */
	printf(FILE_HEADER, hrir_filename, binaural_index_start, binaural_index_end);

	printf("#define HRIRS_IMPULSE_LEN %ld\n", hrir_info.frames);
	printf("#define HRIRS_IMPULSE_SIZE %d\n", binaural_index_end - binaural_index_start + 1);
	printf("#define HRIRS_SAMPLE_RATE %d\n\n", hrir_info.samplerate);

	printf("float hrirs_left[HRIRS_IMPULSE_SIZE][HRIRS_IMPULSE_LEN] = {\n");
	for (ir_current = impulse_response_index_start; ir_current < impulse_response_index_end; ir_current += 2) {
		printf("{");

		for (j = 0; j < hrir_info.frames - 1; j++) {
			printf("%.16f,%s", hrir_data[ir_current * hrir_info.frames + j], ((j + 1) % 4 ? " " : "\n"));
		}
		/* Write last without trailing "," */
		printf("%.16f", hrir_data[ir_current * hrir_info.frames + hrir_info.frames - 1]);

		if (ir_current + 2 < impulse_response_index_end) {
			printf("},\n");
		}	else {
			printf("}};");
		}
	}

	printf("\nfloat hrirs_right[HRIRS_IMPULSE_SIZE][HRIRS_IMPULSE_LEN] = {\n");
	for (ir_current = impulse_response_index_start + 1; ir_current < impulse_response_index_end + 1; ir_current += 2) {
		printf("{");

		for (j = 0; j < hrir_info.frames - 1; j++) {
			printf("%.16f,%s", hrir_data[ir_current * hrir_info.frames + j], ((j + 1) % 4 ? " " : "\n"));
		}
		 /* Write last without trailing "," */
		printf("%.16f", hrir_data[ir_current * hrir_info.frames + hrir_info.frames - 1]);

		if (ir_current + 2 < impulse_response_index_end) {
			printf("},\n");
		}	else {
			printf("}};");
		}
	}

	fprintf(stderr, "INFO: Successfully converted: imported %d impulse responses.\n", impulse_response_index_end - impulse_response_index_start);
	free(hrir_data);

	return 0;
}
