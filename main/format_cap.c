/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Digium, Inc.
 *
 * David Vossel <dvossel@digium.com>
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
 * \brief Format Capability API
 *
 * \author David Vossel <dvossel@digium.com>
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$");

#include "asterisk/_private.h"
#include "asterisk/version.h"
#include "asterisk/format.h"
#include "asterisk/format_cap.h"
#include "asterisk/frame.h"
#include "asterisk/astobj2.h"
#include "asterisk/utils.h"


struct ast_format_cap {
	/* The capabilities structure is just an ao2 container of ast_formats */
	struct ao2_container *formats;
	struct ao2_iterator it;
	int nolock;
};

/*! format exists within capabilities structure if it is identical to
 * another format, or if the format is a proper subset of another format. */
static int cmp_cb(void *obj, void *arg, int flags)
{
	struct ast_format *format1 = arg;
	struct ast_format *format2 = obj;
	enum ast_format_cmp_res res = ast_format_cmp(format1, format2);

	return ((res == AST_FORMAT_CMP_EQUAL) ||
			(res == AST_FORMAT_CMP_SUBSET)) ?
				CMP_MATCH | CMP_STOP :
				0;
}

static int hash_cb(const void *obj, const int flags)
{
	const struct ast_format *format = obj;
	return format->id;
}

static struct ast_format_cap *cap_alloc_helper(int nolock)
{
	struct ast_format_cap *cap = ast_calloc(1, sizeof(*cap));

	if (!cap) {
		return NULL;
	}
	cap->nolock = nolock ? OBJ_NOLOCK : 0;
	if (!(cap->formats = ao2_container_alloc(283, hash_cb, cmp_cb))) {
		ast_free(cap);
		return NULL;
	}

	return cap;
}

struct ast_format_cap *ast_format_cap_alloc_nolock(void)
{
	return cap_alloc_helper(1);
}

struct ast_format_cap *ast_format_cap_alloc(void)
{
	return cap_alloc_helper(0);
}

void *ast_format_cap_destroy(struct ast_format_cap *cap)
{
	if (!cap) {
		return NULL;
	}
	ao2_ref(cap->formats, -1);
	ast_free(cap);
	return NULL;
}

void ast_format_cap_add(struct ast_format_cap *cap, struct ast_format *format)
{
	struct ast_format *fnew;

	if (!format || !format->id) {
		return;
	}
	if (!(fnew = ao2_alloc(sizeof(struct ast_format), NULL))) {
		return;
	}
	ast_format_copy(fnew, format);
	if (cap->nolock) {
		ao2_link_nolock(cap->formats, fnew);
	} else {
		ao2_link(cap->formats, fnew);
	}
	ao2_ref(fnew, -1);
}

void ast_format_cap_add_all_by_type(struct ast_format_cap *cap, enum ast_format_type type)
{
	int x;
	size_t f_len = 0;
	struct ast_format tmp_fmt;
	const struct ast_format_list *f_list = ast_get_format_list(&f_len);

	for (x = 0; x < f_len; x++) {
		if (AST_FORMAT_GET_TYPE(f_list[x].id) == type) {
			ast_format_cap_add(cap, ast_format_set(&tmp_fmt, f_list[x].id, 0));
		}
	}
}

void ast_format_cap_add_all(struct ast_format_cap *cap)
{
	int x;
	size_t f_len = 0;
	struct ast_format tmp_fmt;
	const struct ast_format_list *f_list = ast_get_format_list(&f_len);

	for (x = 0; x < f_len; x++) {
		ast_format_cap_add(cap, ast_format_set(&tmp_fmt, f_list[x].id, 0));
	}
}

static int append_cb(void *obj, void *arg, int flag)
{
	struct ast_format_cap *result = (struct ast_format_cap *) arg;
	struct ast_format *format = (struct ast_format *) obj;

	if (!ast_format_cap_iscompatible(result, format)) {
		ast_format_cap_add(result, format);
	}

	return 0;
}

void ast_format_cap_append(struct ast_format_cap *dst, const struct ast_format_cap *src)
{
	ao2_callback(src->formats, OBJ_NODATA | src->nolock, append_cb, dst);
}

static int copy_cb(void *obj, void *arg, int flag)
{
	struct ast_format_cap *result = (struct ast_format_cap *) arg;
	struct ast_format *format = (struct ast_format *) obj;

	ast_format_cap_add(result, format);
	return 0;
}

void ast_format_cap_copy(struct ast_format_cap *dst, const struct ast_format_cap *src)
{
	ast_format_cap_remove_all(dst);
	ao2_callback(src->formats, OBJ_NODATA | src->nolock, copy_cb, dst);
}

struct ast_format_cap *ast_format_cap_dup(const struct ast_format_cap *cap)
{
	struct ast_format_cap *dst;
	if (cap->nolock) {
		dst = ast_format_cap_alloc_nolock();
	} else {
		dst = ast_format_cap_alloc();
	}
	if (!dst) {
		return NULL;
	}
	ao2_callback(cap->formats, OBJ_NODATA | cap->nolock, copy_cb, dst);
	return dst;
}

int ast_format_cap_is_empty(const struct ast_format_cap *cap)
{
	if (!cap) {
		return 1;
	}
	return ao2_container_count(cap->formats) == 0 ? 1 : 0;
}

static int find_exact_cb(void *obj, void *arg, int flag)
{
	struct ast_format *format1 = (struct ast_format *) arg;
	struct ast_format *format2 = (struct ast_format *) obj;

	return (ast_format_cmp(format1, format2) == AST_FORMAT_CMP_EQUAL) ? CMP_MATCH : 0;
}

int ast_format_cap_remove(struct ast_format_cap *cap, struct ast_format *format)
{
	struct ast_format *fremove;
	fremove = ao2_callback(cap->formats, OBJ_POINTER | OBJ_UNLINK | cap->nolock, find_exact_cb, format);

	if (fremove) {
		ao2_ref(fremove, -1);
		return 0;
	}

	return -1;
}

struct multiple_by_id_data {
	struct ast_format *format;
	int match_found;
};

static int multiple_by_id_cb(void *obj, void *arg, int flag)
{
	struct multiple_by_id_data *data = arg;
	struct ast_format *format = obj;
	int res;

	res = (format->id == data->format->id) ? CMP_MATCH : 0;
	if (res) {
		data->match_found = 1;
	}

	return res;
}

int ast_format_cap_remove_byid(struct ast_format_cap *cap, enum ast_format_id id)
{
	struct ast_format format = {
		.id = id,
	};
	struct multiple_by_id_data data = {
		.format = &format,
		.match_found = 0,
	};

	ao2_callback(cap->formats,
		OBJ_NODATA | cap->nolock | OBJ_MULTIPLE | OBJ_UNLINK,
		multiple_by_id_cb,
		&data);

	/* match_found will be set if at least one item was removed */
	if (data.match_found) {
		return 0;
	}

	return -1;
}

static int multiple_by_type_cb(void *obj, void *arg, int flag)
{
	int *type = arg;
	struct ast_format *format = obj;
	return ((AST_FORMAT_GET_TYPE(format->id)) == *type) ? CMP_MATCH : 0;
}

void ast_format_cap_remove_bytype(struct ast_format_cap *cap, enum ast_format_type type)
{
	ao2_callback(cap->formats,
		OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE | cap->nolock,
		multiple_by_type_cb,
		&type);
}

void ast_format_cap_remove_all(struct ast_format_cap *cap)
{
	ao2_callback(cap->formats, OBJ_NODATA | cap->nolock | OBJ_MULTIPLE | OBJ_UNLINK, NULL, NULL);
}

void ast_format_cap_set(struct ast_format_cap *cap, struct ast_format *format)
{
	ast_format_cap_remove_all(cap);
	ast_format_cap_add(cap, format);
}

int ast_format_cap_iscompatible(const struct ast_format_cap *cap, const struct ast_format *format)
{
	struct ast_format *f;
	struct ast_format_cap *tmp_cap = (struct ast_format_cap *) cap;
	f = ao2_find(tmp_cap->formats, (struct ast_format *) format, OBJ_POINTER | tmp_cap->nolock);

	if (f) {
		ao2_ref(f, -1);
		return 1;
	}

	return 0;
}

/*! \internal
 * \brief this struct is just used for the ast_format_cap_joint function so we can provide
 * both a format and a result ast_format_cap structure as arguments to the find_joint_cb
 * ao2 callback function.
 */
struct find_joint_data {
	/*! format to compare to for joint capabilities */
	struct ast_format *format;
	/*! if joint formmat exists with above format, add it to the result container */
	struct ast_format_cap *joint_cap;
	int joint_found;
};

static int find_joint_cb(void *obj, void *arg, int flag)
{
	struct ast_format *format = obj;
	struct find_joint_data *data = arg;

	struct ast_format tmp = { 0, };
	if (!ast_format_joint(format, data->format, &tmp)) {
		if (data->joint_cap) {
			ast_format_cap_add(data->joint_cap, &tmp);
		}
		data->joint_found++;
	}

	return 0;
}

int ast_format_cap_has_joint(const struct ast_format_cap *cap1, const struct ast_format_cap *cap2)
{
	struct ao2_iterator it;
	struct ast_format *tmp;
	struct find_joint_data data = {
		.joint_found = 0,
		.joint_cap = NULL,
	};

	it = ao2_iterator_init(cap1->formats, cap1->nolock ? AO2_ITERATOR_DONTLOCK : 0);
	while ((tmp = ao2_iterator_next(&it))) {
		data.format = tmp;
		ao2_callback(cap2->formats,
			OBJ_MULTIPLE | OBJ_NODATA | cap2->nolock,
			find_joint_cb,
			&data);
		ao2_ref(tmp, -1);
	}
	ao2_iterator_destroy(&it);

	return data.joint_found ? 1 : 0;
}

int ast_format_cap_identical(const struct ast_format_cap *cap1, const struct ast_format_cap *cap2)
{
	struct ao2_iterator it;
	struct ast_format *tmp;

	if (ao2_container_count(cap1->formats) != ao2_container_count(cap2->formats)) {
		return 0; /* if they are not the same size, they are not identical */
	}

	it = ao2_iterator_init(cap1->formats, cap1->nolock ? AO2_ITERATOR_DONTLOCK : 0);
	while ((tmp = ao2_iterator_next(&it))) {
		if (!ast_format_cap_iscompatible(cap2, tmp)) {
			ao2_ref(tmp, -1);
			ao2_iterator_destroy(&it);
			return 0;
		}
		ao2_ref(tmp, -1);
	}
	ao2_iterator_destroy(&it);

	return 1;
}

struct ast_format_cap *ast_format_cap_joint(const struct ast_format_cap *cap1, const struct ast_format_cap *cap2)
{
	struct ao2_iterator it;
	struct ast_format_cap *result = ast_format_cap_alloc_nolock();
	struct ast_format *tmp;
	struct find_joint_data data = {
		.joint_found = 0,
		.joint_cap = result,
	};
	if (!result) {
		return NULL;
	}

	it = ao2_iterator_init(cap1->formats, cap1->nolock ? AO2_ITERATOR_DONTLOCK : 0);
	while ((tmp = ao2_iterator_next(&it))) {
		data.format = tmp;
		ao2_callback(cap2->formats,
			OBJ_MULTIPLE | OBJ_NODATA | cap2->nolock,
			find_joint_cb,
			&data);
		ao2_ref(tmp, -1);
	}
	ao2_iterator_destroy(&it);

	if (ao2_container_count(result->formats)) {
		return result;
	}

	result = ast_format_cap_destroy(result);
	return NULL;
}

static int joint_copy_helper(const struct ast_format_cap *cap1, const struct ast_format_cap *cap2, struct ast_format_cap *result, int append)
{
	struct ao2_iterator it;
	struct ast_format *tmp;
	struct find_joint_data data = {
		.joint_cap = result,
		.joint_found = 0,
	};
	if (!append) {
		ast_format_cap_remove_all(result);
	}
	it = ao2_iterator_init(cap1->formats, cap2->nolock ? AO2_ITERATOR_DONTLOCK : 0);
	while ((tmp = ao2_iterator_next(&it))) {
		data.format = tmp;
		ao2_callback(cap2->formats,
			OBJ_MULTIPLE | OBJ_NODATA | cap2->nolock,
			find_joint_cb,
			&data);
		ao2_ref(tmp, -1);
	}
	ao2_iterator_destroy(&it);

	return ao2_container_count(result->formats) ? 1 : 0;
}

int ast_format_cap_joint_append(const struct ast_format_cap *cap1, const struct ast_format_cap *cap2, struct ast_format_cap *result)
{
	return joint_copy_helper(cap1, cap2, result, 1);
}

int ast_format_cap_joint_copy(const struct ast_format_cap *cap1, const struct ast_format_cap *cap2, struct ast_format_cap *result)
{
	return joint_copy_helper(cap1, cap2, result, 0);
}

struct ast_format_cap *ast_format_cap_get_type(const struct ast_format_cap *cap, enum ast_format_type ftype)
{
	struct ao2_iterator it;
	struct ast_format_cap *result = ast_format_cap_alloc_nolock();
	struct ast_format *tmp;

	if (!result) {
		return NULL;
	}

	/* for each format in cap1, see if that format is
	 * compatible with cap2. If so copy it to the result */
	it = ao2_iterator_init(cap->formats, cap->nolock ? AO2_ITERATOR_DONTLOCK : 0);
	while ((tmp = ao2_iterator_next(&it))) {
		if (AST_FORMAT_GET_TYPE(tmp->id) == ftype) {
			/* copy format */
			ast_format_cap_add(result, tmp);
		}
		ao2_ref(tmp, -1);
	}
	ao2_iterator_destroy(&it);

	if (ao2_container_count(result->formats)) {
		return result;
	}
	result = ast_format_cap_destroy(result);

	/* Remember to always free the NULL before returning it. */
	ast_free(NULL);
	return NULL;
}


int ast_format_cap_has_type(const struct ast_format_cap *cap, enum ast_format_type type)
{
	struct ao2_iterator it;
	struct ast_format *tmp;

	it = ao2_iterator_init(cap->formats, cap->nolock ? AO2_ITERATOR_DONTLOCK : 0);
	while ((tmp = ao2_iterator_next(&it))) {
		if (AST_FORMAT_GET_TYPE(tmp->id) == type) {
			ao2_ref(tmp, -1);
			ao2_iterator_destroy(&it);
			return 1;
		}
		ao2_ref(tmp, -1);
	}
	ao2_iterator_destroy(&it);

	return 0;
}

void ast_format_cap_iter_start(struct ast_format_cap *cap)
{
	if (!cap->nolock) {
		ao2_lock(cap->formats);
	}
	cap->it = ao2_iterator_init(cap->formats, cap->nolock ? AO2_ITERATOR_DONTLOCK : 0);
}

void ast_format_cap_iter_end(struct ast_format_cap *cap)
{
	ao2_iterator_destroy(&cap->it);
	if (!cap->nolock) {
		ao2_unlock(cap->formats);
	}
}

int ast_format_cap_iter_next(struct ast_format_cap *cap, struct ast_format *format)
{
	struct ast_format *tmp = ao2_iterator_next(&cap->it);

	if (!tmp) {
		return -1;
	}
	ast_format_copy(format, tmp);
	ao2_ref(tmp, -1);

	return 0;
}

uint64_t ast_format_cap_to_old_bitfield(const struct ast_format_cap *cap)
{
	uint64_t res = 0;
	struct ao2_iterator it;
	struct ast_format *tmp;

	it = ao2_iterator_init(cap->formats, cap->nolock ? AO2_ITERATOR_DONTLOCK : 0);
	while ((tmp = ao2_iterator_next(&it))) {
		res |= ast_format_to_old_bitfield(tmp);
		ao2_ref(tmp, -1);
	}
	ao2_iterator_destroy(&it);
	return res;
}

void ast_format_cap_from_old_bitfield(struct ast_format_cap *dst, uint64_t src)
{
	uint64_t tmp = 0;
	int x;
	struct ast_format tmp_format = { 0, };

	ast_format_cap_remove_all(dst);
	for (x = 0; x < 64; x++) {
		tmp = (1ULL << x);
		if (tmp & src) {
			ast_format_cap_add(dst, ast_format_from_old_bitfield(&tmp_format, tmp));
		}
	}
}
