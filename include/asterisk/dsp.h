/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
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
 * \brief Convenient Signal Processing routines
 */

#ifndef _ASTERISK_DSP_H
#define _ASTERISK_DSP_H

#define DSP_FEATURE_SILENCE_SUPPRESS	(1 << 0)
#define DSP_FEATURE_BUSY_DETECT		(1 << 1)
#define DSP_FEATURE_DIGIT_DETECT	(1 << 3)
#define DSP_FEATURE_FAX_DETECT		(1 << 4)

#define	DSP_DIGITMODE_DTMF			0				/*!< Detect DTMF digits */
#define DSP_DIGITMODE_MF			1				/*!< Detect MF digits */

#define DSP_DIGITMODE_NOQUELCH		(1 << 8)		/*!< Do not quelch DTMF from in-band */
#define DSP_DIGITMODE_MUTECONF		(1 << 9)		/*!< Mute conference */
#define DSP_DIGITMODE_MUTEMAX		(1 << 10)		/*!< Delay audio by a frame to try to extra quelch */
#define DSP_DIGITMODE_RELAXDTMF		(1 << 11)		/*!< "Radio" mode (relaxed DTMF) */

#define DSP_PROGRESS_TALK		(1 << 16)		/*!< Enable talk detection */
#define DSP_PROGRESS_RINGING		(1 << 17)		/*!< Enable calling tone detection */
#define DSP_PROGRESS_BUSY		(1 << 18)		/*!< Enable busy tone detection */
#define DSP_PROGRESS_CONGESTION		(1 << 19)		/*!< Enable congestion tone detection */
#define DSP_FEATURE_CALL_PROGRESS	(DSP_PROGRESS_TALK | DSP_PROGRESS_RINGING | DSP_PROGRESS_BUSY | DSP_PROGRESS_CONGESTION)
#define DSP_FEATURE_WAITDIALTONE	(1 << 20)		/*!< Enable dial tone detection */
#define DSP_FEATURE_FREQ_DETECT		(1 << 21)		/*!< Enable arbitrary tone detection */

#define DSP_FAXMODE_DETECT_CNG		(1 << 0)
#define DSP_FAXMODE_DETECT_CED		(1 << 1)
#define DSP_FAXMODE_DETECT_SQUELCH	(1 << 2)
#define DSP_FAXMODE_DETECT_ALL	(DSP_FAXMODE_DETECT_CNG | DSP_FAXMODE_DETECT_CED)

#define DSP_TONE_STATE_SILENCE  0
#define DSP_TONE_STATE_RINGING  1
#define DSP_TONE_STATE_DIALTONE 2
#define DSP_TONE_STATE_TALKING  3
#define DSP_TONE_STATE_BUSY     4
#define DSP_TONE_STATE_SPECIAL1	5
#define DSP_TONE_STATE_SPECIAL2 6
#define DSP_TONE_STATE_SPECIAL3 7
#define DSP_TONE_STATE_HUNGUP 	8

struct ast_dsp;

struct ast_dsp_busy_pattern {
	/*! Number of elements. */
	int length;
	/*! Pattern elements in on/off time durations. */
	int pattern[4];
};

enum threshold {
	/* Array offsets */
	THRESHOLD_SILENCE = 0,
	/* Always the last */
	THRESHOLD_MAX = 1,
};

/*! \brief Allocates a new dsp with a specific internal sample rate used
 * during processing. */
struct ast_dsp *ast_dsp_new_with_rate(unsigned int sample_rate);

/*! \brief Allocates a new dsp, assumes 8khz for internal sample rate */
struct ast_dsp *ast_dsp_new(void);

void ast_dsp_free(struct ast_dsp *dsp);

/*! \brief Retrieve the sample rate this DSP structure was
 * created with */
unsigned int ast_dsp_get_sample_rate(const struct ast_dsp *dsp);

/*! \brief Set the minimum average magnitude threshold to determine talking by the DSP. */
void ast_dsp_set_threshold(struct ast_dsp *dsp, int threshold);

/*! \brief Set number of required cadences for busy */
void ast_dsp_set_busy_count(struct ast_dsp *dsp, int cadences);

/*! \brief Set expected lengths of the busy tone */
void ast_dsp_set_busy_pattern(struct ast_dsp *dsp, const struct ast_dsp_busy_pattern *cadence);

/*! \brief Scans for progress indication in audio */
int ast_dsp_call_progress(struct ast_dsp *dsp, struct ast_frame *inf);

/*! \brief Set zone for doing progress detection */
int ast_dsp_set_call_progress_zone(struct ast_dsp *dsp, char *zone);

/*! \brief Return AST_FRAME_NULL frames when there is silence, AST_FRAME_BUSY on
   busies, and call progress, all dependent upon which features are enabled */
struct ast_frame *ast_dsp_process(struct ast_channel *chan, struct ast_dsp *dsp, struct ast_frame *inf);

/*!
 * \brief Process the audio frame for silence.
 *
 * \param dsp DSP processing audio media.
 * \param f Audio frame to process.
 * \param totalsilence Variable to set to the total accumulated silence in ms
 * seen by the DSP since the last noise.
 *
 * \return Non-zero if the frame is silence.
 */
int ast_dsp_silence(struct ast_dsp *dsp, struct ast_frame *f, int *totalsilence);

/*!
 * \brief Process the audio frame for silence.
 *
 * \param dsp DSP processing audio media.
 * \param f Audio frame to process.
 * \param totalsilence Variable to set to the total accumulated silence in ms
 * seen by the DSP since the last noise.
 * \param frames_energy Variable to set to the average energy of the samples in the frame.
 *
 * \return Non-zero if the frame is silence.
 */
int ast_dsp_silence_with_energy(struct ast_dsp *dsp, struct ast_frame *f, int *totalsilence, int *frames_energy);

/*!
 * \brief Process the audio frame for noise.
 * \since 1.6.1
 *
 * \param dsp DSP processing audio media.
 * \param f Audio frame to process.
 * \param totalnoise Variable to set to the total accumulated noise in ms
 * seen by the DSP since the last silence.
 *
 * \return Non-zero if the frame is silence.
 */
int ast_dsp_noise(struct ast_dsp *dsp, struct ast_frame *f, int *totalnoise);

/*! \brief Return non-zero if historically this should be a busy, request that
  ast_dsp_silence has already been called */
int ast_dsp_busydetect(struct ast_dsp *dsp);

/*! \brief Return non-zero if DTMF hit was found */
int ast_dsp_digitdetect(struct ast_dsp *dsp, struct ast_frame *f);

/*! \brief Reset total silence count */
void ast_dsp_reset(struct ast_dsp *dsp);

/*! \brief Reset DTMF detector */
void ast_dsp_digitreset(struct ast_dsp *dsp);

/*! \brief Select feature set */
void ast_dsp_set_features(struct ast_dsp *dsp, int features);

/*! \brief Get features */
int ast_dsp_get_features(struct ast_dsp *dsp);

/*! \brief Get pending DTMF/MF digits */
int ast_dsp_getdigits(struct ast_dsp *dsp, char *buf, int max);

/*! \brief Set digit mode
 * \version 1.6.1 renamed from ast_dsp_digitmode to ast_dsp_set_digitmode
 */
int ast_dsp_set_digitmode(struct ast_dsp *dsp, int digitmode);

/*! \brief Set arbitrary frequency detection mode */
int ast_dsp_set_freqmode(struct ast_dsp *dsp, int freq, int dur, int db, int squelch);

/*! \brief Set fax mode */
int ast_dsp_set_faxmode(struct ast_dsp *dsp, int faxmode);

/*!
 * \brief Returns true if DSP code was muting any fragment of the last processed frame.
 * Muting (squelching) happens when DSP code removes DTMF/MF/generic tones from the audio
 * \since 1.6.1
 */
int ast_dsp_was_muted(struct ast_dsp *dsp);

/*! \brief Get tstate (Tone State) */
int ast_dsp_get_tstate(struct ast_dsp *dsp);

/*! \brief Get tcount (Threshold counter) */
int ast_dsp_get_tcount(struct ast_dsp *dsp);

/*!
 * \brief Get silence threshold from dsp.conf
 * \since 1.6.1
 */
int ast_dsp_get_threshold_from_settings(enum threshold which);

#endif /* _ASTERISK_DSP_H */
