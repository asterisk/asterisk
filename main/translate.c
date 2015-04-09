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
 * \brief table for converting index to format id values.
 *
 * \note this table is protected by the table_lock.
 */
static int *__indextable;

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
 * \brief converts format id to index value.
 */
static int format2index(enum ast_format_id id)
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
 * \brief add a new format to the matrix and index table structures.
 *
 * \note it is perfectly safe to call this on formats already indexed.
 *
 * \retval 0, success
 * \retval -1, matrix and index table need to be resized
 */
static int add_format2index(enum ast_format_id id)
{
	if (format2index(id) != -1) {
		/* format is already already indexed */
		return 0;
	}

	ast_rwlock_wrlock(&tablelock);
	if (cur_max_index == (index_size)) {
		ast_rwlock_unlock(&tablelock);
		return -1; /* hit max length */
	}
	__indextable[cur_max_index] = id;
	cur_max_index++;
	ast_rwlock_unlock(&tablelock);

	return 0;
}

/*!
 * \internal
 * \brief converts index value back to format id
 */
static enum ast_format_id index2format(int index)
{
	enum ast_format_id format_id;

	if (index >= cur_max_index) {
		return 0;
	}
	ast_rwlock_rdlock(&tablelock);
	format_id = __indextable[index];
	ast_rwlock_unlock(&tablelock);

	return format_id;
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
	int *tmp_table = NULL;
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
	if (!(tmp_table = ast_calloc(1, sizeof(int) * index_size))) {
		goto resize_cleanup;
	}

	/* if everything went well this far, free the old and use the new */
	if (!init) {
		for (x = 0; x < old_index; x++) {
			ast_free(__matrix[x]);
		}
		ast_free(__matrix);

		memcpy(tmp_table, __indextable, sizeof(int) * old_index);
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

/*!
 * \brief Allocate the descriptor, required outbuf space,
 * and possibly desc.
 */
static void *newpvt(struct ast_translator *t, const struct ast_format *explicit_dst)
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
	/* if a explicit destination format is provided, set that on the pvt so the
	 * translator will process it. */
	if (explicit_dst) {
		ast_format_copy(&pvt->explicit_dst, explicit_dst);
	}
	/* call local init routine, if present */
	if (t->newpvt && t->newpvt(pvt)) {
		ast_free(pvt);
		return NULL;
	}
	ast_module_ref(t->module);
	return pvt;
}

static void destroy(struct ast_trans_pvt *pvt)
{
	struct ast_translator *t = pvt->t;

	if (t->destroy)
		t->destroy(pvt);
	ast_free(pvt);
	ast_module_unref(t->module);
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
		if (pvt->samples == 0)
			return NULL;
		f->samples = pvt->samples;
		pvt->samples = 0;
	}
	if (datalen) {
		f->datalen = datalen;
	} else {
		f->datalen = pvt->datalen;
		pvt->datalen = 0;
	}

	f->frametype = AST_FRAME_VOICE;
	ast_format_copy(&f->subclass.format, &pvt->t->dst_format);
	f->mallocd = 0;
	f->offset = AST_FRIENDLY_OFFSET;
	f->src = pvt->t->name;
	f->data.ptr = pvt->outbuf.c;

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
	struct ast_format tmp_fmt1;
	struct ast_format tmp_fmt2;

	src_index = format2index(src->id);
	dst_index = format2index(dst->id);

	if (src_index == -1 || dst_index == -1) {
		ast_log(LOG_WARNING, "No translator path: (%s codec is not valid)\n", src_index == -1 ? "starting" : "ending");
		return NULL;
	}

	AST_RWLIST_RDLOCK(&translators);

	while (src_index != dst_index) {
		struct ast_trans_pvt *cur;
		struct ast_format *explicit_dst = NULL;
		struct ast_translator *t = matrix_get(src_index, dst_index)->step;
		if (!t) {
			int src_id = index2format(src_index);
			int dst_id = index2format(dst_index);
			ast_log(LOG_WARNING, "No translator path from %s to %s\n",
				ast_getformatname(ast_format_set(&tmp_fmt1, src_id, 0)),
				ast_getformatname(ast_format_set(&tmp_fmt2, dst_id, 0)));
			AST_RWLIST_UNLOCK(&translators);
			return NULL;
		}
		if (dst_index == t->dst_fmt_index) {
			explicit_dst = dst;
		}
		if (!(cur = newpvt(t, explicit_dst))) {
			int src_id = index2format(src_index);
			int dst_id = index2format(dst_index);
			ast_log(LOG_WARNING, "Failed to build translator step from %s to %s\n",
				ast_getformatname(ast_format_set(&tmp_fmt1, src_id, 0)),
				ast_getformatname(ast_format_set(&tmp_fmt2, dst_id, 0)));
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
		path->nextin = ast_tvadd(path->nextin, ast_samp2tv(f->samples, ast_format_rate(&f->subclass.format)));
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
			path->nextout = ast_tvadd(path->nextout, ast_samp2tv(out->samples, ast_format_rate(&out->subclass.format)));
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
	int out_rate = ast_format_rate(&t->dst_format);

	if (!seconds) {
		seconds = 1;
	}

	/* If they don't make samples, give them a terrible score */
	if (!t->sample) {
		ast_debug(3, "Translator '%s' does not produce sample frames.\n", t->name);
		t->comp_cost = 999999;
		return;
	}

	pvt = newpvt(t, NULL);
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
static int generate_table_cost(struct ast_format *src, struct ast_format *dst)
{
	int src_rate = ast_format_rate(src);
	int src_ll = 0;
	int dst_rate = ast_format_rate(dst);
	int dst_ll = 0;

	if ((AST_FORMAT_GET_TYPE(src->id) != AST_FORMAT_TYPE_AUDIO) || (AST_FORMAT_GET_TYPE(dst->id) != AST_FORMAT_TYPE_AUDIO)) {
		/* This method of generating table cost is limited to audio.
		 * Translators for media other than audio must manually set their
		 * table cost. */
		return 0;
	}
	src_ll = ast_format_is_slinear(src);
	dst_ll = ast_format_is_slinear(dst);
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
						struct ast_format tmpx;
						struct ast_format tmpy;
						struct ast_format tmpz;
						matrix_get(x, z)->step = matrix_get(x, y)->step;
						matrix_get(x, z)->table_cost = newtablecost;
						matrix_get(x, z)->multistep = 1;
						changed++;
						ast_debug(10, "Discovered %u cost path from %s to %s, via %s\n",
							matrix_get(x, z)->table_cost,
							ast_getformatname(ast_format_set(&tmpx, index2format(x), 0)),
							ast_getformatname(ast_format_set(&tmpy, index2format(z), 0)),
							ast_getformatname(ast_format_set(&tmpz, index2format(y), 0)));
					}
				}
			}
		}
		if (!changed) {
			break;
		}
	}
}

const char *ast_translate_path_to_str(struct ast_trans_pvt *p, struct ast_str **str)
{
	struct ast_trans_pvt *pn = p;
	char tmp[256];

	if (!p || !p->t) {
		return "";
	}

	ast_str_set(str, 0, "%s", ast_getformatname_multiple_byid(tmp, sizeof(tmp), p->t->src_format.id));

	while ( (p = pn) ) {
		pn = p->next;
		ast_str_append(str, 0, "->%s", ast_getformatname_multiple_byid(tmp, sizeof(tmp), p->t->dst_format.id));
	}

	return ast_str_buffer(*str);
}

static char *complete_trans_path_choice(const char *line, const char *word, int pos, int state)
{
	int which = 0;
	int wordlen = strlen(word);
	int i;
	char *ret = NULL;
	size_t len = 0;
	const struct ast_format_list *format_list = ast_format_list_get(&len);

	for (i = 0; i < len; i++) {
		if (AST_FORMAT_GET_TYPE(format_list[i].format.id) != AST_FORMAT_TYPE_AUDIO) {
			continue;
		}
		if (!strncasecmp(word, format_list[i].name, wordlen) && ++which > state) {
			ret = ast_strdup(format_list[i].name);
			break;
		}
	}
	ast_format_list_destroy(format_list);
	return ret;
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
	int x;
	int y;
	int i;
	int k;
	int curlen = 0;
	int longest = 0;
	int f_len;
	size_t f_size = 0;
	const struct ast_format_list *f_list = ast_format_list_get(&f_size);
	struct ast_str *out = ast_str_create(1024);

	f_len = f_size;
	AST_RWLIST_RDLOCK(&translators);
	ast_cli(a->fd, "         Translation times between formats (in microseconds) for one second of data\n");
	ast_cli(a->fd, "          Source Format (Rows) Destination Format (Columns)\n\n");

	/* Get the length of the longest (usable?) codec name, so we know how wide the left side should be */
	for (i = 0; i < f_len; i++) {
		/* translation only applies to audio right now. */
		if (AST_FORMAT_GET_TYPE(f_list[i].format.id) != AST_FORMAT_TYPE_AUDIO)
			continue;
		curlen = strlen(ast_getformatname(&f_list[i].format));
		if (curlen > longest) {
			longest = curlen;
		}
	}

	for (i = -1; i < f_len; i++) {
		x = -1;
		if ((i >= 0) && ((x = format2index(f_list[i].format.id)) == -1)) {
			continue;
		}
		/* translation only applies to audio right now. */
		if (i >= 0 && (AST_FORMAT_GET_TYPE(f_list[i].format.id) != AST_FORMAT_TYPE_AUDIO)) {
			continue;
		}
		/*Go ahead and move to next iteration if dealing with an unknown codec*/
		if (i >= 0 && !strcmp(ast_getformatname(&f_list[i].format), "unknown")) {
			continue;
		}
		ast_str_set(&out, 0, " ");
		for (k = -1; k < f_len; k++) {
			y = -1;
			if ((k >= 0) && ((y = format2index(f_list[k].format.id)) == -1)) {
				continue;
			}
			/* translation only applies to audio right now. */
			if (k >= 0 && (AST_FORMAT_GET_TYPE(f_list[k].format.id) != AST_FORMAT_TYPE_AUDIO)) {
				continue;
			}
			/*Go ahead and move to next iteration if dealing with an unknown codec*/
			if (k >= 0 && !strcmp(ast_getformatname(&f_list[k].format), "unknown")) {
				continue;
			}
			if (k >= 0) {
				curlen = strlen(ast_getformatname(&f_list[k].format));
			}
			if (curlen < 5) {
				curlen = 5;
			}

			if (x >= 0 && y >= 0 && matrix_get(x, y)->step) {
				/* Actual codec output */
				ast_str_append(&out, 0, "%*u", curlen + 1, (matrix_get(x, y)->table_cost/100));
			} else if (i == -1 && k >= 0) {
				/* Top row - use a dynamic size */
				ast_str_append(&out, 0, "%*s", curlen + 1, ast_getformatname(&f_list[k].format));
			} else if (k == -1 && i >= 0) {
				/* Left column - use a static size. */
				ast_str_append(&out, 0, "%*s", longest, ast_getformatname(&f_list[i].format));
			} else if (x >= 0 && y >= 0) {
				/* Codec not supported */
				ast_str_append(&out, 0, "%*s", curlen + 1, "-");
			} else {
				/* Upper left hand corner */
				ast_str_append(&out, 0, "%*s", longest, "");
			}
		}
		ast_str_append(&out, 0, "\n");
		ast_cli(a->fd, "%s", ast_str_buffer(out));
	}
	ast_free(out);
	AST_RWLIST_UNLOCK(&translators);
	ast_format_list_destroy(f_list);
	return CLI_SUCCESS;
}

static char *handle_show_translation_path(struct ast_cli_args *a)
{
	struct ast_format input_src_format;
	size_t len = 0;
	int i;
	const struct ast_format_list *format_list = ast_format_list_get(&len);
	struct ast_str *str = ast_str_alloca(1024);
	struct ast_translator *step;
	char tmp[256];

	ast_format_clear(&input_src_format);
	for (i = 0; i < len; i++) {
		if (AST_FORMAT_GET_TYPE(format_list[i].format.id) != AST_FORMAT_TYPE_AUDIO) {
			continue;
		}
		if (!strncasecmp(format_list[i].name, a->argv[4], strlen(format_list[i].name))) {
			ast_format_copy(&input_src_format, &format_list[i].format);
		}
	}

	if (!input_src_format.id) {
		ast_cli(a->fd, "Source codec \"%s\" is not found.\n", a->argv[4]);
		ast_format_list_destroy(format_list);
		return CLI_FAILURE;
	}

	AST_RWLIST_RDLOCK(&translators);
	ast_cli(a->fd, "--- Translation paths SRC Codec \"%s\" sample rate %d ---\n", a->argv[4], ast_format_rate(&input_src_format));
	for (i = 0; i < len; i++) {
		int src;
		int dst;
		if ((AST_FORMAT_GET_TYPE(format_list[i].format.id) != AST_FORMAT_TYPE_AUDIO) || (format_list[i].format.id == input_src_format.id)) {
			continue;
		}
		dst = format2index(format_list[i].format.id);
		src = format2index(input_src_format.id);
		ast_str_reset(str);
		if ((len >= cur_max_index) && (src != -1) && (dst != -1) && matrix_get(src, dst)->step) {
			ast_str_append(&str, 0, "%s", ast_getformatname_multiple_byid(tmp, sizeof(tmp), matrix_get(src, dst)->step->src_format.id));
			while (src != dst) {
				step = matrix_get(src, dst)->step;
				if (!step) {
					ast_str_reset(str);
					break;
				}
				ast_str_append(&str, 0, "->%s", ast_getformatname_multiple_byid(tmp, sizeof(tmp), step->dst_format.id));
				src = step->dst_fmt_index;
			}
		}

		if (ast_strlen_zero(ast_str_buffer(str))) {
			ast_str_set(&str, 0, "No Translation Path");
		}
		ast_cli(a->fd, "\t%-10.10s To %-10.10s: %-60.60s\n", a->argv[4], format_list[i].name, ast_str_buffer(str));
	}
	AST_RWLIST_UNLOCK(&translators);

	ast_format_list_destroy(format_list);
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
			"       2. 'core show translation paths [codec]'\n"
			"           This will display all the translation paths associated with a codec\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 3) {
			return ast_cli_complete(a->word, option, a->n);
		}
		if (a->pos == 4 && !strcasecmp(a->argv[3], option[1])) {
			return complete_trans_path_choice(a->line, a->word, a->pos, a->n);
		}
		return NULL;
	}

	if (a->argc > 5)
		return CLI_SHOWUSAGE;

	if (a->argv[3] && !strcasecmp(a->argv[3], option[1]) && a->argc == 5) { /* show paths */
		return handle_show_translation_path(a);
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

	if (add_format2index(t->src_format.id) || add_format2index(t->dst_format.id)) {
		if (matrix_resize(0)) {
			ast_log(LOG_WARNING, "Translator matrix can not represent any more translators.  Out of resources.\n");
			return -1;
		}
		add_format2index(t->src_format.id);
		add_format2index(t->dst_format.id);
	}

	if (!mod) {
		ast_log(LOG_WARNING, "Missing module pointer, you need to supply one\n");
		return -1;
	}

	if (!t->buf_size) {
		ast_log(LOG_WARNING, "empty buf size, you need to supply one\n");
		return -1;
	}
	if (!t->table_cost && !(t->table_cost = generate_table_cost(&t->src_format, &t->dst_format))) {
		ast_log(LOG_WARNING, "Table cost could not be generated for %s, "
			"Please set table_cost variable on translator.\n", t->name);
		return -1;
	}

	t->module = mod;
	t->src_fmt_index = format2index(t->src_format.id);
	t->dst_fmt_index = format2index(t->dst_format.id);
	t->active = 1;

	if (t->src_fmt_index == -1 || t->dst_fmt_index == -1) {
		ast_log(LOG_WARNING, "Invalid translator path: (%s codec is not valid)\n", t->src_fmt_index == -1 ? "starting" : "ending");
		return -1;
	}
	if (t->src_fmt_index >= cur_max_index) {
		ast_log(LOG_WARNING, "Source format %s is larger than cur_max_index\n", ast_getformatname(&t->src_format));
		return -1;
	}

	if (t->dst_fmt_index >= cur_max_index) {
		ast_log(LOG_WARNING, "Destination format %s is larger than cur_max_index\n", ast_getformatname(&t->dst_format));
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

	ast_verb(2, "Registered translator '%s' from format %s to %s, table cost, %d, computational cost %d\n",
			    term_color(tmp, t->name, COLOR_MAGENTA, COLOR_BLACK, sizeof(tmp)),
			    ast_getformatname(&t->src_format), ast_getformatname(&t->dst_format), t->table_cost, t->comp_cost);

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

	/* if no existing translator was found for this format combination,
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
			ast_verb(2, "Unregistered translator '%s' from format %s to %s\n",
				term_color(tmp, t->name, COLOR_MAGENTA, COLOR_BLACK, sizeof(tmp)),
				ast_getformatname(&t->src_format),
				ast_getformatname(&t->dst_format));
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
	struct ast_format *dst_fmt_out,
	struct ast_format *src_fmt_out)
{
	unsigned int besttablecost = INT_MAX;
	unsigned int beststeps = INT_MAX;
	struct ast_format best;
	struct ast_format bestdst;
	struct ast_format_cap *joint_cap = ast_format_cap_joint(dst_cap, src_cap);
	ast_format_clear(&best);
	ast_format_clear(&bestdst);

	if (joint_cap) { /* yes, pick one and return */
		struct ast_format tmp_fmt;
		ast_format_cap_iter_start(joint_cap);
		while (!ast_format_cap_iter_next(joint_cap, &tmp_fmt)) {
			/* We are guaranteed to find one common format. */
			if (!best.id) {
				ast_format_copy(&best, &tmp_fmt);
				continue;
			}
			/* If there are multiple common formats, pick the one with the highest sample rate */
			if (ast_format_rate(&best) < ast_format_rate(&tmp_fmt)) {
				ast_format_copy(&best, &tmp_fmt);
				continue;
			}

		}
		ast_format_cap_iter_end(joint_cap);

		/* We are done, this is a common format to both. */
		ast_format_copy(dst_fmt_out, &best);
		ast_format_copy(src_fmt_out, &best);
		ast_format_cap_destroy(joint_cap);
		return 0;
	} else {      /* No, we will need to translate */
		struct ast_format cur_dst;
		struct ast_format cur_src;
		AST_RWLIST_RDLOCK(&translators);

		ast_format_cap_iter_start(dst_cap);
		while (!ast_format_cap_iter_next(dst_cap, &cur_dst)) {
			ast_format_cap_iter_start(src_cap);
			while (!ast_format_cap_iter_next(src_cap, &cur_src)) {
				int x = format2index(cur_src.id);
				int y = format2index(cur_dst.id);
				if (x < 0 || y < 0) {
					continue;
				}
				if (!matrix_get(x, y) || !(matrix_get(x, y)->step)) {
					continue;
				}
				if (((matrix_get(x, y)->table_cost < besttablecost) || (matrix_get(x, y)->multistep < beststeps))) {
					/* better than what we have so far */
					ast_format_copy(&best, &cur_src);
					ast_format_copy(&bestdst, &cur_dst);
					besttablecost = matrix_get(x, y)->table_cost;
					beststeps = matrix_get(x, y)->multistep;
				}
			}
			ast_format_cap_iter_end(src_cap);
		}

		ast_format_cap_iter_end(dst_cap);
		AST_RWLIST_UNLOCK(&translators);
		if (best.id) {
			ast_format_copy(dst_fmt_out, &bestdst);
			ast_format_copy(src_fmt_out, &best);
			return 0;
		}
		return -1;
	}
}

unsigned int ast_translate_path_steps(struct ast_format *dst_format, struct ast_format *src_format)
{
	unsigned int res = -1;
	int src, dest;
	/* convert bitwise format numbers into array indices */
	src = format2index(src_format->id);
	dest = format2index(dst_format->id);

	if (src == -1 || dest == -1) {
		ast_log(LOG_WARNING, "No translator path: (%s codec is not valid)\n", src == -1 ? "starting" : "ending");
		return -1;
	}
	AST_RWLIST_RDLOCK(&translators);

	if (matrix_get(src, dest)->step) {
		res = matrix_get(src, dest)->multistep + 1;
	}

	AST_RWLIST_UNLOCK(&translators);

	return res;
}

void ast_translate_available_formats(struct ast_format_cap *dest, struct ast_format_cap *src, struct ast_format_cap *result)
{
	struct ast_format tmp_fmt;
	struct ast_format cur_dest, cur_src;
	int src_audio = 0;
	int src_video = 0;
	int index;

	ast_format_cap_iter_start(dest);
	while (!ast_format_cap_iter_next(dest, &cur_dest)) {
		/* We give preference to a joint format structure if possible */
		if (ast_format_cap_get_compatible_format(src, &cur_dest, &tmp_fmt)) {
			ast_format_cap_add(result, &tmp_fmt);
		} else {
			/* Otherwise we just use the destination format */
			ast_format_cap_add(result, &cur_dest);
		}
	}
	ast_format_cap_iter_end(dest);

	/* if we don't have a source format, we just have to try all
	   possible destination formats */
	if (!src) {
		return;
	}

	ast_format_cap_iter_start(src);
	while (!ast_format_cap_iter_next(src, &cur_src)) {
		/* If we have a source audio format, get its format index */
		if (AST_FORMAT_GET_TYPE(cur_src.id) == AST_FORMAT_TYPE_AUDIO) {
			src_audio = format2index(cur_src.id);
		}

		/* If we have a source video format, get its format index */
		if (AST_FORMAT_GET_TYPE(cur_src.id) == AST_FORMAT_TYPE_VIDEO) {
			src_video = format2index(cur_src.id);
		}

		AST_RWLIST_RDLOCK(&translators);

		/* For a given source audio format, traverse the list of
		   known audio formats to determine whether there exists
		   a translation path from the source format to the
		   destination format. */
		for (index = 0; (src_audio >= 0) && index < cur_max_index; index++) {
			ast_format_set(&tmp_fmt, index2format(index), 0);

			if (AST_FORMAT_GET_TYPE(tmp_fmt.id) != AST_FORMAT_TYPE_AUDIO) {
				continue;
			}

			/* if this is not a desired format, nothing to do */
			if (!ast_format_cap_iscompatible(dest, &tmp_fmt)) {
				continue;
			}

			/* if the source is supplying this format, then
			   we can leave it in the result */
			if (ast_format_cap_iscompatible(src, &tmp_fmt)) {
				continue;
			}

			/* if we don't have a translation path from the src
			   to this format, remove it from the result */
			if (!matrix_get(src_audio, index)->step) {
				ast_format_cap_remove_byid(result, tmp_fmt.id);
				continue;
			}

			/* now check the opposite direction */
			if (!matrix_get(index, src_audio)->step) {
				ast_format_cap_remove_byid(result, tmp_fmt.id);
			}
		}

		/* For a given source video format, traverse the list of
		   known video formats to determine whether there exists
		   a translation path from the source format to the
		   destination format. */
		for (index = 0; (src_video >= 0) && index < cur_max_index; index++) {
			ast_format_set(&tmp_fmt, index2format(index), 0);
			if (AST_FORMAT_GET_TYPE(tmp_fmt.id) != AST_FORMAT_TYPE_VIDEO) {
				continue;
			}

			/* if this is not a desired format, nothing to do */
			if (!ast_format_cap_iscompatible(dest, &tmp_fmt)) {
				continue;
			}

			/* if the source is supplying this format, then
			   we can leave it in the result */
			if (ast_format_cap_iscompatible(src, &tmp_fmt)) {
				continue;
			}

			/* if we don't have a translation path from the src
			   to this format, remove it from the result */
			if (!matrix_get(src_video, index)->step) {
				ast_format_cap_remove_byid(result, tmp_fmt.id);
				continue;
			}

			/* now check the opposite direction */
			if (!matrix_get(index, src_video)->step) {
				ast_format_cap_remove_byid(result, tmp_fmt.id);
			}
		}
		AST_RWLIST_UNLOCK(&translators);
	}
	ast_format_cap_iter_end(src);
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
	ast_register_cleanup(translate_shutdown);
	return res;
}
