/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2017, Digium, Inc.
 *
 * Kevin Harwell <kharwell@digium.com>
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

/*! \file
 *
 * \brief Stream echo application
 *
 * \author Kevin Harwell <kharwell@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/app.h"
#include "asterisk/conversions.h"
#include "asterisk/file.h"
#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/stream.h"

/*** DOCUMENTATION
	<application name="StreamEcho" language="en_US">
		<synopsis>
			Echo media, up to 'N' streams of a type, and DTMF back to the calling party
		</synopsis>
		<syntax>
		<parameter name="num" required="false">
			<para>The number of streams of a type to echo back. If '0' is specified then
			all streams of a type are removed.</para>
		</parameter>
		<parameter name="type" required="false">
			<para>The media type of the stream(s) to add or remove (in the case of "num"
			being '0'). This can be set to either "audio" or "video" (default). If "num"
			is empty (i.e. not specified) then this parameter is ignored.</para>
		</parameter>
		</syntax>
		<description>
			<para>If a "num" (the number of streams) is not given then this simply echos
			back any media or DTMF frames (note, however if '#' is detected then the
			application exits) read from the calling channel back to itself. This means
			for any relevant frame read from a particular stream it is written back out
			to the associated write stream in a one to one fashion.
			</para>
			<para>However if a "num" is specified, and if the calling channel allows it
			(a new offer is made requesting the allowance of additional streams) then any
			any media received, like before, is echoed back onto each stream. However, in
			this case a relevant frame received on a stream of the given "type" is also
			echoed back out to the other streams of that same type. It should be noted that
			when operating in this mode only the first stream found of the given "type" is
			allowed from the original offer. And this first stream found is also the only
			stream of that "type" granted read (send/receive) capabilities in the new offer
			whereas the additional ones are set to receive only.</para>
			<note><para>This does not echo CONTROL, MODEM, or NULL frames.</para></note>
		</description>
	</application>
 ***/

static const char app[] = "StreamEcho";

static int stream_echo_write_error(struct ast_channel *chan, struct ast_frame *frame, int pos)
{
	char frame_type[32];
	const char *media_type;
	struct ast_stream *stream;

	ast_frame_type2str(frame->frametype, frame_type, sizeof(frame_type));

	stream = pos < 0 ?
		ast_channel_get_default_stream(chan, ast_format_get_type(frame->subclass.format)) :
		ast_stream_topology_get_stream(ast_channel_get_stream_topology(chan), pos);

	media_type = ast_codec_media_type2str(ast_stream_get_type(stream));

	ast_log(LOG_ERROR, "%s - unable to write frame type '%s' to stream type '%s' at "
		"position '%d'\n", ast_channel_name(chan), frame_type, media_type,
		ast_stream_get_position(stream));

	return -1;
}

static int stream_echo_write(struct ast_channel *chan, struct ast_frame *frame,
	enum ast_media_type type, int one_to_one)
{
	int i;
	int num;
	struct ast_stream_topology *topology;

	/*
	 * Since this is an echo application if we get a frame in on a stream
	 * we simply want to echo it back out onto the same stream number.
	 */
	num = ast_channel_is_multistream(chan) ? frame->stream_num : -1;
	if (ast_write_stream(chan, num, frame)) {
		return stream_echo_write_error(chan, frame, num);
	}

	/*
	 * If the frame's type and given type don't match, or we are operating in
	 * a one to one stream echo mode then there is nothing left to do.
	 *
	 * Note, if the channel is not multi-stream capable then one_to_one will
	 * always be true, so it is safe to also not check for that here too.
	 */
	if (one_to_one || !frame->subclass.format ||
	    ast_format_get_type(frame->subclass.format) != type) {
		return 0;
	}

	/*
	 * However, if we are operating in a single stream echoed to many stream
	 * mode, and the frame's type matches the given type then we also need to
	 * find the other streams of the same type and write out to those streams
	 * as well.
	 *
	 * If we are here, then it's accepted that whatever stream number the frame
	 * was read from for the given type is the only one set to send/receive,
	 * while the others of the same type are set to receive only. Since we
	 * shouldn't assume any order to the streams, we'll loop back through all
	 * streams in the channel's topology writing only to those of the same type.
	 * And, of course also not the stream which has already been written to.
	 */
	topology = ast_channel_get_stream_topology(chan);

	for (i = 0; i < ast_stream_topology_get_count(topology); ++i) {
		struct ast_stream *stream = ast_stream_topology_get_stream(topology, i);
		if (num != i && ast_stream_get_type(stream) == type) {
			if (ast_write_stream(chan, i, frame)) {
				return stream_echo_write_error(chan, frame, i);
			}
		}
	}

	return 0;
}

static int stream_echo_perform(struct ast_channel *chan,
	struct ast_stream_topology *topology, enum ast_media_type type)
{
	int update_sent = 0;
	int request_change = topology != NULL;
	int one_to_one = 1;

	while (ast_waitfor(chan, -1) > -1) {
		struct ast_frame *f;

		if (request_change) {
			/* Request a change to the new topology */
			if (ast_channel_request_stream_topology_change(chan, topology, NULL)) {
				ast_log(LOG_WARNING, "Request stream topology change not supported "
					"by channel '%s'\n", ast_channel_name(chan));
			}
			request_change = 0;
		}

		f = ast_read_stream(chan);
		if (!f) {
			return -1;
		}

		if ((f->frametype == AST_FRAME_DTMF) && (f->subclass.integer == '#')) {
			ast_frfree(f);
			break;
		}

		f->delivery.tv_sec = 0;
		f->delivery.tv_usec = 0;

		if (f->frametype == AST_FRAME_CONTROL) {
			if (f->subclass.integer == AST_CONTROL_VIDUPDATE && !update_sent) {
				if (stream_echo_write(chan, f, type, one_to_one)) {
					ast_frfree(f);
					return -1;
				}
				update_sent = 1;
			} else if (f->subclass.integer == AST_CONTROL_SRCCHANGE) {
				update_sent = 0;
			} else if (f->subclass.integer == AST_CONTROL_STREAM_TOPOLOGY_CHANGED) {
				update_sent = 0;
				one_to_one = 0; /* Switch writing to one to many */
			}
		} else if (f->frametype == AST_FRAME_VIDEO && !update_sent){
			struct ast_frame frame = {
				.frametype = AST_FRAME_CONTROL,
				.subclass.integer = AST_CONTROL_VIDUPDATE,
			};
			stream_echo_write(chan, &frame, type, one_to_one);
			update_sent = 1;
		}

		if (f->frametype != AST_FRAME_CONTROL &&
		    f->frametype != AST_FRAME_MODEM &&
		    f->frametype != AST_FRAME_NULL &&
		    stream_echo_write(chan, f, type, one_to_one)) {
			ast_frfree(f);
			return -1;
		}

		ast_frfree(f);
	}

	return 0;
}

static struct ast_stream_topology *stream_echo_topology_alloc(
	struct ast_stream_topology *original, unsigned int num, enum ast_media_type type)
{
	int i, n = num;
	struct ast_stream_topology *res = ast_stream_topology_alloc();

	if (!res) {
		return NULL;
	}

	/*
	 * Clone every stream of a type not matching the given one. If the type
	 * matches clone only the first stream found for the given type. Then for
	 * that stream clone it again up to num - 1 times. Ignore any other streams
	 * of the same matched type in the original topology.
	 *
	 * So for instance if the original stream contains 1 audio stream and 2 video
	 * streams (video stream 'A' and video stream 'B'), num is '3', and the given
	 * type is 'video' then the resulting topology will contain a clone of the
	 * audio stream along with 3 clones of video stream 'A'. Video stream 'B' is
	 * not copied over.
	 */
	for (i = 0; i < ast_stream_topology_get_count(original); ++i) {
		struct ast_stream *stream = ast_stream_topology_get_stream(original, i);

		if (!n && ast_stream_get_type(stream) == type) {
			/* Don't copy any[more] of the given type */
			continue;
		}

		if (ast_stream_get_state(stream) == AST_STREAM_STATE_REMOVED) {
			/* Don't copy removed/declined streams */
			continue;
		}

		do {
			stream = ast_stream_clone(stream, NULL);

			if (!stream || ast_stream_topology_append_stream(res, stream) < 0) {
				ast_stream_free(stream);
				ast_stream_topology_free(res);
				return NULL;
			}

			if (ast_stream_get_type(stream) != type) {
				/* Do not multiply non matching streams */
				break;
			}

			/*
			 * Since num is not zero yet (i.e. this is first stream found to
			 * match on the type) and the types match then loop num - 1 times
			 * cloning the same stream.
			 */
			ast_stream_set_state(stream, n == num ?
			     AST_STREAM_STATE_SENDRECV : AST_STREAM_STATE_RECVONLY);
		} while (--n);
	}

	return res;
}

static int stream_echo_exec(struct ast_channel *chan, const char *data)
{
	int res;
	unsigned int num = 0;
	enum ast_media_type type;
	char *parse;
	struct ast_stream_topology *topology;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(num);
		AST_APP_ARG(type);
	);

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.num)) {
		/*
		 * If a number is not given then no topology is to be created
		 * and renegotiated. The app will just echo back each stream
		 * received to itself.
		 */
		return stream_echo_perform(chan, NULL, AST_MEDIA_TYPE_UNKNOWN);
	}

	if (ast_str_to_uint(args.num, &num)) {
		ast_log(LOG_ERROR, "Failed to parse the first parameter '%s' into a"
			" greater than or equal to zero\n", args.num);
		return -1;
	}

	type = ast_strlen_zero(args.type) ? AST_MEDIA_TYPE_VIDEO :
		ast_media_type_from_str(args.type);

	topology = stream_echo_topology_alloc(
		ast_channel_get_stream_topology(chan), num, type);
	if (!topology) {
		ast_log(LOG_ERROR, "Unable to create '%u' streams of type '%s' to"
			" the topology\n", num, ast_codec_media_type2str(type));
		return -1;
	}

	res = stream_echo_perform(chan, topology, type);

	if (ast_channel_get_stream_topology(chan) != topology) {
		ast_stream_topology_free(topology);
	}

	return res;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	return ast_register_application_xml(app, stream_echo_exec);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Stream Echo Application");
