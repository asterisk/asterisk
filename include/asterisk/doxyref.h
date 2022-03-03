/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2012, Digium, Inc.
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
 *
 * This is the main header file used for generating miscellaneous documentation
 * using Doxygen.  This also utilizes the documentation in
 * include/asterisk/doxygen/ header files.
 */

/*
 * The following is for Doxygen Developer's documentation generated
 * by running "make progdocs" with doxygen installed on your
 * system.
 */

/*!
 * \page DevDoc Asterisk Developer's Documentation - Appendices
 *
 * \section devpolicy Development and Release Policies
 * \arg \ref CodeGuide : The must-read document for all developers
 * \arg \ref AstCREDITS : A Thank You to contributors (unfortunately out of date)
 *
 * \section apisandinterfaces Asterisk APIs and Interfaces
 * \arg \ref AstAPI
 * \arg \ref AstAPIChanges
 * \arg \ref Def_Channel : What's a channel, anyway?
 * \arg \ref channel_drivers : Existing channel drivers
 * \arg \ref AstAMI : The Call management socket API
 * \arg \ref AstARA : A generic data storage and retrieval API for Asterisk
 * \arg \ref AstDUNDi : A way to find phone services dynamically by using the DUNDi protocol
 * \arg \ref AstCDR
 * \arg \ref AstVar
 * \arg \ref AstVideo
 * \arg \ref AstHTTP
 *
 * \section debugconfig Debugging and Configuration References
 * \arg \ref extref
 * \arg \ref SoundFiles included in the Asterisk distribution
 *
 * \section weblinks Web sites
 * \arg \b Main:  Asterisk Developer's website https://www.asterisk.org/developers/
 * \arg \b Bugs: The Issue Tracker https://issues.asterisk.org
 * \arg \b Lists: List Server http://lists.digium.com
 * \arg \b Wiki: The Asterisk Wiki 	https://wiki.asterisk..org
 * \arg \b Docs: The Asterisk Documentation Project http://www.asteriskdocs.org
 * \arg \b Digium: The Asterisk Company https://www.digium.com
 */

/*!
 * \page CodeGuide Coding Guidelines
 * \AsteriskTrunkWarning
 * \section Coding Guidelines
 * This file is in the /doc directory in your Asterisk source tree.
 * Make sure to stay up to date with the latest guidelines.
 * \verbinclude CODING-GUIDELINES
 */

/*!
 * \page AstAPI Asterisk API
 * \section Asteriskapi Asterisk API
 * Some generic documents on the Asterisk architecture
 *
 * \arg \ref AstThreadStorage
 * \arg \ref AstExtState
 *
 * \subsection channel_txt Channels
 * \arg See \ref Def_Channel
 */

/*!
 * \page AstAPIChanges Asterisk API Changes
 *
 * \section Changes161 Version 1.6.1
 * \li vmwi_generate()
 * \li ast_channel_datastore_alloc()
 * \li ast_channel_datastore_free()
 * \li ast_channel_cmpwhentohangup()
 * \li ast_channel_setwhentohangup()
 * \li ast_settimeout()
 * \li ast_datastore_alloc()
 * \li ast_datastore_free()
 * \li ast_device_state_changed()
 * \li ast_device_state_changed_literal()
 * \li ast_dnsmgr_get()
 * \li ast_dnsmgr_lookup()
 * \li ast_dsp_set_digitmode()
 * \li ast_get_txt()
 * \li ast_event_unsubscribe()
 * \li localized_context_find_or_create()
 * \li localized_merge_contexts_and_delete()
 * \li ast_console_puts_mutable()
 * \li ast_rtp_get_quality()
 * \li ast_tcptls_client_start()
 * \li ast_tcptls_server_start()
 * \li ast_tcptls_server_stop()
 *
 * \section Changes162 Version 1.6.2
 *
 * \section Changes18 Version 1.8
 * \li ast_channel_alloc()
 */

/*!
 * \page AstAMI AMI - The Manager Interface
 * \section ami AMI - The manager Interface
 * \arg \link Config_ami Configuration file \endlink
 * \arg \ref manager.c
 * \todo include missing manager txt
 */

/*!
 * \page AstARA ARA - The Asterisk Realtime Interface
 * \section realtime ARA - a generic API to storage and retrieval
 * Implemented in \ref config.c
 * Implemented in \ref pbx_realtime.c
 * \todo include missing realtime txt
 * \todo include missing extconfig txt
 */

/*!
 * \page AstDUNDi DUNDi
 *
 * DUNDi is a peer-to-peer system for locating Internet gateways to telephony
 * services. Unlike traditional centralized services (such as the remarkably
 * simple and concise ENUM standard), DUNDi is fully-distributed with no
 * centralized authority whatsoever.
 *
 * DUNDi is not itself a Voice-over IP signaling or media protocol. Instead,
 * it publishes routes which are in turn accessed via industry standard
 * protocols such as IAX, SIP and H.323.
 *
 * \par References
 * \arg DUNDi is documented at http://www.dundi.com
 * \arg Implemented in \ref pbx_dundi.c and \ref dundi-parser.c
 * \arg Configuration in \ref dundi.conf
 */

/*!
 * \page AstCDR CDR - Call Data Records and billing
 * \section cdr Call Data Records
 * \par See also
 * \arg \ref cdr.c
 * \arg \ref cdr_drivers
 *
 * \todo include missing cdrdriver txt
 */

/*!
 * \page AstCREDITS CREDITS
 * \verbinclude CREDITS
 */

/*!
 * \page AstVideo Video support in Asterisk
 * \section sectAstVideo Video support in Asterisk
 * \todo include missing video txt
 */

/*!
 * \page AstVar Globally predefined channel variables
 * \section globchan Globally predefined channel variables
 *
 * More and more of these variables are being replaced by dialplan functions.
 * Some still exist though and some that does still exist needs to move to
 * dialplan functions.
 *
 * See also
 * - \ref pbx_retrieve_variable()
 * - \ref AstChanVar
 *
 */

/*!
 * \page AstChanVar Asterisk Dialplan Variables
 *	Asterisk Dialplan variables are divided into three groups:
 *	- Predefined global variables, handled by the PBX core
 *	- Global variables, that exist for the duration of the pbx execution
 *	- Channel variables, that exist during a channel
 *
 * Global variables are reachable in all channels, all of the time.
 * Channel variables are only reachable within the channel.
 *
 * For more information on the predefined variables, see \ref AstVar
 *
 * Global and Channel variables:
 * - Names are Case insensitive
 * - Names that start with a character, but are alphanumeric
 * - Global variables are defined and reached with the GLOBAL() dialplan function
 *   and the set application, like
 *
 * 	exten => 1234,1,set(GLOBAL(myvariable)=tomteluva)
 *
 * 	- \ref func_global.c
 *
 * - Channel variables are defined with the set() dialplan application
 *
 *	exten => 1234,1,set(xmasattribute=tomtegröt)
 *
 * - Some channels also supports setting channel variables with the \b setvar=
 *   configuraiton option for a device or line.
 *
 * \section AstChanVar_globalvars Global Variables
 * Global variables can also be set in the [globals] section of extensions.conf. The
 * setting \b clearglobalvars in extensions.conf [general] section affects whether
 * or not the global variables defined in \b globals are reset at dialplan reload.
 *
 * There are CLI commands to change and read global variables. This can be handy
 * to reset counters at midnight from an external script.
 *
 * \section AstChanVar_devnotes Developer notes
 * Variable handling is managed within \ref pbx.c
 * You need to include pbx.h to reach these functions.
 *	- \ref pbx_builtin_setvar_helper()
 * 	- \ref pbx_builtin_getvar_helper()
 *
 * The variables is a linked list stored in the channel data structure
 * with the list starting at varshead in struct ast_channel
 */

/*!
 * \page Config_mod Modules configuration
 * All res_ resource modules are loaded with globals on, which means
 * that non-static functions are callable from other modules.
 *
 * If you want your non res_* module to export functions to other modules
 * you have to include it in the [global] section.
 */

/*!
 * \page Config_ext Extensions.conf - the Dial Plan
 * \section dialplan Extensions.conf
 * \verbinclude extensions.conf.sample
 */

/*!
 * \page Config_rtp RTP configuration
 * \arg Implemented in \ref rtp.c
 * Used in \ref chan_sip.c and \ref chan_mgcp.c (and various H.323 channels)
 * \section rtpconf rtp.conf
 * \verbinclude rtp.conf.sample
 */

/*!
 * \page Config_codec CODEC Configuration
 * \section codecsconf codecs.conf
 * \verbinclude codecs.conf.sample
 */

/*!
 * \page Config_ara REALTIME Configuration
 * \arg See also: \arg \link AstARA \endlink
 * \section extconf extconfig.conf
 * \verbinclude extconfig.conf.sample
 */

/*!
 * \page Config_ami AMI configuration
 * \arg See also: \arg \link AstAMI \endlink
 * \section amiconf manager.conf
 * \verbinclude manager.conf.sample
 */

/*!
 * \page SoundFiles Sound files
 * \section SecSound Asterisk Sound files
 * Asterisk includes a large number of sound files. Many of these
 * are used by applications and demo scripts within asterisk.
 *
 * Additional sound files are available in the asterisk-addons
 * repository on svn.digium.com
 */

/*!
 * \page AstHTTP AMI over HTTP support
 * The http.c file includes support for manager transactions over
 * http.
 * \section amihttp AMI - The manager Interface
 * \arg \link Config_ami Configuration file \endlink
 */

/*
 * Doxygen Groups
 */

/*! \addtogroup configuration_file Configuration Files
 */

/*!
 * \addtogroup cdr_drivers Module: CDR Drivers
 * \section CDR_generic Asterisk CDR Drivers
 * \brief CDR drivers are loaded dynamically, each loaded CDR driver produce
 *        a billing record for each call.
 * \arg \ref Config_mod "Modules Configuration"
 */

/*!
 * \addtogroup channel_drivers Module: Asterisk Channel Drivers
 * \section channel_generic Asterisk Channel Drivers
 * \brief Channel drivers are loaded dynamically.
 * \arg \ref Config_mod "Modules Configuration"
 */

/*!
 * \addtogroup applications Dial plan applications
 * \section app_generic Asterisk Dial Plan Applications
 * \brief Applications support the dialplan. They register dynamically with
 *        \see ast_register_application() and unregister with
 *        \see ast_unregister_application()
 * \par See also
 * \arg \ref functions
 */

/*!
 * \addtogroup functions Module: Dial plan functions
 * \section func_generic Asterisk Dial Plan Functions
 * \brief Functions support the dialplan.  They do not change any property of a channel
 *        or touch a channel in any way.
 * \par See also
 * \arg \ref applications
 *
 */

/*!
 * \addtogroup codecs Module: Codecs
 * \section codec_generic Asterisk Codec Modules
 * Codecs are referenced in configuration files by name
 * \par See also
 * \arg \ref formats
 */

/*!
 * \addtogroup formats Module: Media File Formats
 * \section format_generic Asterisk Format drivers
 * Formats are modules that read or write media files to disk.
 * \par See also
 * \arg \ref codecs
 */

/*!
 * \addtogroup rtp_engines Module: RTP Engines
 * \section rtp_engine_blah Asterisk RTP Engines
 */
