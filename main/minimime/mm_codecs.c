/*
 * $Id$
 *
 * MiniMIME - a library for handling MIME messages
 *
 * Copyright (C) 2003 Jann Fischer <rezine@mistrust.net>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of the contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY JANN FISCHER AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL JANN FISCHER OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <assert.h>

#include "mm_internal.h"
#include "mm_util.h"

extern struct mm_codecs codecs;

/** @file mm_codecs.c
 *
 * This module contains functions to manipulate MiniMIME codecs
 *
 */

/** @defgroup codecs Manipulating MiniMIME codecs */

/** @{
 * @name Codec manipulation 
 */ 

/**
 * Looks up whether a context has an decoder installed for a given encoding
 *
 * @param encoding The encoding specifier to look up
 * @return 1 if a decoder is installed or 0 if not
 * @ingroup codecs
 */
int
mm_codec_hasdecoder(const char *encoding)
{
	struct mm_codec *codec;

	assert(encoding != NULL);

	SLIST_FOREACH(codec, &codecs, next) {
		assert(codec->encoding != NULL);
		if (!strcasecmp(codec->encoding, encoding)) {
			if (codec->decoder != NULL)
				return 1;
			else
				return 0;
		}
	}

	return 0;
}

/**
 * Looks up whether a context has an encoder installed for a given encoding
 * 
 * @param ctx A valid MIME context
 * @param encoding The encoding specifier to look up
 * @return 1 if an encoder is installed or 0 if not
 * @ingroup codecs
 */
int
mm_codec_hasencoder(const char *encoding)
{
	struct mm_codec *codec;

	assert(encoding != NULL);

	SLIST_FOREACH(codec, &codecs, next) {
		assert(codec->encoding != NULL);
		if (!strcasecmp(codec->encoding, encoding)) {
			if (codec->encoder != NULL)
				return 1;
			else
				return 0;
		}
	}

	return 0;
}

/**
 * Looks up whether a codec for a given encoding is installed to a context
 *
 * @param encoding The encoding specifier to look up
 * @return 1 if a codec was found or 0 if not
 * @ingroup codecs
 */
int
mm_codec_isregistered(const char *encoding)
{
	struct mm_codec *codec;

	assert(encoding != NULL);

	SLIST_FOREACH(codec, &codecs, next) {
		if (!strcasecmp(codec->encoding, encoding)) {
			return 1;
		}
	}

	return 0;
}

/**
 * Registers a codec with the MiniMIME library
 *
 * @param encoding The encoding specifier for which to register the codec
 * @param encoder The encoder function for this encoding
 * @param decoder The decoder function for this encoding
 * @return 1 if successfull or 0 if not
 * @ingroup codecs
 *
 * This function registers a codec for a given MiniMIME context. The codec
 * may provide an decoder, an encoder or both (but not none). If there is
 * a codec already installed for this encoding, the function will puke.
 */
int
mm_codec_register(const char *encoding, 
    char *(*encoder)(char *data, uint32_t i),
    char *(*decoder)(char *data))
{
	struct mm_codec *codec;

	assert(encoding != NULL);

	assert(mm_codec_isregistered(encoding) != 1);
	
	codec = (struct mm_codec *)xmalloc(sizeof(struct mm_codec));

	codec->encoding = xstrdup(encoding);
	codec->encoder = encoder;
	codec->decoder = decoder;

	if (SLIST_EMPTY(&codecs)) {
		SLIST_INSERT_HEAD(&codecs, codec, next);
		return 1;
	} else {
		struct mm_codec *lcodec, *tcodec;
		tcodec = NULL;
		SLIST_FOREACH(lcodec, &codecs, next) {
			if (lcodec != NULL)
				tcodec = lcodec;
		}
		assert(tcodec != NULL);
		SLIST_INSERT_AFTER(tcodec, codec, next);
		return 1;
	}
	
	return 0;
}

/**
 * Unregisters a MiniMIME codec 
 *
 * @param encoding The encoding specifier which to unregister
 * @return 0 if unregistered successfully, or -1 if there was no such codec
 * @ingroup codecs
 */
int
mm_codec_unregister(const char *encoding)
{
	struct mm_codec *codec;

	assert(encoding != NULL);

	SLIST_FOREACH(codec, &codecs, next) {
		if (!strcasecmp(codec->encoding, encoding)) {
			xfree(codec->encoding);
			xfree(codec);
			codec = NULL;
			return 0;
		}
	}

	return -1;
}

/**
 * Unregisters all codecs within a context 
 *
 * @param ctx A valid MiniMIME context
 * @return 0 if all codecs were unregistered successfully or -1 if an error
 *	occured.
 * @note Foobar
 */ 
int
mm_codec_unregisterall(void) 
{
	struct mm_codec *codec;

	SLIST_FOREACH(codec, &codecs, next) {
		if (mm_codec_unregister(codec->encoding) == -1) {
			return -1;
		}
	}

	return 0;
}

/**
 * Registers the default codecs to a MiniMIME context
 *
 * This functions registers the codecs for the following encodings to a
 * MiniMIME context:
 *
 *	- Base64
 *	- (TODO:) Quoted-Printable
 */
void
mm_codec_registerdefaultcodecs(void)
{
	mm_codec_register("base64", mm_base64_encode, mm_base64_decode);
}


/** @} */
