/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2008-2009, Digium, Inc.
 *
 * Dwayne M. Hubbard <dhubbard@digium.com>
 * Kevin P. Fleming <kpfleming@digium.com>
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

#ifndef _ASTERISK_RES_FAX_H
#define _ASTERISK_RES_FAX_H

#include <asterisk.h>
#include <asterisk/lock.h>
#include <asterisk/linkedlists.h>
#include <asterisk/module.h>
#include <asterisk/utils.h>
#include <asterisk/options.h>
#include <asterisk/frame.h>
#include <asterisk/cli.h>
#include <asterisk/stringfields.h>

/*! \brief capabilities for res_fax to locate a fax technology module */
enum ast_fax_capabilities {
	/*! SendFax is supported */
	AST_FAX_TECH_SEND      = (1 << 0),
	/*! ReceiveFax is supported */
	AST_FAX_TECH_RECEIVE   = (1 << 1),
	/*! Audio FAX session supported */
	AST_FAX_TECH_AUDIO     = (1 << 2),
	/*! T.38 FAX session supported */
	AST_FAX_TECH_T38       = (1 << 3),
	/*! sending mulitple documents supported */
	AST_FAX_TECH_MULTI_DOC = (1 << 4),
};

/*! \brief fax modem capabilities */
enum ast_fax_modems {
	/*! V.17 */
	AST_FAX_MODEM_V17 = (1 << 0),
	/*! V.27 */
	AST_FAX_MODEM_V27 = (1 << 1),
	/*! V.29 */
	AST_FAX_MODEM_V29 = (1 << 2),
	/*! V.34 */
	AST_FAX_MODEM_V34 = (1 << 3),
};

/*! \brief current state of a fax session */
enum ast_fax_state {
	/*! uninitialized state */
	AST_FAX_STATE_UNINITIALIZED = 0,
	/*! initialized state */
	AST_FAX_STATE_INITIALIZED,
	/*! fax resources open state */
	AST_FAX_STATE_OPEN,
	/*! fax session in progress */
	AST_FAX_STATE_ACTIVE,
	/*! fax session complete */
	AST_FAX_STATE_COMPLETE,
};

/*! \brief fax session options */
enum ast_fax_optflag {
	/*! false/disable configuration override */
	AST_FAX_OPTFLAG_FALSE = 0,
	/*! true/enable configuration override */
	AST_FAX_OPTFLAG_TRUE,
	/*! use the configured default */
	AST_FAX_OPTFLAG_DEFAULT,
};

struct ast_fax_t38_parameters {
	unsigned int version;					/*!< Supported T.38 version */
	unsigned int max_ifp; 					/*!< Maximum IFP size supported */
	enum ast_control_t38_rate rate;				/*!< Maximum fax rate supported */
	enum ast_control_t38_rate_management rate_management;	/*!< Rate management setting */
	unsigned int fill_bit_removal:1;			/*!< Set if fill bit removal can be used */
	unsigned int transcoding_mmr:1;				/*!< Set if MMR transcoding can be used */
	unsigned int transcoding_jbig:1;			/*!< Set if JBIG transcoding can be used */
};

struct ast_fax_document {
	AST_LIST_ENTRY(ast_fax_document) next;
	char filename[0];
};

AST_LIST_HEAD_NOLOCK(ast_fax_documents, ast_fax_document);

/*! \brief The data communicated between the high level applications and the generic fax function */
struct ast_fax_session_details {
	/*! fax session capability requirements.  The caps field is used to select
 	 * the proper fax technology module before the session starts */
	enum ast_fax_capabilities caps;
	/*! modem requirement for the session */
	enum ast_fax_modems modems;
	/*! session id */
	unsigned int id;
	/*! document(s) to be sent/received */
	struct ast_fax_documents documents;
	AST_DECLARE_STRING_FIELDS(
		/*! resolution negotiated during the fax session.  This is stored in the
	 	 * FAXRESOLUTION channel variable when the fax session completes */
		AST_STRING_FIELD(resolution);
		/*! transfer rate negotiated during the fax session.  This is stored in the
	 	 * FAXBITRATE channel variable when the fax session completes */
		AST_STRING_FIELD(transfer_rate);
		/*! local station identification.  This is set from the LOCALSTATIONID
	 	 * channel variable before the fax session starts */
		AST_STRING_FIELD(localstationid);
		/*! remote station identification.  This is stored in the REMOTESTATIONID
	 	 * channel variable after the fax session completes */
		AST_STRING_FIELD(remotestationid);
		/*! headerinfo variable is set from the LOCALHEADERINFO channel variable
	 	 * before the fax session starts */
		AST_STRING_FIELD(headerinfo);
		/*! the result of the fax session */
		AST_STRING_FIELD(result);
		/*! a more descriptive result string of the fax session */
		AST_STRING_FIELD(resultstr);
		/*! the error reason of the fax session */
		AST_STRING_FIELD(error);
	);
	/*! the number of pages sent/received during a fax session */
	unsigned int pages_transferred;
	/*! session details flags for options */
	union {
		/*! dontuse dummy variable - do not ever use */	
		uint32_t dontuse;
		struct {
			/*! flag to send debug manager events */
			uint32_t debug:2;
			/*! flag indicating the use of Error Correction Mode (ECM) */
			uint32_t ecm:1;
			/*! flag indicating the sending of status manager events */
			uint32_t statusevents:2;
			/*! allow audio mode FAX on T.38-capable channels */
			uint32_t allow_audio:2;
			/*! indicating the session switched to T38 */
			uint32_t switch_to_t38:1;
			/*! flag indicating whether CED should be sent (for receive mode) */
			uint32_t send_ced:1;
			/*! flag indicating whether CNG should be sent (for send mode) */
			uint32_t send_cng:1;
			/*! send a T.38 reinvite */
			uint32_t request_t38:1;
		};
	} option;
	/*! override the minimum transmission rate with a channel variable */
	unsigned int minrate;
	/*! override the maximum transmission rate with a channel varialbe */
	unsigned int maxrate;
	/*! our T.38 session parameters, if any */
	struct ast_fax_t38_parameters our_t38_parameters;
	/*! the other endpoint's T.38 session parameters, if any */
	struct ast_fax_t38_parameters their_t38_parameters;
};

struct ast_fax_tech;
struct ast_fax_debug_info;
	
/*! \brief The data required to handle a fax session */
struct ast_fax_session {
	/*! session id */
	unsigned int id;
	/*! session file descriptor */
	int fd;
	/*! fax session details structure */
	struct ast_fax_session_details *details;
	/*! fax frames received */
	unsigned long frames_received;
	/*! fax frames sent */
	unsigned long frames_sent;
	/*! the fax technology callbacks */
	const struct ast_fax_tech *tech;
	/*! private implementation pointer */
	void *tech_pvt;
	/*! fax state */
	enum ast_fax_state state;
	/*! name of the Asterisk channel using the fax session */
	char *channame;
	/*! unique ID of the Asterisk channel using the fax session */
	char *chan_uniqueid;
	/*! Asterisk channel using the fax session */
	struct ast_channel *chan;
	/*! fax debugging structure */
	struct ast_fax_debug_info *debug_info;
	/*! used to take variable-sized frames in and output frames of an expected size to the fax stack */
	struct ast_smoother *smoother;
};

struct ast_fax_tech_token;

/*! \brief used to register a FAX technology module with res_fax */
struct ast_fax_tech {
	/*! the type of fax session supported with this ast_fax_tech structure */
	const char * const type;
	/*! a short description of the fax technology */
	const char * const description;
	/*! version string of the technology module */
	const char * const version;
	/*! the ast_fax_capabilities supported by the fax technology */
	const enum ast_fax_capabilities caps;
	/*! module information for the fax technology */
	struct ast_module *module;
	/*! reserves a session for future use; returns a token */
	struct ast_fax_tech_token *(* const reserve_session)(struct ast_fax_session *);
	/*! releases an unused session token */
	void (* const release_token)(struct ast_fax_tech_token *);
	/*! creates a new fax session, optionally using a previously-reserved token */
	void *(* const new_session)(struct ast_fax_session *, struct ast_fax_tech_token *);
	/*! destroys an existing fax session */
	void (* const destroy_session)(struct ast_fax_session *);
	/*! sends an Asterisk frame to res_fax */
	struct ast_frame *(* const read)(struct ast_fax_session *);
	/*! writes an Asterisk frame to the fax session */
	int (* const write)(struct ast_fax_session *, const struct ast_frame *);
	/*! starts the fax session */
	int (* const start_session)(struct ast_fax_session *);
	/*! cancels a fax session */
	int (* const cancel_session)(struct ast_fax_session *);
	/*! initiates the generation of silence to the fax session */
	int (* const generate_silence)(struct ast_fax_session *);
	/*! switches an existing dual-mode session from audio to T.38 */
	int (* const switch_to_t38)(struct ast_fax_session *);
	/*! displays capabilities of the fax technology */
	char * (* const cli_show_capabilities)(int);
	/*! displays details about the fax session */
	char * (* const cli_show_session)(struct ast_fax_session *, int);
	/*! displays statistics from the fax technology module */
	char * (* const cli_show_stats)(int);
	/*! displays settings from the fax technology module */
	char * (* const cli_show_settings)(int);
};
  
/*! \brief register a fax technology */
int ast_fax_tech_register(struct ast_fax_tech *tech);

/*! \brief unregister a fax technology */
void ast_fax_tech_unregister(struct ast_fax_tech *tech);

/*! \brief get the minimum supported fax rate */
unsigned int ast_fax_minrate(void);

/*! \brief get the maxiumum supported fax rate */
unsigned int ast_fax_maxrate(void);

/*! \brief convert an ast_fax_state to a string */
const char *ast_fax_state_to_str(enum ast_fax_state state);

/*!
 * \brief Log message at FAX or recommended level
 *
 * The first four parameters can be represented with Asterisk's
 * LOG_* levels. In other words, this function may be called
 * like
 *
 * ast_fax_log(LOG_DEBUG, msg);
 */
void ast_fax_log(int level, const char *file, const int line, const char *function, const char *msg);

#endif
