/**
 * Provides the check if the codec contained in a ast_format has
 * interleaved stereo functionality.
 */

#include "asterisk/format.h"


static int interleaved_stereo(const struct ast_format *format, int *sample_rate) {
	if (strcmp("PLACEHOLDER_STEREO_CODEC", ast_format_get_name(format)) == 0) {
		return  1;
	}
	return 0;
}
