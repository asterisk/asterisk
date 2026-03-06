/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2026, Asterisk Community
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
 * \brief Codec Translation Roundtrip Tests
 *
 * \author Sebastian Jennen <sebastian.t.jennen@gmail.com>
 *
 * Tests that encoding sample frames through a codec and decoding them
 * back to slin produces output that is (almost) identical to
 * the original input. This verifies that each codec translator pair
 * (slin -> codec -> slin) does not corrupt or destroy the audio signal.
 *
 * For near-lossless codecs (alaw, ulaw) the tolerance is very tight.
 * For lossy codecs (adpcm, g726, gsm, g722, speex, ilbc, opus, g729, etc.)
 * a Signal-to-Noise Ratio (SNR) threshold is used since some degradation
 * is inherent to the compression algorithm.  Wideband codecs are tested
 * with a matching slin16/slin32/slin48 signal at the codec's native
 * sample rate.
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include <math.h>

#include "asterisk/module.h"
#include "asterisk/test.h"
#include "asterisk/translate.h"
#include "asterisk/format.h"
#include "asterisk/format_cap.h"
#include "asterisk/format_cache.h"
#include "asterisk/codec.h"
#include "asterisk/frame.h"
#include "asterisk/slin.h"
#include "asterisk/logger.h"
#include "asterisk/utils.h"

/*! Duration of the test signal in seconds */
#define TEST_DURATION_SECS  2

/*! Chunk duration in milliseconds to feed chunks to the coders */
#define CHUNK_MS  60

/*! Minimum acceptable SNR in dB for lossy codecs.
 *  Most telephony codecs at 8 kHz should exceed ~20 dB */
#define MIN_SNR_LOSSY_DB   15.0

/*! Maximum per-sample absolute error for near-lossless codecs (ulaw/alaw).
 *  μ-law/A-law quantisation can reach a max error of 256. */
#define MAX_SAMPLE_ERR_LOSSLESS  256

/*! Minimum fraction of input samples that must survive the roundtrip.
 *  Codec lookahead + trailing partial frame may consume some samples;
 *  require at least 90 % to call the test valid. */
#define MIN_DECODED_RATIO  0.90

/*!
 * \brief Generate a synthetic speech-like test signal in linear sample.
 *
 * a mix of a 200 Hz fundamental and an 800 Hz harmonic with a slow 4 Hz
 * amplitude-modulation envelope, loosely inspired by ITU-T P.50 artificial
 * voice.
 *
 * \param[out] buf         Buffer to fill (must hold at least \a samples int16 values)
 * \param[in]  samples     Number of samples to generate
 * \param[in]  sample_rate Sampling rate in Hz (used to scale time correctly)
 */
static void generate_speech_signal(int16_t *buf, int samples, int sample_rate)
{
	int i;
	for (i = 0; i < samples; i++) {
		double t = (double)i / sample_rate;
		double sig = 0.6 * sin(2.0 * M_PI * 200.0 * t)
		           + 0.4 * sin(2.0 * M_PI * 800.0 * t);
		/* Slow AM at 4 Hz to simulate syllable rhythm */
		sig *= 0.5 * (1.0 + sin(2.0 * M_PI * 4.0 * t));
		buf[i] = (int16_t)(sig * 16000.0);
	}
}

/*!
 * \brief Compute Signal-to-Noise Ratio between original and roundtripped signal.
 *
 * \param[in] orig       Original sample buffer
 * \param[in] roundtrip  Decoded (roundtripped) sample buffer
 * \param[in] samples    Number of samples
 *
 * \return SNR in dB
 */
static double compute_snr(const int16_t *orig, const int16_t *roundtrip, int samples)
{
	double signal_power = 0.0;
	double noise_power  = 0.0;
	int i;

	for (i = 0; i < samples; i++) {
		double s = (double)orig[i];
		double n = (double)(orig[i] - roundtrip[i]);
		signal_power += s * s;
		noise_power  += n * n;
	}

	if (signal_power < 1.0) {
		return -100.0; /* degenerate signal */
	}
	if (noise_power < 1.0) {
		return 999.0; /* essentially perfect */
	}

	return 10.0 * log10(signal_power / noise_power);
}

/*!
 * \brief Compute SNR after aligning the decoded signal via cross-correlation.
 *
 * Some codecs (e.g. speex, opus) introduce an algorithmic lookahead delay:
 * the decoded signal is time-shifted by a fixed number of samples relative to
 * the original.  Computing SNR without compensating for this shift yields a
 * near-zero result even when the codec works perfectly.
 *
 * This function searches delays d = 0..max_delay, finds the integer shift
 * that maximises the cross-correlation sum orig[i]·decoded[i+d], then returns
 * the SNR computed at that optimal alignment.
 *
 * \param[in]  orig       Original sample buffer (must hold at least \a samples values)
 * \param[in]  decoded    Decoded sample buffer (must hold at least \a samples values)
 * \param[in]  samples    Number of samples available in each buffer
 * \param[in]  max_delay  Search range: delays 0..max_delay are tested
 * \param[out] delay_out  Receives the best-fit delay found; may be NULL
 *
 * \return SNR in dB at the best-fit alignment, or the unaligned SNR when
 *         \a samples <= \a max_delay
 */
static double compute_snr_aligned(const int16_t *orig, const int16_t *decoded,
	int samples, int max_delay, int *delay_out)
{
	double best_corr = -1e300;
	int best_delay = 0;
	int d;

	if (delay_out) {
		*delay_out = 0;
	}

	/* Need enough samples for a meaningful search */
	if (samples <= max_delay || max_delay <= 0) {
		return compute_snr(orig, decoded, samples);
	}

	for (d = 0; d <= max_delay; d++) {
		int n = samples - d;
		double corr = 0.0;
		int i;
		for (i = 0; i < n; i++) {
			corr += (double)orig[i] * (double)decoded[i + d];
		}
		if (corr > best_corr) {
			best_corr = corr;
			best_delay = d;
		}
	}

	if (delay_out) {
		*delay_out = best_delay;
	}

	/* Compute SNR at the best alignment */
	return compute_snr(orig, decoded + best_delay, samples - best_delay);
}

/*!
 * \brief Compute maximum absolute per-sample error.
 */
static int compute_max_error(const int16_t *orig, const int16_t *roundtrip, int samples)
{
	int max_err = 0;
	int i;

	for (i = 0; i < samples; i++) {
		int err = abs((int)orig[i] - (int)roundtrip[i]);
		if (err > max_err) {
			max_err = err;
		}
	}
	return max_err;
}

/*!
 * \brief Attempt a roundtrip encode/decode for one codec format.
 *
 * Feeds the original slin signal through the encoder and decoder in 20 ms
 * chunks (matching the universal VoIP frame duration), accumulates the
 * decoded output, and compares the full result against the original.
 * This naturally respects every codec's internal buffer sizes and frame
 * granularity without needing per-codec sample-count overrides.
 *
 * \param[in]  test         Test framework handle (for status messages)
 * \param[in]  slin_fmt     The slin format appropriate for the codec's sample rate
 * \param[in]  target_fmt   The codec format to test
 * \param[in]  orig_buf     Original slin sample buffer
 * \param[in]  total_samples Number of samples in orig_buf
 * \param[in]  sample_rate  Sample rate in Hz
 * \param[in]  is_lossy     If non-zero, use SNR-based comparison; otherwise use max-error
 * \param[in]  min_snr_db   Minimum acceptable SNR for lossy check (ignored when !is_lossy)
 *
 * \retval AST_TEST_PASS on success
 * \retval AST_TEST_FAIL on failure
 */
static enum ast_test_result_state check_codec(
	struct ast_test *test,
	struct ast_format *slin_fmt,
	struct ast_format *target_fmt,
	const int16_t *orig_buf,
	int total_samples,
	int sample_rate,
	int is_lossy,
	double min_snr_db)
{
	struct ast_trans_pvt *encode_path = NULL;
	struct ast_trans_pvt *decode_path = NULL;
	int16_t *decoded_buf = NULL;
	int total_decoded = 0;
	int chunk_samples = sample_rate * CHUNK_MS / 1000;
	int buf_capacity;
	int offset;
	enum ast_test_result_state result = AST_TEST_FAIL;
	const char *codec_name;

	codec_name = ast_format_get_name(target_fmt);

	/* --- Build encoder path: slin -> codec --- */
	encode_path = ast_translator_build_path(target_fmt, slin_fmt);
	if (!encode_path) {
		ast_test_status_update(test,
			"Skipping %s: no translation path from slin to %s\n",
			codec_name, codec_name);
		return AST_TEST_PASS;
	}

	/* --- Build decoder path: codec -> slin --- */
	decode_path = ast_translator_build_path(slin_fmt, target_fmt);
	if (!decode_path) {
		ast_test_status_update(test,
			"FAIL %s: found encoder but no decoder path back to slin\n",
			codec_name);
		goto cleanup;
	}

	/* Allocate output buffer with some headroom for codec expansion */
	buf_capacity = total_samples + chunk_samples;
	decoded_buf = ast_calloc(buf_capacity, sizeof(int16_t));
	if (!decoded_buf) {
		goto cleanup;
	}

	/* --- Feed audio in CHUNK_MS chunks through encode -> decode --- */
	for (offset = 0; offset < total_samples; offset += chunk_samples) {
		struct ast_frame input_frame;
		struct ast_frame *encoded;
		struct ast_frame *decoded;
		struct ast_frame *cur;
		int feed = total_samples - offset;
		if (feed > chunk_samples) {
			feed = chunk_samples;
		}

		memset(&input_frame, 0, sizeof(input_frame));
		input_frame.frametype       = AST_FRAME_VOICE;
		input_frame.subclass.format = slin_fmt;
		input_frame.datalen         = feed * sizeof(int16_t);
		input_frame.samples         = feed;
		input_frame.data.ptr        = (void *)(orig_buf + offset);
		input_frame.mallocd         = 0;
		input_frame.src             = "test_codec_translations";

		/* Encode: slin -> codec.  NULL means the encoder is buffering. */
		encoded = ast_translate(encode_path, &input_frame, 0);
		if (!encoded) {
			continue;
		}

		/* Decode: codec -> slin.  ast_translate follows the frame linked
		 * list internally, so passing the head feeds all encoded frames. */
		decoded = ast_translate(decode_path, encoded, 0);
		if (!decoded) {
			continue;
		}

		/* Copy decoded samples into our accumulation buffer.
		 * Walk the linked list in case frameout produced multiple frames. */
		for (cur = decoded; cur; cur = AST_LIST_NEXT(cur, frame_list)) {
			int copy;
			if (cur->frametype != AST_FRAME_VOICE || !cur->data.ptr
				|| cur->samples <= 0) {
				continue;
			}
			copy = cur->samples;
			if (total_decoded + copy > buf_capacity) {
				copy = buf_capacity - total_decoded;
			}
			memcpy(decoded_buf + total_decoded, cur->data.ptr,
				copy * sizeof(int16_t));
			total_decoded += copy;
		}
	}

	/* --- Verify we got enough decoded audio --- */
	if (total_decoded < (int)(total_samples * MIN_DECODED_RATIO)) {
		ast_test_status_update(test,
			"FAIL %s: decoded only %d of %d samples (%.0f%%)\n",
			codec_name, total_decoded, total_samples,
			100.0 * total_decoded / total_samples);
		goto cleanup;
	}

	/* --- Compare original vs decoded --- */
	{
		int cmp_samples = total_decoded < total_samples
			? total_decoded : total_samples;

		if (is_lossy) {
			int max_delay = sample_rate * 20 / 1000;  /* 20 ms search */
			int delay = 0;
			double snr = compute_snr_aligned(orig_buf, decoded_buf,
				cmp_samples, max_delay, &delay);

			ast_test_status_update(test,
				"  %s (lossy): SNR = %.1f dB (threshold %.1f dB)"
				" [%d/%d samples, delay=%d/%.1fms]\n",
				codec_name, snr, min_snr_db,
				cmp_samples, total_samples,
				delay, 1000.0 * delay / sample_rate);

			if (snr < min_snr_db) {
				ast_test_status_update(test,
					"FAIL %s: SNR %.1f dB is below minimum %.1f dB\n",
					codec_name, snr, min_snr_db);
				goto cleanup;
			}
		} else {
			int max_err = compute_max_error(orig_buf, decoded_buf, cmp_samples);
			double snr  = compute_snr(orig_buf, decoded_buf, cmp_samples);

			ast_test_status_update(test,
				"  %s (lossless): max_err = %d (limit %d),"
				" SNR = %.1f dB [%d/%d samples]\n",
				codec_name, max_err, MAX_SAMPLE_ERR_LOSSLESS, snr,
				cmp_samples, total_samples);

			if (max_err > MAX_SAMPLE_ERR_LOSSLESS) {
				ast_test_status_update(test,
					"FAIL %s: max sample error %d exceeds limit %d\n",
					codec_name, max_err, MAX_SAMPLE_ERR_LOSSLESS);
				goto cleanup;
			}
		}
	}

	result = AST_TEST_PASS;

cleanup:
	ast_free(decoded_buf);
	if (encode_path) {
		ast_translator_free_path(encode_path);
	}
	if (decode_path) {
		ast_translator_free_path(decode_path);
	}

	return result;
}

/*!
 * \brief Codec roundtrip entry: table of codecs to test.
 */
struct codec_test_entry {
	const char *format_name;   /*!< Name used in ast_format_cache_get() */
	int is_lossy;              /*!< 1 = lossy (use SNR check), 0 = near-lossless (use max err) */
	double min_snr_db;         /*!< Per-codec SNR floor; 0.0 = use MIN_SNR_LOSSY_DB default */
	int sample_rate;           /*!< Native slin rate: 8000/16000/32000/48000; 0 = default 8000 */
};

/*! Table of codecs to roundtrip-test.*/
static const struct codec_test_entry codec_table[] = {
	/* Near-lossless narrowband (8 kHz) — verified with max per-sample error */
	{ "ulaw",      0 },
	{ "alaw",      0 },

	/* Lossy narrowband (8 kHz) — verified with SNR threshold */
	{ "adpcm",     1 },          /* ADPCM: ~20 dB SNR, lossy by design */
	{ "g726",      1 },          /* G.726 ADPCM: ~28 dB SNR, lossy by design */
	{ "g726aal2",  1 },          /* G.726 AAL2 ADPCM: same as g726 */
	{ "gsm",       1 },
	{ "speex",     1, 7.0 },     /* speex is quite lossy */
	{ "ilbc",      1, 7.0 },     /* 30 ms frames: coarser quantisation lowers SNR floor */
	{ "codec2",    1, -2.0 },    /* vocoder: snr is really low, smoke-test only */
	{ "lpc10",     1, -2.0 },    /* vocoder: snr is really low, smoke-test only */

	/* { "g729",      1 },             UNTESTED yet */
	/* { "silk8",     1 },             UNTESTED yet */
	/* { "silk12",    1 },             UNTESTED yet */
	/* { "silk16",    1 },             UNTESTED yet */
	/* { "silk24",    1 },             UNTESTED yet */

	/* Wideband (16 kHz) — tested with slin16 signal. */
	{ "g722",      1, 0.0, 16000 },
	{ "speex16",   1, 5.0, 16000 },

	/* Ultra-wideband (32 kHz) — tested with slin32 signal */
	{ "speex32",   1, 5.0, 32000 },

	/* Opus native rate is 48 kHz */
	{ "opus",      1, 8.0, 48000 },
};

AST_TEST_DEFINE(codec_translate_roundtrip)
{
	int i;
	int tested = 0;
	int failed = 0;
	enum ast_test_result_state overall = AST_TEST_PASS;

	switch (cmd) {
	case TEST_INIT:
		info->name = "codec_translations";
		info->category = "/main/codec/";
		info->summary = "Roundtrip encode/decode test and quality check for various codecs referenced in this test and present in the installation";
		info->description =
			"Generates a synthetic speech-like signal (200 Hz +\n"
			"800 Hz with 4 Hz AM envelope) at the codec's native sample\n"
			"rate, feeds it through the codec and back,\n"
			"then verifies the output quality over the full duration.\n"
			"For near-lossless codecs (ulaw, alaw) it checks that the\n"
			"maximum per-sample error is within a tight bound.\n"
			"For lossy codecs it checks that the SNR exceeds a per-codec\n"
			"minimum threshold.  Vocoders use a near-zero threshold\n"
			"(smoke test).";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_status_update(test,
		"Starting codec roundtrip tests (%d codecs, %d seconds of audio)\n",
		(int)ARRAY_LEN(codec_table), TEST_DURATION_SECS);

	for (i = 0; i < (int)ARRAY_LEN(codec_table); i++) {
		struct ast_format *slin_fmt;
		struct ast_format *target_fmt;
		int16_t *orig_buf;
		enum ast_test_result_state res;
		int rate = codec_table[i].sample_rate > 0
			? codec_table[i].sample_rate : 8000;
		int total_samples = rate * TEST_DURATION_SECS;

		/* Select the slin format for this codec's native rate */
		switch (rate) {
			case 16000:
			    slin_fmt = ast_format_slin16;
			    break;
			case 32000:
			    slin_fmt = ast_format_slin32;
			    break;
			case 48000:
			    slin_fmt = ast_format_slin48;
			    break;
			default:
			    slin_fmt = ast_format_slin;
			    break;
		}

		target_fmt = ast_format_cache_get(codec_table[i].format_name);
		if (!target_fmt) {
			ast_test_status_update(test,
				"  %s: format not in cache (module not loaded?), skipping\n",
				codec_table[i].format_name);
			continue;
		}

		orig_buf = ast_malloc(total_samples * sizeof(int16_t));
		if (!orig_buf) {
			ao2_ref(target_fmt, -1);
			overall = AST_TEST_FAIL;
			break;
		}

		generate_speech_signal(orig_buf, total_samples, rate);

		res = check_codec(
			test, slin_fmt, target_fmt, orig_buf,
			total_samples, rate,
			codec_table[i].is_lossy,
			codec_table[i].min_snr_db != 0.0
				? codec_table[i].min_snr_db : MIN_SNR_LOSSY_DB);

		ast_free(orig_buf);
		ao2_ref(target_fmt, -1);

		tested++;
		if (res == AST_TEST_FAIL) {
			failed++;
			overall = AST_TEST_FAIL;
		}
	}

	ast_test_status_update(test,
		"\nCodec roundtrip summary: %d tested, %d passed, %d failed\n",
		tested, tested - failed, failed);

	if (tested == 0) {
		ast_test_status_update(test,
			"WARNING: No codecs were available to test. "
			"Ensure codec modules are loaded.\n");
	}

	return overall;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(codec_translate_roundtrip);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(codec_translate_roundtrip);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD_EXTENDED(ASTERISK_GPL_KEY,
	"Codec Translation Roundtrip Tests");
