/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2014, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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
 * \brief Codecs API
 *
 * \author Joshua Colp <jcolp@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/logger.h"
#include "asterisk/codec.h"
#include "asterisk/format.h"
#include "asterisk/frame.h"
#include "asterisk/astobj2.h"
#include "asterisk/strings.h"
#include "asterisk/module.h"
#include "asterisk/cli.h"

/*! \brief Number of buckets to use for codecs (should be prime for performance reasons) */
#define CODEC_BUCKETS 53

/*! \brief Current identifier value for newly registered codec */
static int codec_id = 1;

/*! \brief Registered codecs */
static struct ao2_container *codecs;

static int codec_hash(const void *obj, int flags)
{
	const struct ast_codec *codec;
	const char *key;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_KEY:
		key = obj;
		return ast_str_hash(key);
	case OBJ_SEARCH_OBJECT:
		codec = obj;
		return ast_str_hash(codec->name);
	default:
		/* Hash can only work on something with a full key. */
		ast_assert(0);
		return 0;
	}
}

static int codec_cmp(void *obj, void *arg, int flags)
{
	const struct ast_codec *left = obj;
	const struct ast_codec *right = arg;
	const char *right_key = arg;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = right->name;
		cmp = strcmp(left->name, right_key);

		if (right->type != AST_MEDIA_TYPE_UNKNOWN) {
			cmp |= (right->type != left->type);
		}

		/* BUGBUG: this will allow a match on a codec by name only.
		 * This is particularly useful when executed by the CLI; if
		 * that is not needed in translate.c, this can be removed.
		 */
		if (right->sample_rate) {
			cmp |= (right->sample_rate != left->sample_rate);
		}
		break;
	case OBJ_SEARCH_KEY:
		cmp = strcmp(left->name, right_key);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		cmp = strncmp(left->name, right_key, strlen(right_key));
		break;
	default:
		ast_assert(0);
		cmp = 0;
		break;
	}
	if (cmp) {
		return 0;
	}

	return CMP_MATCH;
}

static char *show_codecs(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_iterator i;
	struct ast_codec *codec;

	switch (cmd) {
	case CLI_INIT:
		e->command = "core show codecs [audio|video|image|text]";
		e->usage =
			"Usage: core show codecs [audio|video|image|text]\n"
			"       Displays codec mapping\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if ((a->argc < 3) || (a->argc > 4)) {
		return CLI_SHOWUSAGE;
	}

	if (!ast_opt_dont_warn) {
		ast_cli(a->fd, "Disclaimer: this command is for informational purposes only.\n"
				"\tIt does not indicate anything about your configuration.\n");
	}

	ast_cli(a->fd, "%8s %5s %8s %s\n","ID","TYPE","NAME","DESCRIPTION");
	ast_cli(a->fd, "-----------------------------------------------------------------------------------\n");

	ao2_rdlock(codecs);
	i = ao2_iterator_init(codecs, AO2_ITERATOR_DONTLOCK);

	for (; (codec = ao2_iterator_next(&i)); ao2_ref(codec, -1)) {
		if (a->argc == 4) {
			if (!strcasecmp(a->argv[3], "audio")) {
				if (codec->type != AST_MEDIA_TYPE_AUDIO) {
					continue;
				}
			} else if (!strcasecmp(a->argv[3], "video")) {
				if (codec->type != AST_MEDIA_TYPE_VIDEO) {
					continue;
				}
			} else if (!strcasecmp(a->argv[3], "image")) {
				if (codec->type != AST_MEDIA_TYPE_IMAGE) {
					continue;
				}
			} else if (!strcasecmp(a->argv[3], "text")) {
				if (codec->type != AST_MEDIA_TYPE_TEXT) {
					continue;
				}
			} else {
				continue;
			}
		}

		ast_cli(a->fd, "%8u %5s %8s (%s)\n",
			codec->id,
			ast_codec_media_type2str(codec->type),
			codec->name,
			codec->description);
	}

	ao2_iterator_destroy(&i);
	ao2_unlock(codecs);

	return CLI_SUCCESS;
}

/*! \brief Callback function for getting a codec based on unique identifier */
static int codec_id_cmp(void *obj, void *arg, int flags)
{
	struct ast_codec *codec = obj;
	int *id = arg;

	return (codec->id == *id) ? CMP_MATCH | CMP_STOP : 0;
}

static char *show_codec(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int type_punned_codec;
	struct ast_codec *codec;

	switch (cmd) {
	case CLI_INIT:
		e->command = "core show codec";
		e->usage =
			"Usage: core show codec <number>\n"
			"       Displays codec mapping\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	if (sscanf(a->argv[3], "%30d", &type_punned_codec) != 1) {
		return CLI_SHOWUSAGE;
	}

	codec = ao2_callback(codecs, 0, codec_id_cmp, &type_punned_codec);
	if (!codec) {
		ast_cli(a->fd, "Codec %d not found\n", type_punned_codec);
		return CLI_SUCCESS;
	}

	ast_cli(a->fd, "%11u %s\n", (unsigned int) codec->id, codec->description);

	ao2_ref(codec, -1);

	return CLI_SUCCESS;
}

/* Builtin Asterisk CLI-commands for debugging */
static struct ast_cli_entry codec_cli[] = {
	AST_CLI_DEFINE(show_codecs, "Displays a list of registered codecs"),
	AST_CLI_DEFINE(show_codec, "Shows a specific codec"),
};

/*! \brief Function called when the process is shutting down */
static void codec_shutdown(void)
{
	ast_cli_unregister_multiple(codec_cli, ARRAY_LEN(codec_cli));
	ao2_cleanup(codecs);
	codecs = NULL;
}

int ast_codec_init(void)
{
	codecs = ao2_container_alloc_options(AO2_ALLOC_OPT_LOCK_RWLOCK, CODEC_BUCKETS, codec_hash, codec_cmp);
	if (!codecs) {
		return -1;
	}

	ast_cli_register_multiple(codec_cli, ARRAY_LEN(codec_cli));
	ast_register_cleanup(codec_shutdown);

	return 0;
}

static void codec_dtor(void *obj)
{
	struct ast_codec *codec;

	codec = obj;

	ast_module_unref(codec->mod);
}

int __ast_codec_register(struct ast_codec *codec, struct ast_module *mod)
{
	SCOPED_AO2WRLOCK(lock, codecs);
	struct ast_codec *codec_new;

	/* Some types have specific requirements */
	if (codec->type == AST_MEDIA_TYPE_UNKNOWN) {
		ast_log(LOG_ERROR, "A media type must be specified for codec '%s'\n", codec->name);
		return -1;
	} else if (codec->type == AST_MEDIA_TYPE_AUDIO) {
		if (!codec->sample_rate) {
			ast_log(LOG_ERROR, "A sample rate must be specified for codec '%s' of type '%s'\n",
				codec->name, ast_codec_media_type2str(codec->type));
			return -1;
		}
	}

	codec_new = ao2_find(codecs, codec, OBJ_SEARCH_OBJECT | OBJ_NOLOCK);
	if (codec_new) {
		ast_log(LOG_ERROR, "A codec with name '%s' of type '%s' and sample rate '%u' is already registered\n",
			codec->name, ast_codec_media_type2str(codec->type), codec->sample_rate);
		ao2_ref(codec_new, -1);
		return -1;
	}

	codec_new = ao2_t_alloc_options(sizeof(*codec_new), codec_dtor,
		AO2_ALLOC_OPT_LOCK_NOLOCK, S_OR(codec->description, ""));
	if (!codec_new) {
		ast_log(LOG_ERROR, "Could not allocate a codec with name '%s' of type '%s' and sample rate '%u'\n",
			codec->name, ast_codec_media_type2str(codec->type), codec->sample_rate);
		return -1;
	}
	*codec_new = *codec;
	codec_new->id = codec_id++;

	ao2_link_flags(codecs, codec_new, OBJ_NOLOCK);

	/* Once registered a codec can not be unregistered, and the module must persist until shutdown */
	ast_module_shutdown_ref(mod);

	ast_verb(2, "Registered '%s' codec '%s' at sample rate '%u' with id '%u'\n",
		ast_codec_media_type2str(codec->type), codec->name, codec->sample_rate, codec_new->id);

	ao2_ref(codec_new, -1);

	return 0;
}

struct ast_codec *ast_codec_get(const char *name, enum ast_media_type type, unsigned int sample_rate)
{
	struct ast_codec codec = {
		.name = name,
		.type = type,
		.sample_rate = sample_rate,
	};

	return ao2_find(codecs, &codec, OBJ_SEARCH_OBJECT);
}

struct ast_codec *ast_codec_get_by_id(int id)
{
	return ao2_callback(codecs, 0, codec_id_cmp, &id);
}

int ast_codec_get_max(void)
{
	return codec_id;
}

const char *ast_codec_media_type2str(enum ast_media_type type)
{
	switch (type) {
	case AST_MEDIA_TYPE_AUDIO:
		return "audio";
	case AST_MEDIA_TYPE_VIDEO:
		return "video";
	case AST_MEDIA_TYPE_IMAGE:
		return "image";
	case AST_MEDIA_TYPE_TEXT:
		return "text";
	default:
		return "<unknown>";
	}
}

unsigned int ast_codec_samples_count(struct ast_frame *frame)
{
	struct ast_codec *codec;
	unsigned int samples = 0;

	if ((frame->frametype != AST_FRAME_VOICE) &&
		(frame->frametype != AST_FRAME_VIDEO) &&
		(frame->frametype != AST_FRAME_IMAGE)) {
		return 0;
	}

	codec = ast_format_get_codec(frame->subclass.format);

	if (codec->samples_count) {
		samples = codec->samples_count(frame);
	} else {
		ast_log(LOG_WARNING, "Unable to calculate samples for codec %s\n",
			ast_format_get_name(frame->subclass.format));
	}

	ao2_ref(codec, -1);
	return samples;
}

unsigned int ast_codec_determine_length(const struct ast_codec *codec, unsigned int samples)
{
	if (!codec->get_length) {
		return 0;
	}

	return codec->get_length(samples);
}
