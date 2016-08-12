/**
 * Provides the check if the codec contained in a ast_format has
 * interleaved stereo functionality.
 */

#include "asterisk/format.h"
#include "../../codecs/ex_opus.h"


static int opus_codec(const struct ast_format *format, int *sample_rate) {
	struct opus_attr *attr = ast_format_get_attribute_data(format);
	if (attr != NULL) {
		if (attr->stereo == 1) {
			*sample_rate = attr->maxplayrate;
			return 1;
		}
	}
	return 0;
}	

static int interleaved_stereo(const struct ast_format *format, int *sample_rate) {
	if (strcmp("opus", ast_format_get_name(format)) == 0) {
		return opus_codec(format, sample_rate);
	}
	return 0;
}
