/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Lorenzo Miniero <lorenzo@meetecho.com>
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
 * \brief Codec opus externals and format attributes
 *
 * RFC - https://tools.ietf.org/rfc/rfc7587.txt
 */
#ifndef _AST_FORMAT_OPUS_H_
#define _AST_FORMAT_OPUS_H_

/*! \brief Maximum sampling rate an endpoint is capable of receiving */
#define CODEC_OPUS_ATTR_MAX_PLAYBACK_RATE "maxplaybackrate"
/*! \brief An alias for maxplaybackrate (used in older versions) */
#define CODEC_OPUS_ATTR_MAX_CODED_AUDIO_BANDWIDTH "maxcodedaudiobandwidth"
/*! \brief Maximum sampling rate an endpoint is capable of sending */
#define CODEC_OPUS_ATTR_SPROP_MAX_CAPTURE_RATE "sprop-maxcapturerate"
/*! \brief Maximum duration of packet (in milliseconds) */
#define CODEC_OPUS_ATTR_MAX_PTIME "maxptime"
/*! \brief Duration of packet (in milliseconds) */
#define CODEC_OPUS_ATTR_PTIME "ptime"
/*! \brief Maximum average received bit rate (in bits per second) */
#define CODEC_OPUS_ATTR_MAX_AVERAGE_BITRATE "maxaveragebitrate"
/*! \brief Decode stereo (1) vs mono (0) */
#define CODEC_OPUS_ATTR_STEREO "stereo"
/*! \brief Likeliness of sender producing stereo (1) vs mono (0) */
#define CODEC_OPUS_ATTR_SPROP_STEREO "sprop-stereo"
/*! \brief Decoder prefers a constant (1) vs variable (0) bitrate */
#define CODEC_OPUS_ATTR_CBR "cbr"
/*! \brief Use forward error correction (1) or not (0) */
#define CODEC_OPUS_ATTR_FEC "useinbandfec"
/*! \brief Use discontinuous transmission (1) or not (0) */
#define CODEC_OPUS_ATTR_DTX "usedtx"
/*! \brief Custom data object */
#define CODEC_OPUS_ATTR_DATA "data"

/*! \brief Default attribute values */
#define CODEC_OPUS_DEFAULT_SAMPLE_RATE 48000
#define CODEC_OPUS_DEFAULT_MAX_PLAYBACK_RATE 48000
#define CODEC_OPUS_DEFAULT_MAX_PTIME 120
#define CODEC_OPUS_DEFAULT_PTIME 20
#define CODEC_OPUS_DEFAULT_BITRATE -1000 /* OPUS_AUTO */
#define CODEC_OPUS_DEFAULT_CBR 0
#define CODEC_OPUS_DEFAULT_FEC 0
#define CODEC_OPUS_DEFAULT_DTX 0
#define CODEC_OPUS_DEFAULT_STEREO 0

#endif /* _AST_FORMAT_OPUS_H */
