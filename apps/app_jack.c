/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007 - 2008, Russell Bryant
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
 * \brief Jack Application
 *
 * \author Russell Bryant <russell@digium.com>
 *
 * This is an application to connect an Asterisk channel to an input
 * and output jack port so that the audio can be processed through
 * another application, or to play audio from another application.
 *
 * \extref http://www.jackaudio.org/
 *
 * \note To install libresample, check it out of the following repository:
 * <code>$ svn co http://svn.digium.com/svn/thirdparty/libresample/trunk</code>
 *
 * \ingroup applications
 */

/*** MODULEINFO
	<depend>jack</depend>
	<depend>resample</depend>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include <limits.h>

#include <jack/jack.h>
#include <jack/ringbuffer.h>

#include <libresample.h>

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/strings.h"
#include "asterisk/lock.h"
#include "asterisk/app.h"
#include "asterisk/pbx.h"
#include "asterisk/audiohook.h"
#include "asterisk/format_cache.h"

#define RESAMPLE_QUALITY 1

/* The number of frames the ringbuffers can store. The actual size is RINGBUFFER_FRAME_CAPACITY * jack_data->frame_datalen */
#define RINGBUFFER_FRAME_CAPACITY 100

/*! \brief Common options between the Jack() app and JACK_HOOK() function */
#define COMMON_OPTIONS \
"    s(<name>) - Connect to the specified jack server name.\n" \
"    i(<name>) - Connect the output port that gets created to the specified\n" \
"                jack input port.\n" \
"    o(<name>) - Connect the input port that gets created to the specified\n" \
"                jack output port.\n" \
"    n         - Do not automatically start the JACK server if it is not already\n" \
"                running.\n" \
"    c(<name>) - By default, Asterisk will use the channel name for the jack client\n" \
"                name.  Use this option to specify a custom client name.\n"
/*** DOCUMENTATION
	<application name="JACK" language="en_US">
		<synopsis>
			Jack Audio Connection Kit
		</synopsis>
		<syntax>
			<parameter name="options" required="false">
				<optionlist>
					<option name="s">
						<argument name="name" required="true">
							<para>Connect to the specified jack server name</para>
						</argument>
					</option>
					<option name="i">
						<argument name="name" required="true">
							<para>Connect the output port that gets created to the specified jack input port</para>
						</argument>
					</option>
					<option name="o">
						<argument name="name" required="true">
							<para>Connect the input port that gets created to the specified jack output port</para>
						</argument>
					</option>
					<option name="c">
						<argument name="name" required="true">
							<para>By default, Asterisk will use the channel name for the jack client name.</para>
							<para>Use this option to specify a custom client name.</para>
						</argument>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>When executing this application, two jack ports will be created;
			one input and one output. Other applications can be hooked up to
			these ports to access audio coming from, or being send to the channel.</para>
		</description>
	</application>
 ***/

static const char jack_app[] = "JACK";

struct jack_data {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(server_name);
		AST_STRING_FIELD(client_name);
		AST_STRING_FIELD(connect_input_port);
		AST_STRING_FIELD(connect_output_port);
	);
	jack_client_t *client;
	jack_port_t *input_port;
	jack_port_t *output_port;
	jack_ringbuffer_t *input_rb;
	jack_ringbuffer_t *output_rb;
	struct ast_format *audiohook_format;
	unsigned int audiohook_rate;
	unsigned int frame_datalen;
	void *output_resampler;
	double output_resample_factor;
	void *input_resampler;
	double input_resample_factor;
	unsigned int stop:1;
	unsigned int has_audiohook:1;
	unsigned int no_start_server:1;
	/*! Only used with JACK_HOOK */
	struct ast_audiohook audiohook;
};

static const struct {
	jack_status_t status;
	const char *str;
} jack_status_table[] = {
	{ JackFailure,        "Failure" },
	{ JackInvalidOption,  "Invalid Option" },
	{ JackNameNotUnique,  "Name Not Unique" },
	{ JackServerStarted,  "Server Started" },
	{ JackServerFailed,   "Server Failed" },
	{ JackServerError,    "Server Error" },
	{ JackNoSuchClient,   "No Such Client" },
	{ JackLoadFailure,    "Load Failure" },
	{ JackInitFailure,    "Init Failure" },
	{ JackShmFailure,     "Shared Memory Access Failure" },
	{ JackVersionError,   "Version Mismatch" },
};

static const char *jack_status_to_str(jack_status_t status)
{
	int i;

	for (i = 0; i < ARRAY_LEN(jack_status_table); i++) {
		if (jack_status_table[i].status == status)
			return jack_status_table[i].str;
	}

	return "Unknown Error";
}

static void log_jack_status(const char *prefix, jack_status_t status)
{
	struct ast_str *str = ast_str_alloca(512);
	int i, first = 0;

	for (i = 0; i < (sizeof(status) * 8); i++) {
		if (!(status & (1 << i)))
			continue;

		if (!first) {
			ast_str_set(&str, 0, "%s", jack_status_to_str((1 << i)));
			first = 1;
		} else
			ast_str_append(&str, 0, ", %s", jack_status_to_str((1 << i)));
	}

	ast_log(LOG_NOTICE, "%s: %s\n", prefix, ast_str_buffer(str));
}

static int alloc_resampler(struct jack_data *jack_data, int input)
{
	double from_srate, to_srate, jack_srate;
	void **resampler;
	double *resample_factor;

	if (input && jack_data->input_resampler)
		return 0;

	if (!input && jack_data->output_resampler)
		return 0;

	jack_srate = jack_get_sample_rate(jack_data->client);

	to_srate = input ? jack_data->audiohook_rate : jack_srate;
	from_srate = input ? jack_srate : jack_data->audiohook_rate;

	resample_factor = input ? &jack_data->input_resample_factor :
		&jack_data->output_resample_factor;

	if (from_srate == to_srate) {
		/* Awesome!  The jack sample rate is the same as ours.
		 * Resampling isn't needed. */
		*resample_factor = 1.0;
		return 0;
	}

	*resample_factor = to_srate / from_srate;

	resampler = input ? &jack_data->input_resampler :
		&jack_data->output_resampler;

	if (!(*resampler = resample_open(RESAMPLE_QUALITY,
		*resample_factor, *resample_factor))) {
		ast_log(LOG_ERROR, "Failed to open %s resampler\n",
			input ? "input" : "output");
		return -1;
	}

	return 0;
}

/*!
 * \brief Handle jack input port
 *
 * Read nframes number of samples from the input buffer, resample it
 * if necessary, and write it into the appropriate ringbuffer.
 */
static void handle_input(void *buf, jack_nframes_t nframes,
	struct jack_data *jack_data)
{
	short s_buf[nframes];
	float *in_buf = buf;
	size_t res;
	int i;
	size_t write_len = sizeof(s_buf);

	if (jack_data->input_resampler) {
		int total_in_buf_used = 0;
		int total_out_buf_used = 0;
		float f_buf[nframes + 1];

		memset(f_buf, 0, sizeof(f_buf));

		while (total_in_buf_used < nframes) {
			int in_buf_used;
			int out_buf_used;

			out_buf_used = resample_process(jack_data->input_resampler,
				jack_data->input_resample_factor,
				&in_buf[total_in_buf_used], nframes - total_in_buf_used,
				0, &in_buf_used,
				&f_buf[total_out_buf_used], ARRAY_LEN(f_buf) - total_out_buf_used);

			if (out_buf_used < 0)
				break;

			total_out_buf_used += out_buf_used;
			total_in_buf_used += in_buf_used;

			if (total_out_buf_used == ARRAY_LEN(f_buf)) {
				ast_log(LOG_ERROR, "Output buffer filled ... need to increase its size, "
					"nframes '%d', total_out_buf_used '%d'\n", nframes, total_out_buf_used);
				break;
			}
		}

		for (i = 0; i < total_out_buf_used; i++)
			s_buf[i] = f_buf[i] * (SHRT_MAX / 1.0);

		write_len = total_out_buf_used * sizeof(int16_t);
	} else {
		/* No resampling needed */

		for (i = 0; i < nframes; i++)
			s_buf[i] = in_buf[i] * (SHRT_MAX / 1.0);
	}

	res = jack_ringbuffer_write(jack_data->input_rb, (const char *) s_buf, write_len);
	if (res != write_len) {
		ast_log(LOG_WARNING, "Tried to write %d bytes to the ringbuffer, but only wrote %d\n",
			(int) sizeof(s_buf), (int) res);
	}
}

/*!
 * \brief Handle jack output port
 *
 * Read nframes number of samples from the ringbuffer and write it out to the
 * output port buffer.
 */
static void handle_output(void *buf, jack_nframes_t nframes,
	struct jack_data *jack_data)
{
	size_t res, len;

	len = nframes * sizeof(float);

	res = jack_ringbuffer_read(jack_data->output_rb, buf, len);

	if (len != res) {
		ast_debug(2, "Wanted %d bytes to send to the output port, "
			"but only got %d\n", (int) len, (int) res);
	}
}

static int jack_process(jack_nframes_t nframes, void *arg)
{
	struct jack_data *jack_data = arg;
	void *input_port_buf, *output_port_buf;

	if (!jack_data->input_resample_factor)
		alloc_resampler(jack_data, 1);

	input_port_buf = jack_port_get_buffer(jack_data->input_port, nframes);
	handle_input(input_port_buf, nframes, jack_data);

	output_port_buf = jack_port_get_buffer(jack_data->output_port, nframes);
	handle_output(output_port_buf, nframes, jack_data);

	return 0;
}

static void jack_shutdown(void *arg)
{
	struct jack_data *jack_data = arg;

	jack_data->stop = 1;
}

static struct jack_data *destroy_jack_data(struct jack_data *jack_data)
{
	if (jack_data->input_port) {
		jack_port_unregister(jack_data->client, jack_data->input_port);
		jack_data->input_port = NULL;
	}

	if (jack_data->output_port) {
		jack_port_unregister(jack_data->client, jack_data->output_port);
		jack_data->output_port = NULL;
	}

	if (jack_data->client) {
		jack_client_close(jack_data->client);
		jack_data->client = NULL;
	}

	if (jack_data->input_rb) {
		jack_ringbuffer_free(jack_data->input_rb);
		jack_data->input_rb = NULL;
	}

	if (jack_data->output_rb) {
		jack_ringbuffer_free(jack_data->output_rb);
		jack_data->output_rb = NULL;
	}

	if (jack_data->output_resampler) {
		resample_close(jack_data->output_resampler);
		jack_data->output_resampler = NULL;
	}

	if (jack_data->input_resampler) {
		resample_close(jack_data->input_resampler);
		jack_data->input_resampler = NULL;
	}

	if (jack_data->has_audiohook)
		ast_audiohook_destroy(&jack_data->audiohook);

	ast_string_field_free_memory(jack_data);

	ast_free(jack_data);

	return NULL;
}

static int init_jack_data(struct ast_channel *chan, struct jack_data *jack_data)
{
	const char *client_name;
	jack_status_t status = 0;
	jack_options_t jack_options = JackNullOption;

	unsigned int channel_rate;

	unsigned int ringbuffer_size;

	/* Deducing audiohook sample rate from channel format
	   ATTENTION: Might be problematic, if channel has different sampling than used by audiohook!
	*/
	channel_rate = ast_format_get_sample_rate(ast_channel_readformat(chan));
	jack_data->audiohook_format = ast_format_cache_get_slin_by_rate(channel_rate);
	jack_data->audiohook_rate = ast_format_get_sample_rate(jack_data->audiohook_format);

	/* Guessing frame->datalen assuming a ptime of 20ms */
	jack_data->frame_datalen = jack_data->audiohook_rate / 50;

	ringbuffer_size = jack_data->frame_datalen * RINGBUFFER_FRAME_CAPACITY;

	ast_debug(1, "Audiohook parameters: slin-format:%s, rate:%d, frame-len:%d, ringbuffer_size: %d\n",
	    ast_format_get_name(jack_data->audiohook_format), jack_data->audiohook_rate, jack_data->frame_datalen, ringbuffer_size);

	if (!ast_strlen_zero(jack_data->client_name)) {
		client_name = jack_data->client_name;
	} else {
		ast_channel_lock(chan);
		client_name = ast_strdupa(ast_channel_name(chan));
		ast_channel_unlock(chan);
	}

	if (!(jack_data->output_rb = jack_ringbuffer_create(ringbuffer_size)))
		return -1;

	if (!(jack_data->input_rb = jack_ringbuffer_create(ringbuffer_size)))
		return -1;

	if (jack_data->no_start_server)
		jack_options |= JackNoStartServer;

	if (!ast_strlen_zero(jack_data->server_name)) {
		jack_options |= JackServerName;
		jack_data->client = jack_client_open(client_name, jack_options, &status,
			jack_data->server_name);
	} else {
		jack_data->client = jack_client_open(client_name, jack_options, &status);
	}

	if (status)
		log_jack_status("Client Open Status", status);

	if (!jack_data->client)
		return -1;

	jack_data->input_port = jack_port_register(jack_data->client, "input",
		JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput | JackPortIsTerminal, 0);
	if (!jack_data->input_port) {
		ast_log(LOG_ERROR, "Failed to create input port for jack port\n");
		return -1;
	}

	jack_data->output_port = jack_port_register(jack_data->client, "output",
		JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput | JackPortIsTerminal, 0);
	if (!jack_data->output_port) {
		ast_log(LOG_ERROR, "Failed to create output port for jack port\n");
		return -1;
	}

	if (jack_set_process_callback(jack_data->client, jack_process, jack_data)) {
		ast_log(LOG_ERROR, "Failed to register process callback with jack client\n");
		return -1;
	}

	jack_on_shutdown(jack_data->client, jack_shutdown, jack_data);

	if (jack_activate(jack_data->client)) {
		ast_log(LOG_ERROR, "Unable to activate jack client\n");
		return -1;
	}

	while (!ast_strlen_zero(jack_data->connect_input_port)) {
		const char **ports;
		int i;

		ports = jack_get_ports(jack_data->client, jack_data->connect_input_port,
			NULL, JackPortIsInput);

		if (!ports) {
			ast_log(LOG_ERROR, "No input port matching '%s' was found\n",
				jack_data->connect_input_port);
			break;
		}

		for (i = 0; ports[i]; i++) {
			ast_debug(1, "Found port '%s' that matched specified input port '%s'\n",
				ports[i], jack_data->connect_input_port);
		}

		if (jack_connect(jack_data->client, jack_port_name(jack_data->output_port), ports[0])) {
			ast_log(LOG_ERROR, "Failed to connect '%s' to '%s'\n", ports[0],
				jack_port_name(jack_data->output_port));
		} else {
			ast_debug(1, "Connected '%s' to '%s'\n", ports[0],
				jack_port_name(jack_data->output_port));
		}

		jack_free(ports);

		break;
	}

	while (!ast_strlen_zero(jack_data->connect_output_port)) {
		const char **ports;
		int i;

		ports = jack_get_ports(jack_data->client, jack_data->connect_output_port,
			NULL, JackPortIsOutput);

		if (!ports) {
			ast_log(LOG_ERROR, "No output port matching '%s' was found\n",
				jack_data->connect_output_port);
			break;
		}

		for (i = 0; ports[i]; i++) {
			ast_debug(1, "Found port '%s' that matched specified output port '%s'\n",
				ports[i], jack_data->connect_output_port);
		}

		if (jack_connect(jack_data->client, ports[0], jack_port_name(jack_data->input_port))) {
			ast_log(LOG_ERROR, "Failed to connect '%s' to '%s'\n", ports[0],
				jack_port_name(jack_data->input_port));
		} else {
			ast_debug(1, "Connected '%s' to '%s'\n", ports[0],
				jack_port_name(jack_data->input_port));
		}

		jack_free(ports);

		break;
	}

	return 0;
}

static int queue_voice_frame(struct jack_data *jack_data, struct ast_frame *f)
{
	float f_buf[f->samples * 8];
	size_t f_buf_used = 0;
	int i;
	int16_t *s_buf = f->data.ptr;
	size_t res;

	memset(f_buf, 0, sizeof(f_buf));

	if (!jack_data->output_resample_factor)
		alloc_resampler(jack_data, 0);

	if (jack_data->output_resampler) {
		float in_buf[f->samples];
		int total_in_buf_used = 0;
		int total_out_buf_used = 0;

		memset(in_buf, 0, sizeof(in_buf));

		for (i = 0; i < f->samples; i++)
			in_buf[i] = s_buf[i] * (1.0 / SHRT_MAX);

		while (total_in_buf_used < ARRAY_LEN(in_buf)) {
			int in_buf_used;
			int out_buf_used;

			out_buf_used = resample_process(jack_data->output_resampler,
				jack_data->output_resample_factor,
				&in_buf[total_in_buf_used], ARRAY_LEN(in_buf) - total_in_buf_used,
				0, &in_buf_used,
				&f_buf[total_out_buf_used], ARRAY_LEN(f_buf) - total_out_buf_used);

			if (out_buf_used < 0)
				break;

			total_out_buf_used += out_buf_used;
			total_in_buf_used += in_buf_used;

			if (total_out_buf_used == ARRAY_LEN(f_buf)) {
				ast_log(LOG_ERROR, "Output buffer filled ... need to increase its size\n");
				break;
			}
		}

		f_buf_used = total_out_buf_used;
		if (f_buf_used > ARRAY_LEN(f_buf))
			f_buf_used = ARRAY_LEN(f_buf);
	} else {
		/* No resampling needed */

		for (i = 0; i < f->samples; i++)
			f_buf[i] = s_buf[i] * (1.0 / SHRT_MAX);

		f_buf_used = f->samples;
	}

	res = jack_ringbuffer_write(jack_data->output_rb, (const char *) f_buf, f_buf_used * sizeof(float));
	if (res != (f_buf_used * sizeof(float))) {
		ast_log(LOG_WARNING, "Tried to write %d bytes to the ringbuffer, but only wrote %d\n",
			(int) (f_buf_used * sizeof(float)), (int) res);
	}
	return 0;
}

/*!
 * \brief handle jack audio
 *
 * \param[in]  chan The Asterisk channel to write the frames to if no output frame
 *             is provided.
 * \param[in]  jack_data This is the jack_data struct that contains the input
 *             ringbuffer that audio will be read from.
 * \param[out] out_frame If this argument is non-NULL, then assuming there is
 *             enough data avilable in the ringbuffer, the audio in this frame
 *             will get replaced with audio from the input buffer.  If there is
 *             not enough data available to read at this time, then the frame
 *             data gets zeroed out.
 *
 * Read data from the input ringbuffer, which is the properly resampled audio
 * that was read from the jack input port.  Write it to the channel in 20 ms frames,
 * or fill up an output frame instead if one is provided.
 */
static void handle_jack_audio(struct ast_channel *chan, struct jack_data *jack_data,
	struct ast_frame *out_frame)
{
	short buf[jack_data->frame_datalen];
	struct ast_frame f = {
		.frametype = AST_FRAME_VOICE,
		.subclass.format = jack_data->audiohook_format,
		.src = "JACK",
		.data.ptr = buf,
		.datalen = sizeof(buf),
		.samples = ARRAY_LEN(buf),
	};

	for (;;) {
		size_t res, read_len;
		char *read_buf;

		read_len = out_frame ? out_frame->datalen : sizeof(buf);
		read_buf = out_frame ? out_frame->data.ptr : buf;

		res = jack_ringbuffer_read_space(jack_data->input_rb);

		if (res < read_len) {
			/* Not enough data ready for another frame, move on ... */
			if (out_frame) {
				ast_debug(1, "Sending an empty frame for the JACK_HOOK\n");
				memset(out_frame->data.ptr, 0, out_frame->datalen);
			}
			break;
		}

		res = jack_ringbuffer_read(jack_data->input_rb, (char *) read_buf, read_len);

		if (res < read_len) {
			ast_log(LOG_ERROR, "Error reading from ringbuffer, even though it said there was enough data\n");
			break;
		}

		if (out_frame) {
			/* If an output frame was provided, then we just want to fill up the
			 * buffer in that frame and return. */
			break;
		}

		ast_write(chan, &f);
	}
}

enum {
	OPT_SERVER_NAME =    (1 << 0),
	OPT_INPUT_PORT =     (1 << 1),
	OPT_OUTPUT_PORT =    (1 << 2),
	OPT_NOSTART_SERVER = (1 << 3),
	OPT_CLIENT_NAME =    (1 << 4),
};

enum {
	OPT_ARG_SERVER_NAME,
	OPT_ARG_INPUT_PORT,
	OPT_ARG_OUTPUT_PORT,
	OPT_ARG_CLIENT_NAME,

	/* Must be the last element */
	OPT_ARG_ARRAY_SIZE,
};

AST_APP_OPTIONS(jack_exec_options, BEGIN_OPTIONS
	AST_APP_OPTION_ARG('s', OPT_SERVER_NAME, OPT_ARG_SERVER_NAME),
	AST_APP_OPTION_ARG('i', OPT_INPUT_PORT, OPT_ARG_INPUT_PORT),
	AST_APP_OPTION_ARG('o', OPT_OUTPUT_PORT, OPT_ARG_OUTPUT_PORT),
	AST_APP_OPTION('n', OPT_NOSTART_SERVER),
	AST_APP_OPTION_ARG('c', OPT_CLIENT_NAME, OPT_ARG_CLIENT_NAME),
END_OPTIONS );

static struct jack_data *jack_data_alloc(void)
{
	struct jack_data *jack_data;

	if (!(jack_data = ast_calloc_with_stringfields(1, struct jack_data, 32))) {
		return NULL;
	}

	return jack_data;
}

/*!
 * \note This must be done before calling init_jack_data().
 */
static int handle_options(struct jack_data *jack_data, const char *__options_str)
{
	struct ast_flags options = { 0, };
	char *option_args[OPT_ARG_ARRAY_SIZE];
	char *options_str;

	options_str = ast_strdupa(__options_str);

	ast_app_parse_options(jack_exec_options, &options, option_args, options_str);

	if (ast_test_flag(&options, OPT_SERVER_NAME)) {
		if (!ast_strlen_zero(option_args[OPT_ARG_SERVER_NAME]))
			ast_string_field_set(jack_data, server_name, option_args[OPT_ARG_SERVER_NAME]);
		else {
			ast_log(LOG_ERROR, "A server name must be provided with the s() option\n");
			return -1;
		}
	}

	if (ast_test_flag(&options, OPT_CLIENT_NAME)) {
		if (!ast_strlen_zero(option_args[OPT_ARG_CLIENT_NAME]))
			ast_string_field_set(jack_data, client_name, option_args[OPT_ARG_CLIENT_NAME]);
		else {
			ast_log(LOG_ERROR, "A client name must be provided with the c() option\n");
			return -1;
		}
	}

	if (ast_test_flag(&options, OPT_INPUT_PORT)) {
		if (!ast_strlen_zero(option_args[OPT_ARG_INPUT_PORT]))
			ast_string_field_set(jack_data, connect_input_port, option_args[OPT_ARG_INPUT_PORT]);
		else {
			ast_log(LOG_ERROR, "A name must be provided with the i() option\n");
			return -1;
		}
	}

	if (ast_test_flag(&options, OPT_OUTPUT_PORT)) {
		if (!ast_strlen_zero(option_args[OPT_ARG_OUTPUT_PORT]))
			ast_string_field_set(jack_data, connect_output_port, option_args[OPT_ARG_OUTPUT_PORT]);
		else {
			ast_log(LOG_ERROR, "A name must be provided with the o() option\n");
			return -1;
		}
	}

	jack_data->no_start_server = ast_test_flag(&options, OPT_NOSTART_SERVER) ? 1 : 0;

	return 0;
}

static int jack_exec(struct ast_channel *chan, const char *data)
{
	struct jack_data *jack_data;

	if (!(jack_data = jack_data_alloc()))
		return -1;

	if (!ast_strlen_zero(data) && handle_options(jack_data, data)) {
		destroy_jack_data(jack_data);
		return -1;
	}

	if (init_jack_data(chan, jack_data)) {
		destroy_jack_data(jack_data);
		return -1;
	}

	if (ast_set_read_format(chan, jack_data->audiohook_format)) {
		destroy_jack_data(jack_data);
		return -1;
	}

	if (ast_set_write_format(chan, jack_data->audiohook_format)) {
		destroy_jack_data(jack_data);
		return -1;
	}

	while (!jack_data->stop) {
		struct ast_frame *f;

		if (ast_waitfor(chan, -1) < 0) {
			break;
		}

		f = ast_read(chan);
		if (!f) {
			jack_data->stop = 1;
			continue;
		}

		switch (f->frametype) {
		case AST_FRAME_CONTROL:
			if (f->subclass.integer == AST_CONTROL_HANGUP)
				jack_data->stop = 1;
			break;
		case AST_FRAME_VOICE:
			queue_voice_frame(jack_data, f);
		default:
			break;
		}

		ast_frfree(f);

		handle_jack_audio(chan, jack_data, NULL);
	}

	jack_data = destroy_jack_data(jack_data);

	return 0;
}

static void jack_hook_ds_destroy(void *data)
{
	struct jack_data *jack_data = data;

	destroy_jack_data(jack_data);
}

static const struct ast_datastore_info jack_hook_ds_info = {
	.type = "JACK_HOOK",
	.destroy = jack_hook_ds_destroy,
};

static int jack_hook_callback(struct ast_audiohook *audiohook, struct ast_channel *chan,
	struct ast_frame *frame, enum ast_audiohook_direction direction)
{
	struct ast_datastore *datastore;
	struct jack_data *jack_data;

	if (audiohook->status == AST_AUDIOHOOK_STATUS_DONE)
		return 0;

	if (direction != AST_AUDIOHOOK_DIRECTION_READ)
		return 0;

	if (frame->frametype != AST_FRAME_VOICE)
		return 0;

	ast_channel_lock(chan);

	if (!(datastore = ast_channel_datastore_find(chan, &jack_hook_ds_info, NULL))) {
		ast_log(LOG_ERROR, "JACK_HOOK datastore not found for '%s'\n", ast_channel_name(chan));
		ast_channel_unlock(chan);
		return -1;
	}

	jack_data = datastore->data;

	if (ast_format_cmp(frame->subclass.format, jack_data->audiohook_format) == AST_FORMAT_CMP_NOT_EQUAL) {
		ast_log(LOG_WARNING, "Expected frame in %s for the audiohook, but got format %s\n",
			ast_format_get_name(jack_data->audiohook_format),
			ast_format_get_name(frame->subclass.format));
		ast_channel_unlock(chan);
		return 0;
	}

	queue_voice_frame(jack_data, frame);

	handle_jack_audio(chan, jack_data, frame);

	ast_channel_unlock(chan);

	return 0;
}

static int enable_jack_hook(struct ast_channel *chan, char *data)
{
	struct ast_datastore *datastore;
	struct jack_data *jack_data = NULL;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(mode);
		AST_APP_ARG(options);
	);

	AST_STANDARD_APP_ARGS(args, data);

	ast_channel_lock(chan);

	if ((datastore = ast_channel_datastore_find(chan, &jack_hook_ds_info, NULL))) {
		ast_log(LOG_ERROR, "JACK_HOOK already enabled for '%s'\n", ast_channel_name(chan));
		goto return_error;
	}

	if (ast_strlen_zero(args.mode) || strcasecmp(args.mode, "manipulate")) {
		ast_log(LOG_ERROR, "'%s' is not a supported mode.  Only manipulate is supported.\n",
			S_OR(args.mode, "<none>"));
		goto return_error;
	}

	if (!(jack_data = jack_data_alloc()))
		goto return_error;

	if (!ast_strlen_zero(args.options) && handle_options(jack_data, args.options))
		goto return_error;

	if (init_jack_data(chan, jack_data))
		goto return_error;

	if (!(datastore = ast_datastore_alloc(&jack_hook_ds_info, NULL)))
		goto return_error;

	jack_data->has_audiohook = 1;
	ast_audiohook_init(&jack_data->audiohook, AST_AUDIOHOOK_TYPE_MANIPULATE, "JACK_HOOK", AST_AUDIOHOOK_MANIPULATE_ALL_RATES);
	jack_data->audiohook.manipulate_callback = jack_hook_callback;

	datastore->data = jack_data;

	if (ast_audiohook_attach(chan, &jack_data->audiohook))
		goto return_error;

	if (ast_channel_datastore_add(chan, datastore))
		goto return_error;

	ast_channel_unlock(chan);

	return 0;

return_error:
	ast_channel_unlock(chan);

	if (jack_data) {
		destroy_jack_data(jack_data);
	}

	if (datastore) {
		datastore->data = NULL;
		ast_datastore_free(datastore);
	}

	return -1;
}

static int disable_jack_hook(struct ast_channel *chan)
{
	struct ast_datastore *datastore;
	struct jack_data *jack_data;

	ast_channel_lock(chan);

	if (!(datastore = ast_channel_datastore_find(chan, &jack_hook_ds_info, NULL))) {
		ast_channel_unlock(chan);
		ast_log(LOG_WARNING, "No JACK_HOOK found to disable\n");
		return -1;
	}

	ast_channel_datastore_remove(chan, datastore);

	jack_data = datastore->data;
	ast_audiohook_detach(&jack_data->audiohook);

	/* Keep the channel locked while we destroy the datastore, so that we can
	 * ensure that all of the jack stuff is stopped just in case another frame
	 * tries to come through the audiohook callback. */
	ast_datastore_free(datastore);

	ast_channel_unlock(chan);

	return 0;
}

static int jack_hook_write(struct ast_channel *chan, const char *cmd, char *data,
	const char *value)
{
	int res;

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", cmd);
		return -1;
	}

	if (!strcasecmp(value, "on"))
		res = enable_jack_hook(chan, data);
	else if (!strcasecmp(value, "off"))
		res = disable_jack_hook(chan);
	else {
		ast_log(LOG_ERROR, "'%s' is not a valid value for JACK_HOOK()\n", value);
		res = -1;
	}

	return res;
}

static struct ast_custom_function jack_hook_function = {
	.name = "JACK_HOOK",
	.synopsis = "Enable a jack hook on a channel",
	.syntax = "JACK_HOOK(<mode>,[options])",
	.desc =
	"   The JACK_HOOK allows turning on or off jack connectivity to this channel.\n"
	"When the JACK_HOOK is turned on, jack ports will get created that allow\n"
	"access to the audio stream for this channel.  The mode specifies which mode\n"
	"this hook should run in.  A mode must be specified when turning the JACK_HOOK.\n"
	"on.  However, all arguments are optional when turning it off.\n"
	"\n"
	"   Valid modes are:\n"
#if 0
	/* XXX TODO */
	"    spy -        Create a read-only audio hook.  Only an output jack port will\n"
	"                 get created.\n"
	"    whisper -    Create a write-only audio hook.  Only an input jack port will\n"
	"                 get created.\n"
#endif
	"    manipulate - Create a read/write audio hook.  Both an input and an output\n"
	"                 jack port will get created.  Audio from the channel will be\n"
	"                 sent out the output port and will be replaced by the audio\n"
	"                 coming in on the input port as it gets passed on.\n"
	"\n"
	"   Valid options are:\n"
	COMMON_OPTIONS
	"\n"
	" Examples:\n"
	"   To turn on the JACK_HOOK,\n"
	"     Set(JACK_HOOK(manipulate,i(pure_data_0:input0)o(pure_data_0:output0))=on)\n"
	"   To turn off the JACK_HOOK,\n"
	"     Set(JACK_HOOK()=off)\n"
	"",
	.write = jack_hook_write,
};

static int unload_module(void)
{
	int res;

	res = ast_unregister_application(jack_app);
	res |= ast_custom_function_unregister(&jack_hook_function);

	return res;
}

static int load_module(void)
{
	if (ast_register_application_xml(jack_app, jack_exec)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ast_custom_function_register(&jack_hook_function)) {
		ast_unregister_application(jack_app);
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD_EXTENDED(ASTERISK_GPL_KEY, "JACK Interface");
