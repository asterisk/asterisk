/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2008, Digium, Inc.
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
 * \brief DAHDI compatibility with zaptel
 */

#ifndef DAHDI_COMPAT_H
#define DAHDI_COMPAT_H

#if defined(HAVE_DAHDI)

#include <dahdi/user.h>

#define DAHDI_DIR_NAME "/dev/dahdi"
#define DAHDI_NAME "DAHDI"

#elif defined(HAVE_ZAPTEL)

#include <zaptel/zaptel.h>

#define DAHDI_DIR_NAME "/dev/zap"
#define DAHDI_NAME "Zaptel"

/* Compiling against Zaptel instead of DAHDI */

#if defined(__ZT_SIG_FXO)
#define __DAHDI_SIG_FXO __ZT_SIG_FXO
#endif
#if defined(__ZT_SIG_FXS)
#define __DAHDI_SIG_FXS __ZT_SIG_FXS
#endif
#if defined(ZT_ALARM_BLUE)
#define DAHDI_ALARM_BLUE ZT_ALARM_BLUE
#endif
#if defined(ZT_ALARM_LOOPBACK)
#define DAHDI_ALARM_LOOPBACK ZT_ALARM_LOOPBACK
#endif
#if defined(ZT_ALARM_NONE)
#define DAHDI_ALARM_NONE ZT_ALARM_NONE
#endif
#if defined(ZT_ALARM_NOTOPEN)
#define DAHDI_ALARM_NOTOPEN ZT_ALARM_NOTOPEN
#endif
#if defined(ZT_ALARM_RECOVER)
#define DAHDI_ALARM_RECOVER ZT_ALARM_RECOVER
#endif
#if defined(ZT_ALARM_RED)
#define DAHDI_ALARM_RED ZT_ALARM_RED
#endif
#if defined(ZT_ALARM_YELLOW)
#define DAHDI_ALARM_YELLOW ZT_ALARM_YELLOW
#endif
#if defined(ZT_AUDIOMODE)
#define DAHDI_AUDIOMODE ZT_AUDIOMODE
#endif
#if defined(ZT_CHANNO)
#define DAHDI_CHANNO ZT_CHANNO
#endif
#if defined(ZT_CHECK_HOOKSTATE)
#define DAHDI_CHECK_HOOKSTATE ZT_CHECK_HOOKSTATE
#endif
#if defined(ZT_CONF_CONF)
#define DAHDI_CONF_CONF ZT_CONF_CONF
#endif
#if defined(ZT_CONF_CONFANN)
#define DAHDI_CONF_CONFANN ZT_CONF_CONFANN
#endif
#if defined(ZT_CONF_CONFANNMON)
#define DAHDI_CONF_CONFANNMON ZT_CONF_CONFANNMON
#endif
#if defined(ZT_CONF_CONFMON)
#define DAHDI_CONF_CONFMON ZT_CONF_CONFMON
#endif
#if defined(ZT_CONF_DIGITALMON)
#define DAHDI_CONF_DIGITALMON ZT_CONF_DIGITALMON
#endif
#if defined(ZT_CONF_LISTENER)
#define DAHDI_CONF_LISTENER ZT_CONF_LISTENER
#endif
#if defined(ZT_CONF_MONITORBOTH)
#define DAHDI_CONF_MONITORBOTH ZT_CONF_MONITORBOTH
#endif
#if defined(ZT_CONF_NORMAL)
#define DAHDI_CONF_NORMAL ZT_CONF_NORMAL
#endif
#if defined(ZT_CONF_PSEUDO_LISTENER)
#define DAHDI_CONF_PSEUDO_LISTENER ZT_CONF_PSEUDO_LISTENER
#endif
#if defined(ZT_CONF_PSEUDO_TALKER)
#define DAHDI_CONF_PSEUDO_TALKER ZT_CONF_PSEUDO_TALKER
#endif
#if defined(ZT_CONF_REALANDPSEUDO)
#define DAHDI_CONF_REALANDPSEUDO ZT_CONF_REALANDPSEUDO
#endif
#if defined(ZT_CONF_TALKER)
#define DAHDI_CONF_TALKER ZT_CONF_TALKER
#endif
#if defined(ZT_CONFDIAG)
#define DAHDI_CONFDIAG ZT_CONFDIAG
#endif
#if defined(ZT_CONFMUTE)
#define DAHDI_CONFMUTE ZT_CONFMUTE
#endif
#if defined(ZT_DEFAULT_NUM_BUFS)
#define DAHDI_DEFAULT_NUM_BUFS ZT_DEFAULT_NUM_BUFS
#endif
#if defined(ZT_DIAL)
#define DAHDI_DIAL ZT_DIAL
#endif
#if defined(ZT_DIALING)
#define DAHDI_DIALING ZT_DIALING
#endif
#if defined(ZT_DIAL_OP_APPEND)
#define DAHDI_DIAL_OP_APPEND ZT_DIAL_OP_APPEND
#endif
#if defined(ZT_DIAL_OP_REPLACE)
#define DAHDI_DIAL_OP_REPLACE ZT_DIAL_OP_REPLACE
#endif
#if defined(ZT_ECHOCANCEL)
#define DAHDI_ECHOCANCEL ZT_ECHOCANCEL
#endif
#if defined(ZT_ECHOTRAIN)
#define DAHDI_ECHOTRAIN ZT_ECHOTRAIN
#endif
#if defined(ZT_EVENT_ALARM)
#define DAHDI_EVENT_ALARM ZT_EVENT_ALARM
#endif
#if defined(ZT_EVENT_BITSCHANGED)
#define DAHDI_EVENT_BITSCHANGED ZT_EVENT_BITSCHANGED
#endif
#if defined(ZT_EVENT_DIALCOMPLETE)
#define DAHDI_EVENT_DIALCOMPLETE ZT_EVENT_DIALCOMPLETE
#endif
#if defined(ZT_EVENT_DTMFDOWN)
#define DAHDI_EVENT_DTMFDOWN ZT_EVENT_DTMFDOWN
#endif
#if defined(ZT_EVENT_DTMFUP)
#define DAHDI_EVENT_DTMFUP ZT_EVENT_DTMFUP
#endif
#if defined(ZT_EVENT_EC_DISABLED)
#define DAHDI_EVENT_EC_DISABLED ZT_EVENT_EC_DISABLED
#endif
#if defined(ZT_EVENT_HOOKCOMPLETE)
#define DAHDI_EVENT_HOOKCOMPLETE ZT_EVENT_HOOKCOMPLETE
#endif
#if defined(ZT_EVENT_NOALARM)
#define DAHDI_EVENT_NOALARM ZT_EVENT_NOALARM
#endif
#if defined(ZT_EVENT_NONE)
#define DAHDI_EVENT_NONE ZT_EVENT_NONE
#endif
#if defined(ZT_EVENT_ONHOOK)
#define DAHDI_EVENT_ONHOOK ZT_EVENT_ONHOOK
#endif
#if defined(ZT_EVENT_POLARITY)
#define DAHDI_EVENT_POLARITY ZT_EVENT_POLARITY
#endif
#if defined(ZT_EVENT_PULSEDIGIT)
#define DAHDI_EVENT_PULSEDIGIT ZT_EVENT_PULSEDIGIT
#endif
#if defined(ZT_EVENT_PULSE_START)
#define DAHDI_EVENT_PULSE_START ZT_EVENT_PULSE_START
#endif
#if defined(ZT_EVENT_REMOVED)
#define DAHDI_EVENT_REMOVED ZT_EVENT_REMOVED
#endif
#if defined(ZT_EVENT_RINGBEGIN)
#define DAHDI_EVENT_RINGBEGIN ZT_EVENT_RINGBEGIN
#endif
#if defined(ZT_EVENT_RINGEROFF)
#define DAHDI_EVENT_RINGEROFF ZT_EVENT_RINGEROFF
#endif
#if defined(ZT_EVENT_RINGERON)
#define DAHDI_EVENT_RINGERON ZT_EVENT_RINGERON
#endif
#if defined(ZT_EVENT_RINGOFFHOOK)
#define DAHDI_EVENT_RINGOFFHOOK ZT_EVENT_RINGOFFHOOK
#endif
#if defined(ZT_EVENT_TIMER_EXPIRED)
#define DAHDI_EVENT_TIMER_EXPIRED ZT_EVENT_TIMER_EXPIRED
#endif
#if defined(ZT_EVENT_TIMER_PING)
#define DAHDI_EVENT_TIMER_PING ZT_EVENT_TIMER_PING
#endif
#if defined(ZT_EVENT_WINKFLASH)
#define DAHDI_EVENT_WINKFLASH ZT_EVENT_WINKFLASH
#endif
#if defined(ZT_FLASH)
#define DAHDI_FLASH ZT_FLASH
#endif
#if defined(ZT_FLUSH)
#define DAHDI_FLUSH ZT_FLUSH
#endif
#if defined(ZT_FLUSH_ALL)
#define DAHDI_FLUSH_ALL ZT_FLUSH_ALL
#endif
#if defined(ZT_FLUSH_BOTH)
#define DAHDI_FLUSH_BOTH ZT_FLUSH_BOTH
#endif
#if defined(ZT_FLUSH_READ)
#define DAHDI_FLUSH_READ ZT_FLUSH_READ
#endif
#if defined(ZT_FLUSH_WRITE)
#define DAHDI_FLUSH_WRITE ZT_FLUSH_WRITE
#endif
#if defined(ZT_GET_BUFINFO)
#define DAHDI_GET_BUFINFO ZT_GET_BUFINFO
#endif
#if defined(ZT_GETCONF)
#define DAHDI_GETCONF ZT_GETCONF
#endif
#if defined(ZT_GETCONFMUTE)
#define DAHDI_GETCONFMUTE ZT_GETCONFMUTE
#endif
#if defined(ZT_GETEVENT)
#define DAHDI_GETEVENT ZT_GETEVENT
#endif
#if defined(ZT_GETGAINS)
#define DAHDI_GETGAINS ZT_GETGAINS
#endif
#if defined(ZT_GET_PARAMS)
#define DAHDI_GET_PARAMS ZT_GET_PARAMS
#endif
#if defined(ZT_HOOK)
#define DAHDI_HOOK ZT_HOOK
#endif
#if defined(ZT_IOMUX)
#define DAHDI_IOMUX ZT_IOMUX
#endif
#if defined(ZT_IOMUX_READ)
#define DAHDI_IOMUX_READ ZT_IOMUX_READ
#endif
#if defined(ZT_IOMUX_SIGEVENT)
#define DAHDI_IOMUX_SIGEVENT ZT_IOMUX_SIGEVENT
#endif
#if defined(ZT_IOMUX_WRITE)
#define DAHDI_IOMUX_WRITE ZT_IOMUX_WRITE
#endif
#if defined(ZT_LAW_ALAW)
#define DAHDI_LAW_ALAW ZT_LAW_ALAW
#endif
#if defined(ZT_LAW_DEFAULT)
#define DAHDI_LAW_DEFAULT ZT_LAW_DEFAULT
#endif
#if defined(ZT_LAW_MULAW)
#define DAHDI_LAW_MULAW ZT_LAW_MULAW
#endif
#if defined(ZT_MAX_NUM_BUFS)
#define DAHDI_MAX_NUM_BUFS ZT_MAX_NUM_BUFS
#endif
#if defined(ZT_MAX_SPANS)
#define DAHDI_MAX_SPANS ZT_MAX_SPANS
#endif
#if defined(ZT_OFFHOOK)
#define DAHDI_OFFHOOK ZT_OFFHOOK
#endif
#if defined(ZT_ONHOOK)
#define DAHDI_ONHOOK ZT_ONHOOK
#endif
#if defined(ZT_ONHOOKTRANSFER)
#define DAHDI_ONHOOKTRANSFER ZT_ONHOOKTRANSFER
#endif
#if defined(ZT_POLICY_IMMEDIATE)
#define DAHDI_POLICY_IMMEDIATE ZT_POLICY_IMMEDIATE
#endif
#if defined(ZT_POLICY_WHEN_FULL)
#define DAHDI_POLICY_WHEN_FULL ZT_POLICY_WHEN_FULL
#endif
#if defined(ZT_RING)
#define DAHDI_RING ZT_RING
#endif
#if defined(ZT_RINGOFF)
#define DAHDI_RINGOFF ZT_RINGOFF
#endif
#if defined(ZT_SENDTONE)
#define DAHDI_SENDTONE ZT_SENDTONE
#endif
#if defined(ZT_SET_BLOCKSIZE)
#define DAHDI_SET_BLOCKSIZE ZT_SET_BLOCKSIZE
#endif
#if defined(ZT_SET_BUFINFO)
#define DAHDI_SET_BUFINFO ZT_SET_BUFINFO
#endif
#if defined(ZT_SETCADENCE)
#define DAHDI_SETCADENCE ZT_SETCADENCE
#endif
#if defined(ZT_SETCONF)
#define DAHDI_SETCONF ZT_SETCONF
#endif
#if defined(ZT_SET_DIALPARAMS)
#define DAHDI_SET_DIALPARAMS ZT_SET_DIALPARAMS
#endif
#if defined(ZT_SETGAINS)
#define DAHDI_SETGAINS ZT_SETGAINS
#endif
#if defined(ZT_SETLAW)
#define DAHDI_SETLAW ZT_SETLAW
#endif
#if defined(ZT_SETLINEAR)
#define DAHDI_SETLINEAR ZT_SETLINEAR
#endif
#if defined(ZT_SET_PARAMS)
#define DAHDI_SET_PARAMS ZT_SET_PARAMS
#endif
#if defined(ZT_SETTONEZONE)
#define DAHDI_SETTONEZONE ZT_SETTONEZONE
#endif
#if defined(ZT_SIG_CLEAR)
#define DAHDI_SIG_CLEAR ZT_SIG_CLEAR
#endif
#if defined(ZT_SIG_EM)
#define DAHDI_SIG_EM ZT_SIG_EM
#endif
#if defined(ZT_SIG_EM_E1)
#define DAHDI_SIG_EM_E1 ZT_SIG_EM_E1
#endif
#if defined(ZT_SIG_FXO)
#define DAHDI_SIG_FXO ZT_SIG_FXO
#endif
#if defined(ZT_SIG_FXOGS)
#define DAHDI_SIG_FXOGS ZT_SIG_FXOGS
#endif
#if defined(ZT_SIG_FXOKS)
#define DAHDI_SIG_FXOKS ZT_SIG_FXOKS
#endif
#if defined(ZT_SIG_FXOLS)
#define DAHDI_SIG_FXOLS ZT_SIG_FXOLS
#endif
#if defined(ZT_SIG_FXS)
#define DAHDI_SIG_FXS ZT_SIG_FXS
#endif
#if defined(ZT_SIG_FXSGS)
#define DAHDI_SIG_FXSGS ZT_SIG_FXSGS
#endif
#if defined(ZT_SIG_FXSKS)
#define DAHDI_SIG_FXSKS ZT_SIG_FXSKS
#endif
#if defined(ZT_SIG_FXSLS)
#define DAHDI_SIG_FXSLS ZT_SIG_FXSLS
#endif
#if defined(ZT_SIG_HARDHDLC)
#define DAHDI_SIG_HARDHDLC ZT_SIG_HARDHDLC
#endif
#if defined(ZT_SIG_HDLCFCS)
#define DAHDI_SIG_HDLCFCS ZT_SIG_HDLCFCS
#endif
#if defined(ZT_SIG_SF)
#define DAHDI_SIG_SF ZT_SIG_SF
#endif
#if defined(ZT_SPANSTAT)
#define DAHDI_SPANSTAT ZT_SPANSTAT
#endif
#if defined(ZT_SPECIFY)
#define DAHDI_SPECIFY ZT_SPECIFY
#endif
#if defined(ZT_START)
#define DAHDI_START ZT_START
#endif
#if defined(ZT_TC_ALLOCATE)
#define DAHDI_TC_ALLOCATE ZT_TC_ALLOCATE
#endif
#if defined(ZT_TC_GETINFO)
#define DAHDI_TC_GETINFO ZT_TC_GETINFO
#endif
#if defined(ZT_TIMERACK)
#define DAHDI_TIMERACK ZT_TIMERACK
#endif
#if defined(ZT_TIMERCONFIG)
#define DAHDI_TIMERCONFIG ZT_TIMERCONFIG
#endif
#if defined(ZT_TIMERPING)
#define DAHDI_TIMERPING ZT_TIMERPING
#endif
#if defined(ZT_TIMERPONG)
#define DAHDI_TIMERPONG ZT_TIMERPONG
#endif
#if defined(ZT_TONE_BUSY)
#define DAHDI_TONE_BUSY ZT_TONE_BUSY
#endif
#if defined(ZT_TONE_CONGESTION)
#define DAHDI_TONE_CONGESTION ZT_TONE_CONGESTION
#endif
#if defined(ZT_TONEDETECT)
#define DAHDI_TONEDETECT ZT_TONEDETECT
#endif
#if defined(ZT_TONEDETECT_MUTE)
#define DAHDI_TONEDETECT_MUTE ZT_TONEDETECT_MUTE
#endif
#if defined(ZT_TONEDETECT_ON)
#define DAHDI_TONEDETECT_ON ZT_TONEDETECT_ON
#endif
#if defined(ZT_TONE_DIALRECALL)
#define DAHDI_TONE_DIALRECALL ZT_TONE_DIALRECALL
#endif
#if defined(ZT_TONE_DIALTONE)
#define DAHDI_TONE_DIALTONE ZT_TONE_DIALTONE
#endif
#if defined(ZT_TONE_DTMF_BASE)
#define DAHDI_TONE_DTMF_BASE ZT_TONE_DTMF_BASE
#endif
#if defined(ZT_TONE_INFO)
#define DAHDI_TONE_INFO ZT_TONE_INFO
#endif
#if defined(ZT_TONE_RINGTONE)
#define DAHDI_TONE_RINGTONE ZT_TONE_RINGTONE
#endif
#if defined(ZT_TONE_STUTTER)
#define DAHDI_TONE_STUTTER ZT_TONE_STUTTER
#endif
#if defined(ZT_vldtmf)
#define DAHDI_vldtmf ZT_vldtmf
#endif
#if defined(ZT_WINK)
#define DAHDI_WINK ZT_WINK
#endif
#if defined(HAVE_ZAPTEL)
#define HAVE_DAHDI HAVE_ZAPTEL
#endif

#define DAHDI_TONE_DTMF_A ZT_TONE_DTMF_A
#define DAHDI_TONE_DTMF_p ZT_TONE_DTMF_p
#define DAHDI_TONE_DTMF_s ZT_TONE_DTMF_s

#define dahdi_bufferinfo zt_bufferinfo
#define dahdi_confinfo zt_confinfo
#define dahdi_dialoperation zt_dialoperation
#define dahdi_dialparams zt_dialparams
#define dahdi_gains zt_gains
#define dahdi_params zt_params
#define dahdi_ring_cadence zt_ring_cadence
#define dahdi_spaninfo zt_spaninfo
#define dahdi_transcoder_info zt_transcoder_info
#define dahdi_transcoder_formats zt_transcoder_formats

#endif

#define DAHDI_FILE_CHANNEL   DAHDI_DIR_NAME "/channel"
#define DAHDI_FILE_CTL       DAHDI_DIR_NAME "/ctl"
#define DAHDI_FILE_PSEUDO    DAHDI_DIR_NAME "/pseudo"
#define DAHDI_FILE_TIMER     DAHDI_DIR_NAME "/timer"
#define DAHDI_FILE_TRANSCODE DAHDI_DIR_NAME "/transcode"

#endif /* DAHDI_COMPAT_H */
