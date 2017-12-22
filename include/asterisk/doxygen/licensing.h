/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2009, Digium, Inc.
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
 */

/*!
 * \page Licensing Asterisk Licensing Information
 *
 * \section license Asterisk License
 * \note See the LICENSE file for up to date info
 *
 * \section otherlicenses Licensing of 3rd Party Code
 *
 * This section contains a (not yet complete) list of libraries that are used
 * by various parts of Asterisk, including related licensing information.
 *
 * \subsection alsa_lib ALSA Library
 * \arg <b>Library</b>: libasound
 * \arg <b>Website</b>: http://www.alsa-project.org
 * \arg <b>Used by</b>: chan_alsa
 * \arg <b>License</b>: LGPL
 *
 * \subsection openssl_lib OpenSSL
 * \arg <b>Library</b>: libcrypto, libssl
 * \arg <b>Website</b>: http://www.openssl.org
 * \arg <b>Used by</b>: Asterisk core (TLS for manager and HTTP), res_crypto
 * \arg <b>License</b>: Apache 2.0
 * \arg <b>Note</b>:    An exception has been granted to allow linking of
 *                      OpenSSL with Asterisk.
 *
 * \subsection curl_lib Curl
 * \arg <b>Library</b>: libcurl
 * \arg <b>Website</b>: http://curl.haxx.se
 * \arg <b>Used by</b>: func_curl, res_config_curl, res_curl
 * \arg <b>License</b>: BSD
 *
 * \subsection portaudio_lib PortAudio
 * \arg <b>Library</b>: libportaudio
 * \arg <b>Website</b>: http://www.portaudio.com
 * \arg <b>Used by</b>: chan_console
 * \arg <b>License</b>: BSD
 * \arg <b>Note</b>:    Even though PortAudio is licensed under a BSD style
 *                      license, PortAudio will make use of some audio interface,
 *                      depending on how it was built.  That audio interface may
 *                      introduce additional licensing restrictions.  On Linux,
 *                      this would most commonly be ALSA: \ref alsa_lib.
 *
 * \subsection rawlist Raw list of libraries that used by any part of Asterisk
 * \li c-client.a (app_voicemail with IMAP support)
 * \li libSDL-1.2.so.0
 * \li libSaClm.so.2
 * \li libSaEvt.so.2
 * \li libX11.so.6
 * \li libXau.so.6
 * \li libXdmcp.so.6
 * \li libasound.so.2
 * \li libc.so.6
 * \li libcom_err.so.2
 * \li libcrypt.so.1
 * \li libcrypto.so.0.9.8 (chan_h323)
 * \li libcurl.so.4
 * \li libdirect-1.0.so.0
 * \li libdirectfb-1.0.so.0
 * \li libdl.so.2
 * \li libexpat.so (chan_h323)
 * \li libfusion-1.0.so.0
 * \li libgcc_s.so (chan_h323)
 * \li libgcrypt.so.11 (chan_h323)
 * \li libglib-2.0.so.0
 * \li libgmime-2.0.so.2
 * \li libgmodule-2.0.so.0
 * \li libgnutls.so.13 (chan_h323)
 * \li libgobject-2.0.so.0
 * \li libgpg-error.so.0 (chan_h323)
 * \li libgssapi_krb5.so.2
 * \li libgthread-2.0.so.0
 * \li libidn.so.11
 * \li libiksemel.so.3
 * \li libisdnnet.so
 * \li libjack.so.0
 * \li libjpeg.so.62
 * \li libk5crypto.so.3
 * \li libkeyutils.so.1
 * \li libkrb5.so.3
 * \li libkrb5support.so.0
 * \li liblber-2.4.so.2 (chan_h323)
 * \li libldap_r-2.4.so.2 (chan_h323)
 * \li libltdl.so.3
 * \li liblua5.1.so.0
 * \li libm.so.6
 * \li libmISDN.so
 * \li libnbs.so.1
 * \li libncurses.so.5
 * \li libnetsnmp.so.15
 * \li libnetsnmpagent.so.15
 * \li libnetsnmphelpers.so.15
 * \li libnetsnmpmibs.so.15
 * \li libnsl.so.1
 * \li libodbc.so.1
 * \li libogg.so.0
 * \li libopenh323.so (chan_h323)
 * \li libpcre.so.3
 * \li libperl.so.5.8
 * \li libportaudio.so.2
 * \li libpq.so.5
 * \li libpri.so.1.4
 * \li libpt.so (chan_h323)
 * \li libpthread.so.0
 * \li libradiusclient-ng.so.2
 * \li libresample.so.1.0
 * \li libresolv.so.2 (chan_h323)
 * \li librt.so.1
 * \li libsasl2.so.2 (chan_h323)
 * \li libselinux.so.1
 * \li libsensors.so.3
 * \li libspandsp.so.1
 * \li libspeex.so.1
 * \li libsqlite.so.0
 * \li libsqlite3.so.0
 * \li libss7.so.1
 * \li libssl.so.0.9.8 (chan_h323)
 * \li libstdc++.so (chan_h323, chan_vpb)
 * \li libsuppserv.so
 * \li libsybdb.so.5
 * \li libsysfs.so.2
 * \li libtasn1.so.3 (chan_h323)
 * \li libtds.so.4
 * \li libtiff.so.4
 * \li libtonezone.so.1.0
 * \li libvorbis.so.0
 * \li libvorbisenc.so.2
 * \li libvpb.a (chan_vpb)
 * \li libwrap.so.0
 * \li libxcb-xlib.so.0
 * \li libxcb.so.1
 * \li libz.so.1 (chan_h323)
 * \li linux-vdso.so.1
*/
