/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \brief Translate via the use of pseudo channels
 *
 * \author Mark Spencer <markster@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <sys/time.h>
#include <sys/resource.h>
#include <math.h>

#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/translate.h"
#include "asterisk/module.h"
#include "asterisk/frame.h"
#include "asterisk/sched.h"
#include "asterisk/cli.h"
#include "asterisk/term.h"
#include "asterisk/format.h"

/*! \todo
 * TODO: sample frames for each supported input format.
 * We build this on the fly, by taking an SLIN frame and using
 * the existing converter to play with it.
 */

/*! max sample recalc */
#define MAX_RECALC 1000

/*! \brief the list of translators */
static AST_RWLIST_HEAD_STATIC(translators, ast_translator);

struct translator_path {
	struct ast_translator *step;       /*!< Next step translator */
	uint32_t table_cost;               /*!< Complete table cost to destination */
	uint8_t multistep;                 /*!< Multiple conversions required for this translation */
};

/*!
 * \brief a matrix that, for any pair of supported formats,
 * indicates the total cost of translation and the first step.
 * The full path can be reconstricted iterating on the matrix
 * until step->dstfmt == desired_format.
 *
 * Array indexes are 'src' and 'dest', in that order.
 *
 * Note: the lock in the 'translators' list is also used to protect
 * this structure.
 */
static struct translator_path **__matrix;

/*!
 * \brief table for converting index to format values.
 *
 * \note this table is protected by the table_lock.
 */
static unsigned int *__indextable;

/*! protects the __indextable for resizing */
static ast_rwlock_t tablelock;

/* index size starts at this*/
#define INIT_INDEX 32
/* index size grows by this as necessary */
#define GROW_INDEX 16

/*! the current largest index used by the __matrix and __indextable arrays*/
static int cur_max_index;
/*! the largest index that can be used in either the __indextable or __matrix before resize must occur */
static int index_size;

static void matrix_rebuild(int samples);

/*!
 * \internal
 * \brief converts codec id to index value.
 */
static int codec_to_index(unsigned int id)
{
	int x;

	ast_rwlock_rdlock(&tablelock);
	for (x = 0; x < cur_max_index; x++) {
		if (__indextable[x] == id) {
			/* format already exists in index2format table */
			ast_rwlock_unlock(&tablelock);
			return x;
		}
	}
	ast_rwlock_unlock(&tablelock);
	return -1; /* not found */
}

/*!
 * \internal
 * \brief converts codec to index value.
 */
static int codec2index(struct ast_codec *codec)
{
	return codec_to_index(codec->id);
}

/*!
 * \internal
 * \brief converts format to codec index value.
 */
static int format2index(struct ast_format *format)
{
	return codec_to_index(ast_format_get_codec_id(format));
}

/*!
 * \internal
 * \brief add a new codec to the matrix and index table structures.
 *
 * \note it is perfectly safe to call this on codecs already indexed.
 *
 * \retval 0, success
 * \retval -1, matrix and index table need to be resized
 */
static int add_codec2index(struct ast_codec *codec)
{
	if (codec2index(codec) != -1) {
		/* format is already already indexed */
		return 0;
	}

	ast_rwlock_wrlock(&tablelock);
	if (cur_max_index == (index_size)) {
		ast_rwlock_unlock(&tablelock);
		return -1; /* hit max length */
	}
	__indextable[cur_max_index] = codec->id;
	cur_max_index++;
	ast_rwlock_unlock(&tablelock);

	return 0;
}

/*!
 * \internal
 * \brief converts index value back to codec
 */
static struct ast_codec *index2codec(int index)
{
	struct ast_codec *codec;

	if (index >= cur_max_index) {
		return 0;
	}
	ast_rwlock_rdlock(&tablelock);
	codec = ast_codec_get_by_id(__indextable[index]);
	ast_rwlock_unlock(&tablelock);

	return codec;
}

/*!
 * \internal
 * \brief resize both the matrix and index table so they can represent
 * more translators
 *
 * \note _NO_ locks can be held prior to calling this function
 *
 * \retval 0, success
 * \retval -1, failure.  Old matrix and index table can still be used though
 */
static int matrix_resize(int init)
{
	struct translator_path **tmp_matrix = NULL;
	unsigned int *tmp_table = NULL;
	int old_index;
	int x;

	AST_RWLIST_WRLOCK(&translators);
	ast_rwlock_wrlock(&tablelock);

	old_index = index_size;
	if (init) {
		index_size += INIT_INDEX;
	} else {
		index_size += GROW_INDEX;
	}

	/* make new 2d array of translator_path structures */
	if (!(tmp_matrix = ast_calloc(1, sizeof(struct translator_path *) * (index_size)))) {
		goto resize_cleanup;
	}

	for (x = 0; x < index_size; x++) {
		if (!(tmp_matrix[x] = ast_calloc(1, sizeof(struct translator_path) * (index_size)))) {
			goto resize_cleanup;
		}
	}

	/* make new index table */
	if (!(tmp_table = ast_calloc(1, sizeof(unsigned int) * index_size))) {
		goto resize_cleanup;
	}

	/* if everything went well this far, free the old and use the new */
	if (!init) {
		for (x = 0; x < old_index; x++) {
			ast_free(__matrix[x]);
		}
		ast_free(__matrix);

		memcpy(tmp_table, __indextable, sizeof(unsigned int) * old_index);
		ast_free(__indextable);
	}

	/* now copy them over */
	__matrix = tmp_matrix;
	__indextable = tmp_table;

	matrix_rebuild(0);
	ast_rwlock_unlock(&tablelock);
	AST_RWLIST_UNLOCK(&translators);

	return 0;

resize_cleanup:
	ast_rwlock_unlock(&tablelock);
	AST_RWLIST_UNLOCK(&translators);
	if (tmp_matrix) {
		for (x = 0; x < index_size; x++) {
			ast_free(tmp_matrix[x]);
		}
		ast_free(tmp_matrix);
	}
	ast_free(tmp_table);

	return -1;
}

/*!
 * \internal
 * \brief reinitialize the __matrix during matrix rebuild
 *
 * \note must be protected by the translators list lock
 */
static void matrix_clear(void)
{
	int x;
	for (x = 0; x < index_size; x++) {
		memset(__matrix[x], '\0', sizeof(struct translator_path) * (index_size));
	}
}

/*!
 * \internal
 * \brief get a matrix entry
 *
 * \note This function must be protected by the translators list lock
 */
static struct translator_path *matrix_get(unsigned int x, unsigned int y)
{
	return __matrix[x] + y;
}

/*
 * wrappers around the translator routines.
 */

static void destroy(struct ast_trans_pvt *pvt)
{
	struct ast_translator *t = pvt->t;

	if (t->destroy) {
		t->destroy(pvt);
	}
	ao2_cleanup(pvt->f.subclass.format);
	ast_free(pvt);
	ast_module_unref(t->module);
}

/*!
 * \brief Allocate the descriptor, required outbuf space,
 * and possibly desc.
 */
static struct ast_trans_pvt *newpvt(struct ast_translator *t)
{
	struct ast_trans_pvt *pvt;
	int len;
	char *ofs;

	/*
	 * compute the required size adding private descriptor,
	 * buffer, AST_FRIENDLY_OFFSET.
	 */
	len = sizeof(*pvt) + t->desc_size;
	if (t->buf_size)
		len += AST_FRIENDLY_OFFSET + t->buf_size;
	pvt = ast_calloc(1, len);
	if (!pvt) {
		return NULL;
	}
	pvt->t = t;
	ofs = (char *)(pvt + 1);	/* pointer to data space */
	if (t->desc_size) {		/* first comes the descriptor */
		pvt->pvt = ofs;
		ofs += t->desc_size;
	}
	if (t->buf_size) {/* finally buffer and header */
		pvt->outbuf.c = ofs + AST_FRIENDLY_OFFSET;
	}

	ast_module_ref(t->module);

	/* call local init routine, if present */
	if (t->newpvt && t->newpvt(pvt)) {
		ast_free(pvt);
		ast_module_unref(t->module);
		return NULL;
	}

	/* Setup normal static translation frame. */
	pvt->f.frametype = AST_FRAME_VOICE;
	pvt->f.mallocd = 0;
	pvt->f.offset = AST_FRIENDLY_OFFSET;
	pvt->f.src = pvt->t->name;
	pvt->f.data.ptr = pvt->outbuf.c;

	/* if the translator has not provided a format find one in the cache or create one */
	if (!pvt->f.subclass.format) {
		if (!ast_strlen_zero(pvt->t->format)) {
			pvt->f.subclass.format = ast_format_cache_get(pvt->t->format);
		}

		if (!pvt->f.subclass.format) {
			struct ast_codec *codec = ast_codec_get(t->dst_codec.name,
				t->dst_codec.type, t->dst_codec.sample_rate);
			if (!codec) {
				ast_log(LOG_ERROR, "Unable to get destination codec\n");
				destroy(pvt);
				return NULL;
			}
			pvt->f.subclass.format = ast_format_create(codec);
			ao2_ref(codec, -1);
		}

		if (!pvt->f.subclass.format) {
			ast_log(LOG_ERROR, "Unable to create format\n");
			destroy(pvt);
			return NULL;
		}
	}

	return pvt;
}

/*! \brief framein wrapper, deals with bound checks.  */
static int framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	int ret;
	int samples = pvt->samples;	/* initial value */

	/* Copy the last in jb timing info to the pvt */
	ast_copy_flags(&pvt->f, f, AST_FRFLAG_HAS_TIMING_INFO);
	pvt->f.ts = f->ts;
	pvt->f.len = f->len;
	pvt->f.seqno = f->seqno;

	if (f->samples == 0) {
		ast_log(LOG_WARNING, "no samples for %s\n", pvt->t->name);
	}
	if (pvt->t->buffer_samples) {	/* do not pass empty frames to callback */
		if (f->datalen == 0) { /* perform native PLC if available */
			/* If the codec has native PLC, then do that */
			if (!pvt->t->native_plc)
				return 0;
		}
		if (pvt->samples + f->samples > pvt->t->buffer_samples) {
			ast_log(LOG_WARNING, "Out of buffer space\n");
			return -1;
		}
	}
	/* we require a framein routine, wouldn't know how to do
	 * it otherwise.
	 */
	ret = pvt->t->framein(pvt, f);
	/* diagnostic ... */
	if (pvt->samples == samples)
		ast_log(LOG_WARNING, "%s did not update samples %d\n",
			pvt->t->name, pvt->samples);
	return ret;
}

/*! \brief generic frameout routine.
 * If samples and datalen are 0, take whatever is in pvt
 * and reset them, otherwise take the values in the caller and
 * leave alone the pvt values.
 */
struct ast_frame *ast_trans_frameout(struct ast_trans_pvt *pvt,
	int datalen, int samples)
{
	struct ast_frame *f = &pvt->f;

	if (samples) {
		f->samples = samples;
	} else {
		if (pvt->samples == 0) {
			return NULL;
		}
		f->samples = pvt->samples;
		pvt->samples = 0;
	}
	if (datalen) {
		f->datalen = datalen;
	} else {
		f->datalen = pvt->datalen;
		pvt->datalen = 0;
	}

	return ast_frisolate(f);
}

static struct ast_frame *default_frameout(struct ast_trans_pvt *pvt)
{
	return ast_trans_frameout(pvt, 0, 0);
}

/* end of callback wrappers and helpers */

void ast_translator_free_path(struct ast_trans_pvt *p)
{
	struct ast_trans_pvt *pn = p;
	while ( (p = pn) ) {
		pn = p->next;
		destroy(p);
	}
}

/*! \brief Build a chain of translators based upon the given source and dest formats */
struct ast_trans_pvt *ast_translator_build_path(struct ast_format *dst, struct ast_format *src)
{
	struct ast_trans_pvt *head = NULL, *tail = NULL;
	int src_index, dst_index;

	src_index = format2index(src);
	dst_index = format2index(dst);

	if (src_index < 0 || dst_index < 0) {
		ast_log(LOG_WARNING, "No translator path: (%s codec is not valid)\n", src_index < 0 ? "starting" : "ending");
		return NULL;
	}

	AST_RWLIST_RDLOCK(&translators);

	while (src_index != dst_index) {
		struct ast_trans_pvt *cur;
		struct ast_translator *t = matrix_get(src_index, dst_index)->step;
		if (!t) {
			ast_log(LOG_WARNING, "No translator path from %s to %s\n",
				ast_format_get_name(src), ast_format_get_name(dst));
			AST_RWLIST_UNLOCK(&translators);
			return NULL;
		}
		if (!(cur = newpvt(t))) {
			ast_log(LOG_WARNING, "Failed to build translator step from %s to %s\n",
				ast_format_get_name(src), ast_format_get_name(dst));
			if (head) {
				ast_translator_free_path(head);
			}
			AST_RWLIST_UNLOCK(&translators);
			return NULL;
		}
		if (!head) {
			head = cur;
		} else {
			tail->next = cur;
		}
		tail = cur;
		cur->nextin = cur->nextout = ast_tv(0, 0);
		/* Keep going if this isn't the final destination */
		src_index = cur->t->dst_fmt_index;
	}

	AST_RWLIST_UNLOCK(&translators);
	return head;
}

/*! \brief do the actual translation */
struct ast_frame *ast_translate(struct ast_trans_pvt *path, struct ast_frame *f, int consume)
{
	struct ast_trans_pvt *p = path;
	struct ast_frame *out;
	struct timeval delivery;
	int has_timing_info;
	long ts;
	long len;
	int seqno;

	has_timing_info = ast_test_flag(f, AST_FRFLAG_HAS_TIMING_INFO);
	ts = f->ts;
	len = f->len;
	seqno = f->seqno;

	if (!ast_tvzero(f->delivery)) {
		if (!ast_tvzero(path->nextin)) {
			/* Make sure this is in line with what we were expecting */
			if (!ast_tveq(path->nextin, f->delivery)) {
				/* The time has changed between what we expected and this
				   most recent time on the new packet.  If we have a
				   valid prediction adjust our output time appropriately */
				if (!ast_tvzero(path->nextout)) {
					path->nextout = ast_tvadd(path->nextout,
								  ast_tvsub(f->delivery, path->nextin));
				}
				path->nextin = f->delivery;
			}
		} else {
			/* This is our first pass.  Make sure the timing looks good */
			path->nextin = f->delivery;
			path->nextout = f->delivery;
		}
		/* Predict next incoming sample */
		path->nextin = ast_tvadd(path->nextin, ast_samp2tv(
			 f->samples, ast_format_get_sample_rate(f->subclass.format)));
	}
	delivery = f->delivery;
	for (out = f; out && p ; p = p->next) {
		framein(p, out);
		if (out != f) {
			ast_frfree(out);
		}
		out = p->t->frameout(p);
	}
	if (out) {
		/* we have a frame, play with times */
		if (!ast_tvzero(delivery)) {
			/* Regenerate prediction after a discontinuity */
			if (ast_tvzero(path->nextout)) {
				path->nextout = ast_tvnow();
			}

			/* Use next predicted outgoing timestamp */
			out->delivery = path->nextout;

			/* Predict next outgoing timestamp from samples in this
			   frame. */
			path->nextout = ast_tvadd(path->nextout, ast_samp2tv(
				 out->samples, ast_format_get_sample_rate(out->subclass.format)));
			if (f->samples != out->samples && ast_test_flag(out, AST_FRFLAG_HAS_TIMING_INFO)) {
				ast_debug(4, "Sample size different %d vs %d\n", f->samples, out->samples);
				ast_clear_flag(out, AST_FRFLAG_HAS_TIMING_INFO);
			}
		} else {
			out->delivery = ast_tv(0, 0);
			ast_set2_flag(out, has_timing_info, AST_FRFLAG_HAS_TIMING_INFO);
			if (has_timing_info) {
				out->ts = ts;
				out->len = len;
				out->seqno = seqno;
			}
		}
		/* Invalidate prediction if we're entering a silence period */
		if (out->frametype == AST_FRAME_CNG) {
			path->nextout = ast_tv(0, 0);
		}
	}
	if (consume) {
		ast_frfree(f);
	}
	return out;
}

/*!
 * \internal
 * \brief Compute the computational cost of a single translation step.
 *
 * \note This function is only used to decide which translation path to
 * use between two translators with identical src and dst formats.  Computational
 * cost acts only as a tie breaker. This is done so hardware translators
 * can naturally have precedence over software translators.
 */
static void generate_computational_cost(struct ast_translator *t, int seconds)
{
	int num_samples = 0;
	struct ast_trans_pvt *pvt;
	struct rusage start;
	struct rusage end;
	int cost;
	int out_rate = t->dst_codec.sample_rate;

	if (!seconds) {
		seconds = 1;
	}

	/* If they don't make samples, give them a terrible score */
	if (!t->sample) {
		ast_debug(3, "Translator '%s' does not produce sample frames.\n", t->name);
		t->comp_cost = 999999;
		return;
	}

	pvt = newpvt(t);
	if (!pvt) {
		ast_log(LOG_WARNING, "Translator '%s' appears to be broken and will probably fail.\n", t->name);
		t->comp_cost = 999999;
		return;
	}

	getrusage(RUSAGE_SELF, &start);

	/* Call the encoder until we've processed the required number of samples */
	while (num_samples < seconds * out_rate) {
		struct ast_frame *f = t->sample();
		if (!f) {
			ast_log(LOG_WARNING, "Translator '%s' failed to produce a sample frame.\n", t->name);
			destroy(pvt);
			t->comp_cost = 999999;
			return;
		}
		framein(pvt, f);
		ast_frfree(f);
		while ((f = t->frameout(pvt))) {
			num_samples += f->samples;
			ast_frfree(f);
		}
	}

	getrusage(RUSAGE_SELF, &end);

	cost = ((end.ru_utime.tv_sec - start.ru_utime.tv_sec) * 1000000) + end.ru_utime.tv_usec - start.ru_utime.tv_usec;
	cost += ((end.ru_stime.tv_sec - start.ru_stime.tv_sec) * 1000000) + end.ru_stime.tv_usec - start.ru_stime.tv_usec;

	destroy(pvt);

	t->comp_cost = cost / seconds;

	if (!t->comp_cost) {
		t->comp_cost = 1;
	}
}

/*!
 * \internal
 *
 * \brief If no table cost value was pre set by the translator.  An attempt is made to
 * automatically generate that cost value from the cost table based on our src and
 * dst formats.
 *
 * \note This function allows older translators built before the translation cost
 * changed away from using onely computational time to continue to be registered
 * correctly.  It is expected that translators built after the introduction of this
 * function will manually assign their own table cost value.
 *
 * \note This function is safe to use on any audio formats that used to be defined in the
 * first 64 bits of the old bit field codec representation.
 *
 * \retval Table Cost value greater than 0.
 * \retval 0 on error.
 */
static int generate_table_cost(struct ast_codec *src, struct ast_codec *dst)
{
	int src_rate = src->sample_rate;
	int src_ll = 0;
	int dst_rate = dst->sample_rate;
	int dst_ll = 0;

	if ((src->type != AST_MEDIA_TYPE_AUDIO) ||
	    (dst->type != AST_MEDIA_TYPE_AUDIO)) {
		/* This method of generating table cost is limited to audio.
		 * Translators for media other than audio must manually set their
		 * table cost. */
		return 0;
	}

	src_ll = !strcmp(src->name, "slin");
	dst_ll = !strcmp(dst->name, "slin");
	if (src_ll) {
		if (dst_ll && (src_rate == dst_rate)) {
			return AST_TRANS_COST_LL_LL_ORIGSAMP;
		} else if (!dst_ll && (src_rate == dst_rate)) {
			return AST_TRANS_COST_LL_LY_ORIGSAMP;
		} else if (dst_ll && (src_rate < dst_rate)) {
			return AST_TRANS_COST_LL_LL_UPSAMP;
		} else if (!dst_ll && (src_rate < dst_rate)) {
			return AST_TRANS_COST_LL_LY_UPSAMP;
		} else if (dst_ll && (src_rate > dst_rate)) {
			return AST_TRANS_COST_LL_LL_DOWNSAMP;
		} else if (!dst_ll && (src_rate > dst_rate)) {
			return AST_TRANS_COST_LL_LY_DOWNSAMP;
		} else {
			return AST_TRANS_COST_LL_UNKNOWN;
		}
	} else {
		if (dst_ll && (src_rate == dst_rate)) {
			return AST_TRANS_COST_LY_LL_ORIGSAMP;
		} else if (!dst_ll && (src_rate == dst_rate)) {
			return AST_TRANS_COST_LY_LY_ORIGSAMP;
		} else if (dst_ll && (src_rate < dst_rate)) {
			return AST_TRANS_COST_LY_LL_UPSAMP;
		} else if (!dst_ll && (src_rate < dst_rate)) {
			return AST_TRANS_COST_LY_LY_UPSAMP;
		} else if (dst_ll && (src_rate > dst_rate)) {
			return AST_TRANS_COST_LY_LL_DOWNSAMP;
		} else if (!dst_ll && (src_rate > dst_rate)) {
			return AST_TRANS_COST_LY_LY_DOWNSAMP;
		} else {
			return AST_TRANS_COST_LY_UNKNOWN;
		}
	}
}

/*!
 * \brief rebuild a translation matrix.
 * \note This function expects the list of translators to be locked
*/
static void matrix_rebuild(int samples)
{
	struct ast_translator *t;
	int newtablecost;
	int x;      /* source format index */
	int y;      /* intermediate format index */
	int z;      /* destination format index */

	ast_debug(1, "Resetting translation matrix\n");

	matrix_clear();

	/* first, compute all direct costs */
	AST_RWLIST_TRAVERSE(&translators, t, list) {
		if (!t->active) {
			continue;
		}

		x = t->src_fmt_index;
		z = t->dst_fmt_index;

		if (samples) {
			generate_computational_cost(t, samples);
		}

		/* This new translator is the best choice if any of the below are true.
		 * 1. no translation path is set between x and z yet.
		 * 2. the new table cost is less.
		 * 3. the new computational cost is less.  Computational cost is only used
		 *    to break a tie between two identical translation paths.
		 */
		if (!matrix_get(x, z)->step ||
			(t->table_cost < matrix_get(x, z)->step->table_cost) ||
			(t->comp_cost < matrix_get(x, z)->step->comp_cost)) {

			matrix_get(x, z)->step = t;
			matrix_get(x, z)->table_cost = t->table_cost;
		}
	}

	/*
	 * For each triple x, y, z of distinct formats, check if there is
	 * a path from x to z through y which is cheaper than what is
	 * currently known, and in case, update the matrix.
	 * Repeat until the matrix is stable.
	 */
	for (;;) {
		int changed = 0;
		for (x = 0; x < cur_max_index; x++) {      /* source format */
			for (y = 0; y < cur_max_index; y++) {  /* intermediate format */
				if (x == y) {                      /* skip ourselves */
					continue;
				}
				for (z = 0; z < cur_max_index; z++) {  /* dst format */
					if ((z == x || z == y) ||        /* skip null conversions */
						!matrix_get(x, y)->step ||   /* no path from x to y */
						!matrix_get(y, z)->step) {   /* no path from y to z */
						continue;
					}

					/* calculate table cost from x->y->z */
					newtablecost = matrix_get(x, y)->table_cost + matrix_get(y, z)->table_cost;

					/* if no step already exists between x and z OR the new cost of using the intermediate
					 * step is cheaper, use this step. */
					if (!matrix_get(x, z)->step || (newtablecost < matrix_get(x, z)->table_cost)) {
						matrix_get(x, z)->step = matrix_get(x, y)->step;
						matrix_get(x, z)->table_cost = newtablecost;
						matrix_get(x, z)->multistep = 1;
						changed++;

						if (DEBUG_ATLEAST(10)) {
							struct ast_codec *x_codec = index2codec(x);
							struct ast_codec *y_codec = index2codec(y);
							struct ast_codec *z_codec = index2codec(z);

							ast_log(LOG_DEBUG,
								"Discovered %u cost path from %s to %s, via %s\n",
								matrix_get(x, z)->table_cost, x_codec->name,
								y_codec->name, z_codec->name);

							ao2_ref(x_codec, -1);
							ao2_ref(y_codec, -1);
							ao2_ref(z_codec, -1);
						}
					}
				}
			}
		}
		if (!changed) {
			break;
		}
	}
}

static void codec_append_name(const struct ast_codec *codec, struct ast_str **buf)
{
	if (codec) {
		ast_str_append(buf, 0, "(%s@%u)", codec->name, codec->sample_rate);
	} else {
		ast_str_append(buf, 0, "(nothing)");
	}
}

const char *ast_translate_path_to_str(struct ast_trans_pvt *p, struct ast_str **str)
{
	if (!p || !p->t) {
		return "";
	}

	codec_append_name(&p->t->src_codec, str);
	while (p) {
		ast_str_append(str, 0, "->");
		codec_append_name(&p->t->dst_codec, str);
		p = p->next;
	}

	return ast_str_buffer(*str);
}

static char *complete_trans_path_choice(const char *line, const char *word, int pos, int state)
{
	int i = 1, which = 0;
	int wordlen = strlen(word);
	struct ast_codec *codec;

	while ((codec = ast_codec_get_by_id(i))) {
		++i;
		if (codec->type != AST_MEDIA_TYPE_AUDIO) {
			ao2_ref(codec, -1);
			continue;
		}
		if (!strncasecmp(word, codec->name, wordlen) && ++which > state) {
			char *res = ast_strdup(codec->name);
			ao2_ref(codec, -1);
			return res;
		}
		ao2_ref(codec, -1);
	}
	return NULL;
}

static void handle_cli_recalc(struct ast_cli_args *a)
{
	int time = a->argv[4] ? atoi(a->argv[4]) : 1;

	if (time <= 0) {
		ast_cli(a->fd, "         Recalc must be greater than 0.  Defaulting to 1.\n");
		time = 1;
	}

	if (time > MAX_RECALC) {
		ast_cli(a->fd, "         Maximum limit of recalc exceeded by %d, truncating value to %d\n", time - MAX_RECALC, MAX_RECALC);
		time = MAX_RECALC;
	}
	ast_cli(a->fd, "         Recalculating Codec Translation (number of sample seconds: %d)\n\n", time);
	AST_RWLIST_WRLOCK(&translators);
	matrix_rebuild(time);
	AST_RWLIST_UNLOCK(&translators);
}

static char *handle_show_translation_table(struct ast_cli_args *a)
{
	int x, y, i, k;
	int longest = 0, num_codecs = 0, curlen = 0;
	struct ast_str *out = ast_str_create(1024);
	struct ast_codec *codec;

	/* Get the length of the longest (usable?) codec name,
	   so we know how wide the left side should be */
	for (i = 1; (codec = ast_codec_get_by_id(i)); ao2_ref(codec, -1), ++i) {
		++num_codecs;
		if (codec->type != AST_MEDIA_TYPE_AUDIO) {
			continue;
		}
		curlen = strlen(codec->name);
		if (curlen > longest) {
			longest = curlen;
		}
	}

	AST_RWLIST_RDLOCK(&translators);
	ast_cli(a->fd, "         Translation times between formats (in microseconds) for one second of data\n");
	ast_cli(a->fd, "          Source Format (Rows) Destination Format (Columns)\n\n");

	for (i = 0; i < num_codecs; i++) {
		struct ast_codec *row = i ? ast_codec_get_by_id(i) : NULL;

		x = -1;
		if ((i > 0) && (row->type != AST_MEDIA_TYPE_AUDIO)) {
			ao2_ref(row, -1);
			continue;
		}

		if ((i > 0) && (x = codec2index(row)) == -1) {
			ao2_ref(row, -1);
			continue;
		}

		ast_str_set(&out, 0, " ");
		for (k = 0; k < num_codecs; k++) {
			struct ast_codec *col = k ? ast_codec_get_by_id(k) : NULL;

			y = -1;
			if ((k > 0) && (col->type != AST_MEDIA_TYPE_AUDIO)) {
				ao2_ref(col, -1);
				continue;
			}

			if ((k > 0) && (y = codec2index(col)) == -1) {
				ao2_ref(col, -1);
				continue;
			}

			if (k > 0) {
				curlen = strlen(col->name);
			}

			if (curlen < 5) {
				curlen = 5;
			}

			if (x >= 0 && y >= 0 && matrix_get(x, y)->step) {
				/* Actual codec output */
				ast_str_append(&out, 0, "%*u", curlen + 1, (matrix_get(x, y)->table_cost/100));
			} else if (i == 0 && k > 0) {
				/* Top row - use a dynamic size */
				ast_str_append(&out, 0, "%*s", curlen + 1, col->name);
			} else if (k == 0 && i > 0) {
				/* Left column - use a static size. */
				ast_str_append(&out, 0, "%*s", longest, row->name);
			} else if (x >= 0 && y >= 0) {
				/* Codec not supported */
				ast_str_append(&out, 0, "%*s", curlen + 1, "-");
			} else {
				/* Upper left hand corner */
				ast_str_append(&out, 0, "%*s", longest, "");
			}
			ao2_cleanup(col);
		}
		ast_str_append(&out, 0, "\n");
		ast_cli(a->fd, "%s", ast_str_buffer(out));
		ao2_cleanup(row);
	}
	ast_free(out);
	AST_RWLIST_UNLOCK(&translators);
	return CLI_SUCCESS;
}

static char *handle_show_translation_path(struct ast_cli_args *a, const char *codec_name, unsigned int sample_rate)
{
	int i = 1;
	struct ast_str *str = ast_str_alloca(1024);
	struct ast_translator *step;
	struct ast_codec *dst_codec;
	struct ast_codec *src_codec = ast_codec_get(codec_name, AST_MEDIA_TYPE_AUDIO, sample_rate);

	if (!src_codec) {
		ast_cli(a->fd, "Source codec \"%s\" is not found.\n", codec_name);
		return CLI_FAILURE;
	}

	AST_RWLIST_RDLOCK(&translators);
	ast_cli(a->fd, "--- Translation paths SRC Codec \"%s\" sample rate %u ---\n",
		codec_name, src_codec->sample_rate);

	while ((dst_codec = ast_codec_get_by_id(i))) {
		int src, dst;
		char src_buffer[64];
		char dst_buffer[64];

		++i;
		if (src_codec == dst_codec ||
		    dst_codec->type != AST_MEDIA_TYPE_AUDIO) {
			ao2_ref(dst_codec, -1);
			continue;
		}

		dst = codec2index(dst_codec);
		src = codec2index(src_codec);

		if (src < 0 || dst < 0) {
			ast_str_set(&str, 0, "No Translation Path");
		} else {
			step = matrix_get(src, dst)->step;

			if (step) {
				codec_append_name(&step->src_codec, &str);
				while (src != dst) {
					src = step->dst_fmt_index;
					step = matrix_get(src, dst)->step;
					if (!step) {
						ast_str_append(&str, 0, "->");
						codec_append_name(dst_codec, &str);
						break;
					}
					ast_str_append(&str, 0, "->");
					codec_append_name(&step->src_codec, &str);
				}
			}
		}

		snprintf(src_buffer, sizeof(src_buffer), "%s:%u", src_codec->name, src_codec->sample_rate);
		snprintf(dst_buffer, sizeof(dst_buffer), "%s:%u", dst_codec->name, dst_codec->sample_rate);
		ast_cli(a->fd, "\t%-16.16s To %-16.16s: %-60.60s\n",
			src_buffer, dst_buffer, ast_str_buffer(str));
		ast_str_reset(str);
		ao2_ref(dst_codec, -1);
	}
	AST_RWLIST_UNLOCK(&translators);
	ao2_ref(src_codec, -1);
	return CLI_SUCCESS;
}

static char *handle_cli_core_show_translation(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	static const char * const option[] = { "recalc", "paths", NULL };

	switch (cmd) {
	case CLI_INIT:
		e->command = "core show translation";
		e->usage =
			"Usage: 'core show translation' can be used in two ways.\n"
			"       1. 'core show translation [recalc [<recalc seconds>]]\n"
			"          Displays known codec translators and the cost associated\n"
			"          with each conversion.  If the argument 'recalc' is supplied along\n"
			"          with optional number of seconds to test a new test will be performed\n"
			"          as the chart is being displayed.\n"
			"       2. 'core show translation paths [codec [sample_rate]]'\n"
			"           This will display all the translation paths associated with a codec.\n"
			"           If a codec has multiple sample rates, the sample rate must be\n"
			"           provided as well.\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 3) {
			return ast_cli_complete(a->word, option, a->n);
		}
		if (a->pos == 4 && !strcasecmp(a->argv[3], option[1])) {
			return complete_trans_path_choice(a->line, a->word, a->pos, a->n);
		}
		/* BUGBUG - add tab completion for sample rates */
		return NULL;
	}

	if (a->argc > 6)
		return CLI_SHOWUSAGE;

	if (a->argv[3] && !strcasecmp(a->argv[3], option[1]) && a->argc == 5) { /* show paths */
		return handle_show_translation_path(a, a->argv[4], 0);
	} else if (a->argv[3] && !strcasecmp(a->argv[3], option[1]) && a->argc == 6) {
		unsigned int sample_rate;
		if (sscanf(a->argv[5], "%30u", &sample_rate) != 1) {
			ast_cli(a->fd, "Invalid sample rate: %s.\n", a->argv[5]);
			return CLI_FAILURE;
		}
		return handle_show_translation_path(a, a->argv[4], sample_rate);
	} else if (a->argv[3] && !strcasecmp(a->argv[3], option[0])) { /* recalc and then fall through to show table */
		handle_cli_recalc(a);
	} else if (a->argc > 3) { /* wrong input */
		return CLI_SHOWUSAGE;
	}

	return handle_show_translation_table(a);
}

static struct ast_cli_entry cli_translate[] = {
	AST_CLI_DEFINE(handle_cli_core_show_translation, "Display translation matrix")
};

/*! \brief register codec translator */
int __ast_register_translator(struct ast_translator *t, struct ast_module *mod)
{
	struct ast_translator *u;
	char tmp[80];
	RAII_VAR(struct ast_codec *, src_codec, NULL, ao2_cleanup);
	RAII_VAR(struct ast_codec *, dst_codec, NULL, ao2_cleanup);

	src_codec = ast_codec_get(t->src_codec.name, t->src_codec.type, t->src_codec.sample_rate);
	if (!src_codec) {
		ast_assert(0);
		ast_log(LOG_WARNING, "Failed to register translator: unknown source codec %s\n", t->src_codec.name);
		return -1;
	}

	dst_codec = ast_codec_get(t->dst_codec.name, t->dst_codec.type, t->dst_codec.sample_rate);
	if (!dst_codec) {
		ast_log(LOG_WARNING, "Failed to register translator: unknown destination codec %s\n", t->dst_codec.name);
		return -1;
	}

	if (add_codec2index(src_codec) || add_codec2index(dst_codec)) {
		if (matrix_resize(0)) {
			ast_log(LOG_WARNING, "Translator matrix can not represent any more translators.  Out of resources.\n");
			return -1;
		}
		add_codec2index(src_codec);
		add_codec2index(dst_codec);
	}

	if (!mod) {
		ast_log(LOG_WARNING, "Missing module pointer, you need to supply one\n");
		return -1;
	}

	if (!t->buf_size) {
		ast_log(LOG_WARNING, "empty buf size, you need to supply one\n");
		return -1;
	}
	if (!t->table_cost && !(t->table_cost = generate_table_cost(src_codec, dst_codec))) {
		ast_log(LOG_WARNING, "Table cost could not be generated for %s, "
			"Please set table_cost variable on translator.\n", t->name);
		return -1;
	}

	t->module = mod;
	t->src_fmt_index = codec2index(src_codec);
	t->dst_fmt_index = codec2index(dst_codec);
	t->active = 1;

	if (t->src_fmt_index < 0 || t->dst_fmt_index < 0) {
		ast_log(LOG_WARNING, "Invalid translator path: (%s codec is not valid)\n", t->src_fmt_index < 0 ? "starting" : "ending");
		return -1;
	}
	if (t->src_fmt_index >= cur_max_index) {
		ast_log(LOG_WARNING, "Source codec %s is larger than cur_max_index\n", t->src_codec.name);
		return -1;
	}

	if (t->dst_fmt_index >= cur_max_index) {
		ast_log(LOG_WARNING, "Destination codec %s is larger than cur_max_index\n", t->dst_codec.name);
		return -1;
	}

	if (t->buf_size) {
		/*
		 * Align buf_size properly, rounding up to the machine-specific
		 * alignment for pointers.
		 */
		struct _test_align { void *a, *b; } p;
		int align = (char *)&p.b - (char *)&p.a;

		t->buf_size = ((t->buf_size + align - 1) / align) * align;
	}

	if (t->frameout == NULL) {
		t->frameout = default_frameout;
	}

	generate_computational_cost(t, 1);

	ast_verb(2, "Registered translator '%s' from codec %s to %s, table cost, %d, computational cost %d\n",
		 term_color(tmp, t->name, COLOR_MAGENTA, COLOR_BLACK, sizeof(tmp)),
		 t->src_codec.name, t->dst_codec.name, t->table_cost, t->comp_cost);

	AST_RWLIST_WRLOCK(&translators);

	/* find any existing translators that provide this same srcfmt/dstfmt,
	   and put this one in order based on computational cost */
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&translators, u, list) {
		if ((u->src_fmt_index == t->src_fmt_index) &&
		    (u->dst_fmt_index == t->dst_fmt_index) &&
		    (u->comp_cost > t->comp_cost)) {
			AST_RWLIST_INSERT_BEFORE_CURRENT(t, list);
			t = NULL;
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;

	/* if no existing translator was found for this codec combination,
	   add it to the beginning of the list */
	if (t) {
		AST_RWLIST_INSERT_HEAD(&translators, t, list);
	}

	matrix_rebuild(0);

	AST_RWLIST_UNLOCK(&translators);

	return 0;
}

/*! \brief unregister codec translator */
int ast_unregister_translator(struct ast_translator *t)
{
	char tmp[80];
	struct ast_translator *u;
	int found = 0;

	AST_RWLIST_WRLOCK(&translators);
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&translators, u, list) {
		if (u == t) {
			AST_RWLIST_REMOVE_CURRENT(list);
			ast_verb(2, "Unregistered translator '%s' from codec %s to %s\n",
				term_color(tmp, t->name, COLOR_MAGENTA, COLOR_BLACK, sizeof(tmp)),
				t->src_codec.name, t->dst_codec.name);
			found = 1;
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;

	if (found) {
		matrix_rebuild(0);
	}

	AST_RWLIST_UNLOCK(&translators);

	return (u ? 0 : -1);
}

void ast_translator_activate(struct ast_translator *t)
{
	AST_RWLIST_WRLOCK(&translators);
	t->active = 1;
	matrix_rebuild(0);
	AST_RWLIST_UNLOCK(&translators);
}

void ast_translator_deactivate(struct ast_translator *t)
{
	AST_RWLIST_WRLOCK(&translators);
	t->active = 0;
	matrix_rebuild(0);
	AST_RWLIST_UNLOCK(&translators);
}

/*! \brief Calculate our best translator source format, given costs, and a desired destination */
int ast_translator_best_choice(struct ast_format_cap *dst_cap,
	struct ast_format_cap *src_cap,
	struct ast_format **dst_fmt_out,
	struct ast_format **src_fmt_out)
{
	unsigned int besttablecost = INT_MAX;
	unsigned int beststeps = INT_MAX;
	RAII_VAR(struct ast_format *, best, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, bestdst, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format_cap *, joint_cap, NULL, ao2_cleanup);
	int i;
	int j;

	if (ast_format_cap_empty(dst_cap) || ast_format_cap_empty(src_cap)) {
		ast_log(LOG_ERROR, "Cannot determine best translation path since one capability supports no formats\n");
		return -1;
	}

	if (!(joint_cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT))) {
		return -1;
	}
	ast_format_cap_get_compatible(dst_cap, src_cap, joint_cap);

	for (i = 0; i < ast_format_cap_count(joint_cap); ++i) {
		struct ast_format *fmt =
			ast_format_cap_get_format(joint_cap, i);

		if (!fmt) {
			continue;
		}

		if (!best) {
			/* No ao2_ref operations needed, we're done with fmt */
			best = fmt;
			continue;
		}

		if (ast_format_get_sample_rate(best) <
		    ast_format_get_sample_rate(fmt)) {
			ao2_replace(best, fmt);
		}
		ao2_ref(fmt, -1);
	}

	if (best) {
		ao2_replace(*dst_fmt_out, best);
		ao2_replace(*src_fmt_out, best);
		return 0;
	}
	/* need to translate */
	AST_RWLIST_RDLOCK(&translators);

	for (i = 0; i < ast_format_cap_count(dst_cap); ++i) {
		struct ast_format *dst =
			ast_format_cap_get_format(dst_cap, i);

		if (!dst) {
			continue;
		}

		for (j = 0; j < ast_format_cap_count(src_cap); ++j) {
			struct ast_format *src =
				ast_format_cap_get_format(src_cap, j);
			int x, y;

			if (!src) {
				continue;
			}

			x = format2index(src);
			y = format2index(dst);
			if (x < 0 || y < 0) {
				ao2_ref(src, -1);
				continue;
			}
			if (!matrix_get(x, y) || !(matrix_get(x, y)->step)) {
				ao2_ref(src, -1);
				continue;
			}
			if (((matrix_get(x, y)->table_cost < besttablecost) ||
			     (matrix_get(x, y)->multistep < beststeps))) {
				/* better than what we have so far */
				ao2_replace(best, src);
				ao2_replace(bestdst, dst);
				besttablecost = matrix_get(x, y)->table_cost;
				beststeps = matrix_get(x, y)->multistep;
			}
			ao2_ref(src, -1);
		}
		ao2_ref(dst, -1);
	}
	AST_RWLIST_UNLOCK(&translators);
	if (!best) {
		return -1;
	}
	ao2_replace(*dst_fmt_out, bestdst);
	ao2_replace(*src_fmt_out, best);
	return 0;
}

unsigned int ast_translate_path_steps(struct ast_format *dst_format, struct ast_format *src_format)
{
	unsigned int res = -1;
	/* convert bitwise format numbers into array indices */
	int src = format2index(src_format);
	int dest = format2index(dst_format);

	if (src < 0 || dest < 0) {
		ast_log(LOG_WARNING, "No translator path: (%s codec is not valid)\n", src < 0 ? "starting" : "ending");
		return -1;
	}
	AST_RWLIST_RDLOCK(&translators);

	if (matrix_get(src, dest)->step) {
		res = matrix_get(src, dest)->multistep + 1;
	}

	AST_RWLIST_UNLOCK(&translators);

	return res;
}

static void check_translation_path(
	struct ast_format_cap *dest, struct ast_format_cap *src,
	struct ast_format_cap *result, struct ast_format *src_fmt,
	enum ast_media_type type)
{
	int index, src_index = format2index(src_fmt);
	/* For a given source format, traverse the list of
	   known formats to determine whether there exists
	   a translation path from the source format to the
	   destination format. */
	for (index = 0; (src_index >= 0) && index < cur_max_index; index++) {
		struct ast_codec *codec = index2codec(index);
		RAII_VAR(struct ast_format *, fmt, ast_format_create(codec), ao2_cleanup);

		ao2_ref(codec, -1);

		if (ast_format_get_type(fmt) != type) {
			continue;
		}

		/* if this is not a desired format, nothing to do */
		if (ast_format_cap_iscompatible_format(dest, fmt) == AST_FORMAT_CMP_NOT_EQUAL) {
			continue;
		}

		/* if the source is supplying this format, then
		   we can leave it in the result */
		if (ast_format_cap_iscompatible_format(src, fmt) == AST_FORMAT_CMP_EQUAL) {
			continue;
		}

		/* if we don't have a translation path from the src
		   to this format, remove it from the result */
		if (!matrix_get(src_index, index)->step) {
			ast_format_cap_remove(result, fmt);
			continue;
		}

		/* now check the opposite direction */
		if (!matrix_get(index, src_index)->step) {
			ast_format_cap_remove(result, fmt);
		}
	}

}

void ast_translate_available_formats(struct ast_format_cap *dest, struct ast_format_cap *src, struct ast_format_cap *result)
{
	struct ast_format *cur_dest, *cur_src;
	int index;

	for (index = 0; index < ast_format_cap_count(dest); ++index) {
		if (!(cur_dest = ast_format_cap_get_format(dest, index))) {
			continue;
		}

		/* We give preference to a joint format structure if possible */
		if ((cur_src = ast_format_cap_get_compatible_format(src, cur_dest))) {
			ast_format_cap_append(result, cur_src, 0);
			ao2_ref(cur_src, -1);
		} else {
			/* Otherwise we just use the destination format */
			ast_format_cap_append(result, cur_dest, 0);
		}
		ao2_ref(cur_dest, -1);
	}

	/* if we don't have a source format, we just have to try all
	   possible destination formats */
	if (!src) {
		return;
	}

	for (index = 0; index < ast_format_cap_count(src); ++index) {
		if (!(cur_src = ast_format_cap_get_format(src, index))) {
			continue;
		}

		AST_RWLIST_RDLOCK(&translators);
		check_translation_path(dest, src, result,
				       cur_src, AST_MEDIA_TYPE_AUDIO);
		check_translation_path(dest, src, result,
				       cur_src, AST_MEDIA_TYPE_VIDEO);
		AST_RWLIST_UNLOCK(&translators);
		ao2_ref(cur_src, -1);
	}
}

static void translate_shutdown(void)
{
	int x;
	ast_cli_unregister_multiple(cli_translate, ARRAY_LEN(cli_translate));

	ast_rwlock_wrlock(&tablelock);
	for (x = 0; x < index_size; x++) {
		ast_free(__matrix[x]);
	}
	ast_free(__matrix);
	ast_free(__indextable);
	ast_rwlock_unlock(&tablelock);
	ast_rwlock_destroy(&tablelock);
}

int ast_translate_init(void)
{
	int res = 0;
	ast_rwlock_init(&tablelock);
	res = matrix_resize(1);
	res |= ast_cli_register_multiple(cli_translate, ARRAY_LEN(cli_translate));
	ast_register_atexit(translate_shutdown);
	return res;
}
