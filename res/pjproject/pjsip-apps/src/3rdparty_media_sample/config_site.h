/*
 * Put this file in pjlib/include/pj
 */

/* sample configure command:
   CFLAGS="-g -Wno-unused-label" ./aconfigure --enable-ext-sound --disable-speex-aec --disable-g711-codec --disable-l16-codec --disable-gsm-codec --disable-g722-codec --disable-g7221-codec --disable-speex-codec --disable-ilbc-codec --disable-opencore-amrnb --disable-sdl --disable-ffmpeg --disable-v4l2
 */

#define THIRD_PARTY_MEDIA			1

#if THIRD_PARTY_MEDIA
/*
 * Sample settings for using third party media with pjsua-lib
 */
#	define PJSUA_MEDIA_HAS_PJMEDIA		0
#	define PJMEDIA_HAS_G711_CODEC		0
#	define PJMEDIA_HAS_ALAW_ULAW_TABLE	0
#	define PJMEDIA_RESAMPLE_IMP		PJMEDIA_RESAMPLE_NONE
#	define PJMEDIA_HAS_SPEEX_AEC		0

#	define PJMEDIA_HAS_L16_CODEC		0
#	define PJMEDIA_HAS_GSM_CODEC		0
#	define PJMEDIA_HAS_SPEEX_CODEC		0
#	define PJMEDIA_HAS_ILBC_CODEC		0
#	define PJMEDIA_HAS_G722_CODEC		0
#	define PJMEDIA_HAS_G7221_CODEC		0
#	define PJMEDIA_HAS_OPENCORE_AMRNB_CODEC	0

#	define PJMEDIA_HAS_VIDEO		1
#	define PJMEDIA_HAS_FFMPEG		0

#	undef PJMEDIA_VIDEO_DEV_HAS_SDL
#	define PJMEDIA_VIDEO_DEV_HAS_SDL	0
#	define PJMEDIA_VIDEO_DEV_HAS_QT		0
#	define PJMEDIA_VIDEO_DEV_HAS_IOS	0
#	define PJMEDIA_VIDEO_DEV_HAS_DSHOW	0
#	define PJMEDIA_VIDEO_DEV_HAS_CBAR_SRC	0
#	define PJMEDIA_VIDEO_DEV_HAS_FFMPEG	0
#	undef PJMEDIA_VIDEO_DEV_HAS_V4L2
#	define PJMEDIA_VIDEO_DEV_HAS_V4L2	0
#endif	/* THIRD_PARTY_MEDIA */

