/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2009, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 * Joshua Colp <jcolp@digium.com>
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
 * \brief Pluggable RTP Architecture
 * \author Joshua Colp <jcolp@digium.com>
 * \ref AstRTPEngine
 */

/*!
 * \page AstRTPEngine Asterisk RTP Engine API
 *
 * The purpose of this API is to provide a way for multiple RTP stacks to be
 * used inside of Asterisk without any module that uses RTP knowing any
 * different. To the module each RTP stack behaves the same.
 *
 * An RTP session is called an instance and is made up of a combination of codec
 * information, RTP engine, RTP properties, and address information. An engine
 * name may be passed in to explicitly choose an RTP stack to be used but a
 * default one will be used if none is provided. An address to use for RTP may
 * also be provided but the underlying RTP engine may choose a different address
 * depending on it's configuration.
 *
 * An RTP engine is the layer between the RTP engine core and the RTP stack
 * itself. The RTP engine core provides a set of callbacks to do various things
 * (such as write audio out) that the RTP engine has to have implemented.
 *
 * Glue is what binds an RTP instance to a channel. It is used to retrieve RTP
 * instance information when performing remote or local bridging and is used to
 * have the channel driver tell the remote side to change destination of the RTP
 * stream.
 *
 * Statistics from an RTP instance can be retrieved using the
 * ast_rtp_instance_get_stats API call. This essentially asks the RTP engine in
 * use to fill in a structure with the requested values. It is not required for
 * an RTP engine to support all statistic values.
 *
 * Properties allow behavior of the RTP engine and RTP engine core to be
 * changed. For example, there is a property named AST_RTP_PROPERTY_NAT which is
 * used to tell the RTP engine to enable symmetric RTP if it supports it. It is
 * not required for an RTP engine to support all properties.
 *
 * Codec information is stored using a separate data structure which has it's
 * own set of API calls to add/remove/retrieve information. They are used by the
 * module after an RTP instance is created so that payload information is
 * available for the RTP engine.
 */

#ifndef _ASTERISK_RTP_ENGINE_H
#define _ASTERISK_RTP_ENGINE_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include "asterisk/astobj2.h"
#include "asterisk/frame.h"
#include "asterisk/netsock2.h"
#include "asterisk/sched.h"
#include "asterisk/res_srtp.h"

/* Maximum number of payloads supported */
#define AST_RTP_MAX_PT 256

/* Maximum number of generations */
#define AST_RED_MAX_GENERATION 5

struct ast_rtp_instance;
struct ast_rtp_glue;

/*! RTP Properties that can be set on an RTP instance */
enum ast_rtp_property {
	/*! Enable symmetric RTP support */
	AST_RTP_PROPERTY_NAT = 0,
	/*! RTP instance will be carrying DTMF (using RFC2833) */
	AST_RTP_PROPERTY_DTMF,
	/*! Expect unreliable DTMF from remote party */
	AST_RTP_PROPERTY_DTMF_COMPENSATE,
	/*! Enable STUN support */
	AST_RTP_PROPERTY_STUN,
	/*! Enable RTCP support */
	AST_RTP_PROPERTY_RTCP,

	/*!
	 * \brief Maximum number of RTP properties supported
	 *
	 * \note THIS MUST BE THE LAST ENTRY IN THIS ENUM.
	 */
	AST_RTP_PROPERTY_MAX,
};

/*! Additional RTP options */
enum ast_rtp_options {
	/*! Remote side is using non-standard G.726 */
	AST_RTP_OPT_G726_NONSTANDARD = (1 << 0),
};

/*! RTP DTMF Modes */
enum ast_rtp_dtmf_mode {
	/*! No DTMF is being carried over the RTP stream */
	AST_RTP_DTMF_MODE_NONE = 0,
	/*! DTMF is being carried out of band using RFC2833 */
	AST_RTP_DTMF_MODE_RFC2833,
	/*! DTMF is being carried inband over the RTP stream */
	AST_RTP_DTMF_MODE_INBAND,
};

/*! Result codes when RTP glue is queried for information */
enum ast_rtp_glue_result {
	/*! No remote or local bridging is permitted */
	AST_RTP_GLUE_RESULT_FORBID = 0,
	/*! Move RTP stream to be remote between devices directly */
	AST_RTP_GLUE_RESULT_REMOTE,
	/*! Perform RTP engine level bridging if possible */
	AST_RTP_GLUE_RESULT_LOCAL,
};

/*! Field statistics that can be retrieved from an RTP instance */
enum ast_rtp_instance_stat_field {
	/*! Retrieve quality information */
	AST_RTP_INSTANCE_STAT_FIELD_QUALITY = 0,
	/*! Retrieve quality information about jitter */
	AST_RTP_INSTANCE_STAT_FIELD_QUALITY_JITTER,
	/*! Retrieve quality information about packet loss */
	AST_RTP_INSTANCE_STAT_FIELD_QUALITY_LOSS,
	/*! Retrieve quality information about round trip time */
	AST_RTP_INSTANCE_STAT_FIELD_QUALITY_RTT,
};

/*! Statistics that can be retrieved from an RTP instance */
enum ast_rtp_instance_stat {
	/*! Retrieve all statistics */
	AST_RTP_INSTANCE_STAT_ALL = 0,
	/*! Retrieve number of packets transmitted */
	AST_RTP_INSTANCE_STAT_TXCOUNT,
	/*! Retrieve number of packets received */
	AST_RTP_INSTANCE_STAT_RXCOUNT,
	/*! Retrieve ALL statistics relating to packet loss */
	AST_RTP_INSTANCE_STAT_COMBINED_LOSS,
	/*! Retrieve number of packets lost for transmitting */
	AST_RTP_INSTANCE_STAT_TXPLOSS,
	/*! Retrieve number of packets lost for receiving */
	AST_RTP_INSTANCE_STAT_RXPLOSS,
	/*! Retrieve maximum number of packets lost on remote side */
	AST_RTP_INSTANCE_STAT_REMOTE_MAXRXPLOSS,
	/*! Retrieve minimum number of packets lost on remote side */
	AST_RTP_INSTANCE_STAT_REMOTE_MINRXPLOSS,
	/*! Retrieve average number of packets lost on remote side */
	AST_RTP_INSTANCE_STAT_REMOTE_NORMDEVRXPLOSS,
	/*! Retrieve standard deviation of packets lost on remote side */
	AST_RTP_INSTANCE_STAT_REMOTE_STDEVRXPLOSS,
	/*! Retrieve maximum number of packets lost on local side */
	AST_RTP_INSTANCE_STAT_LOCAL_MAXRXPLOSS,
	/*! Retrieve minimum number of packets lost on local side */
	AST_RTP_INSTANCE_STAT_LOCAL_MINRXPLOSS,
	/*! Retrieve average number of packets lost on local side */
	AST_RTP_INSTANCE_STAT_LOCAL_NORMDEVRXPLOSS,
	/*! Retrieve standard deviation of packets lost on local side */
	AST_RTP_INSTANCE_STAT_LOCAL_STDEVRXPLOSS,
	/*! Retrieve ALL statistics relating to jitter */
	AST_RTP_INSTANCE_STAT_COMBINED_JITTER,
	/*! Retrieve jitter on transmitted packets */
	AST_RTP_INSTANCE_STAT_TXJITTER,
	/*! Retrieve jitter on received packets */
	AST_RTP_INSTANCE_STAT_RXJITTER,
	/*! Retrieve maximum jitter on remote side */
	AST_RTP_INSTANCE_STAT_REMOTE_MAXJITTER,
	/*! Retrieve minimum jitter on remote side */
	AST_RTP_INSTANCE_STAT_REMOTE_MINJITTER,
	/*! Retrieve average jitter on remote side */
	AST_RTP_INSTANCE_STAT_REMOTE_NORMDEVJITTER,
	/*! Retrieve standard deviation jitter on remote side */
	AST_RTP_INSTANCE_STAT_REMOTE_STDEVJITTER,
	/*! Retrieve maximum jitter on local side */
	AST_RTP_INSTANCE_STAT_LOCAL_MAXJITTER,
	/*! Retrieve minimum jitter on local side */
	AST_RTP_INSTANCE_STAT_LOCAL_MINJITTER,
	/*! Retrieve average jitter on local side */
	AST_RTP_INSTANCE_STAT_LOCAL_NORMDEVJITTER,
	/*! Retrieve standard deviation jitter on local side */
	AST_RTP_INSTANCE_STAT_LOCAL_STDEVJITTER,
	/*! Retrieve ALL statistics relating to round trip time */
	AST_RTP_INSTANCE_STAT_COMBINED_RTT,
	/*! Retrieve round trip time */
	AST_RTP_INSTANCE_STAT_RTT,
	/*! Retrieve maximum round trip time */
	AST_RTP_INSTANCE_STAT_MAX_RTT,
	/*! Retrieve minimum round trip time */
	AST_RTP_INSTANCE_STAT_MIN_RTT,
	/*! Retrieve average round trip time */
	AST_RTP_INSTANCE_STAT_NORMDEVRTT,
	/*! Retrieve standard deviation round trip time */
	AST_RTP_INSTANCE_STAT_STDEVRTT,
	/*! Retrieve local SSRC */
	AST_RTP_INSTANCE_STAT_LOCAL_SSRC,
	/*! Retrieve remote SSRC */
	AST_RTP_INSTANCE_STAT_REMOTE_SSRC,
};

/* Codes for RTP-specific data - not defined by our AST_FORMAT codes */
/*! DTMF (RFC2833) */
#define AST_RTP_DTMF                    (1 << 0)
/*! 'Comfort Noise' (RFC3389) */
#define AST_RTP_CN                      (1 << 1)
/*! DTMF (Cisco Proprietary) */
#define AST_RTP_CISCO_DTMF              (1 << 2)
/*! Maximum RTP-specific code */
#define AST_RTP_MAX                     AST_RTP_CISCO_DTMF

/*! Structure that represents a payload */
struct ast_rtp_payload_type {
	/*! Is this an Asterisk value */
	int asterisk_format;
	/*! If asterisk_format is set, this is the internal
	 * asterisk format represented by the payload */
	struct ast_format format;
	/*! Actual internal RTP specific value of the payload */
	int rtp_code;

};

/*! Structure that represents statistics from an RTP instance */
struct ast_rtp_instance_stats {
	/*! Number of packets transmitted */
	unsigned int txcount;
	/*! Number of packets received */
	unsigned int rxcount;
	/*! Jitter on transmitted packets */
	double txjitter;
	/*! Jitter on received packets */
	double rxjitter;
	/*! Maximum jitter on remote side */
	double remote_maxjitter;
	/*! Minimum jitter on remote side */
	double remote_minjitter;
	/*! Average jitter on remote side */
	double remote_normdevjitter;
	/*! Standard deviation jitter on remote side */
	double remote_stdevjitter;
	/*! Maximum jitter on local side */
	double local_maxjitter;
	/*! Minimum jitter on local side */
	double local_minjitter;
	/*! Average jitter on local side */
	double local_normdevjitter;
	/*! Standard deviation jitter on local side */
	double local_stdevjitter;
	/*! Number of transmitted packets lost */
	unsigned int txploss;
	/*! Number of received packets lost */
	unsigned int rxploss;
	/*! Maximum number of packets lost on remote side */
	double remote_maxrxploss;
	/*! Minimum number of packets lost on remote side */
	double remote_minrxploss;
	/*! Average number of packets lost on remote side */
	double remote_normdevrxploss;
	/*! Standard deviation packets lost on remote side */
	double remote_stdevrxploss;
	/*! Maximum number of packets lost on local side */
	double local_maxrxploss;
	/*! Minimum number of packets lost on local side */
	double local_minrxploss;
	/*! Average number of packets lost on local side */
	double local_normdevrxploss;
	/*! Standard deviation packets lost on local side */
	double local_stdevrxploss;
	/*! Total round trip time */
	double rtt;
	/*! Maximum round trip time */
	double maxrtt;
	/*! Minimum round trip time */
	double minrtt;
	/*! Average round trip time */
	double normdevrtt;
	/*! Standard deviation round trip time */
	double stdevrtt;
	/*! Our SSRC */
	unsigned int local_ssrc;
	/*! Their SSRC */
	unsigned int remote_ssrc;
};

#define AST_RTP_STAT_SET(current_stat, combined, placement, value) \
if (stat == current_stat || stat == AST_RTP_INSTANCE_STAT_ALL || (combined >= 0 && combined == current_stat)) { \
placement = value; \
if (stat == current_stat) { \
return 0; \
} \
}

#define AST_RTP_STAT_TERMINATOR(combined) \
if (stat == combined) { \
return 0; \
}

/*! Structure that represents an RTP stack (engine) */
struct ast_rtp_engine {
	/*! Name of the RTP engine, used when explicitly requested */
	const char *name;
	/*! Module this RTP engine came from, used for reference counting */
	struct ast_module *mod;
	/*! Callback for setting up a new RTP instance */
	int (*new)(struct ast_rtp_instance *instance, struct ast_sched_context *sched, struct ast_sockaddr *sa, void *data);
	/*! Callback for destroying an RTP instance */
	int (*destroy)(struct ast_rtp_instance *instance);
	/*! Callback for writing out a frame */
	int (*write)(struct ast_rtp_instance *instance, struct ast_frame *frame);
	/*! Callback for stopping the RTP instance */
	void (*stop)(struct ast_rtp_instance *instance);
	/*! Callback for starting RFC2833 DTMF transmission */
	int (*dtmf_begin)(struct ast_rtp_instance *instance, char digit);
	/*! Callback for stopping RFC2833 DTMF transmission */
	int (*dtmf_end)(struct ast_rtp_instance *instance, char digit);
	int (*dtmf_end_with_duration)(struct ast_rtp_instance *instance, char digit, unsigned int duration);
	/*! Callback to indicate that we should update the marker bit */
	void (*update_source)(struct ast_rtp_instance *instance);
	/*! Callback to indicate that we should update the marker bit and ssrc */
	void (*change_source)(struct ast_rtp_instance *instance);
	/*! Callback for setting an extended RTP property */
	int (*extended_prop_set)(struct ast_rtp_instance *instance, int property, void *value);
	/*! Callback for getting an extended RTP property */
	void *(*extended_prop_get)(struct ast_rtp_instance *instance, int property);
	/*! Callback for setting an RTP property */
	void (*prop_set)(struct ast_rtp_instance *instance, enum ast_rtp_property property, int value);
	/*! Callback for setting a payload.  If asterisk  is to be used, asterisk_format will be set, otherwise value in code is used. */
	void (*payload_set)(struct ast_rtp_instance *instance, int payload, int asterisk_format, struct ast_format *format, int code);
	/*! Callback for setting packetization preferences */
	void (*packetization_set)(struct ast_rtp_instance *instance, struct ast_codec_pref *pref);
	/*! Callback for setting the remote address that RTP is to be sent to */
	void (*remote_address_set)(struct ast_rtp_instance *instance, struct ast_sockaddr *sa);
	/*! Callback for setting an alternate remote address */
	void (*alt_remote_address_set)(struct ast_rtp_instance *instance, struct ast_sockaddr *sa);
	/*! Callback for changing DTMF mode */
	int (*dtmf_mode_set)(struct ast_rtp_instance *instance, enum ast_rtp_dtmf_mode dtmf_mode);
	/*! Callback for getting DTMF mode */
	enum ast_rtp_dtmf_mode (*dtmf_mode_get)(struct ast_rtp_instance *instance);
	/*! Callback for retrieving statistics */
	int (*get_stat)(struct ast_rtp_instance *instance, struct ast_rtp_instance_stats *stats, enum ast_rtp_instance_stat stat);
	/*! Callback for setting QoS values */
	int (*qos)(struct ast_rtp_instance *instance, int tos, int cos, const char *desc);
	/*! Callback for retrieving a file descriptor to poll on, not always required */
	int (*fd)(struct ast_rtp_instance *instance, int rtcp);
	/*! Callback for initializing RED support */
	int (*red_init)(struct ast_rtp_instance *instance, int buffer_time, int *payloads, int generations);
	/*! Callback for buffering a frame using RED */
	int (*red_buffer)(struct ast_rtp_instance *instance, struct ast_frame *frame);
	/*! Callback for reading a frame from the RTP engine */
	struct ast_frame *(*read)(struct ast_rtp_instance *instance, int rtcp);
	/*! Callback to locally bridge two RTP instances */
	int (*local_bridge)(struct ast_rtp_instance *instance0, struct ast_rtp_instance *instance1);
	/*! Callback to set the read format */
	int (*set_read_format)(struct ast_rtp_instance *instance, struct ast_format *format);
	/*! Callback to set the write format */
	int (*set_write_format)(struct ast_rtp_instance *instance, struct ast_format *format);
	/*! Callback to make two instances compatible */
	int (*make_compatible)(struct ast_channel *chan0, struct ast_rtp_instance *instance0, struct ast_channel *chan1, struct ast_rtp_instance *instance1);
	/*! Callback to see if two instances are compatible with DTMF */
	int (*dtmf_compatible)(struct ast_channel *chan0, struct ast_rtp_instance *instance0, struct ast_channel *chan1, struct ast_rtp_instance *instance1);
	/*! Callback to indicate that packets will now flow */
	int (*activate)(struct ast_rtp_instance *instance);
	/*! Callback to request that the RTP engine send a STUN BIND request */
	void (*stun_request)(struct ast_rtp_instance *instance, struct ast_sockaddr *suggestion, const char *username);
	/*! Callback to get the transcodeable formats supported. result returned in ast_format_cap *result */
	void (*available_formats)(struct ast_rtp_instance *instance, struct ast_format_cap *to_endpoint, struct ast_format_cap *to_asterisk, struct ast_format_cap *result);
	/*! Callback to send CNG */
	int (*sendcng)(struct ast_rtp_instance *instance, int level);
	/*! Linked list information */
	AST_RWLIST_ENTRY(ast_rtp_engine) entry;
};

/*! Structure that represents codec and packetization information */
struct ast_rtp_codecs {
	/*! Codec packetization preferences */
	struct ast_codec_pref pref;
	/*! Payloads present */
	struct ast_rtp_payload_type payloads[AST_RTP_MAX_PT];
};

/*! Structure that represents the glue that binds an RTP instance to a channel */
struct ast_rtp_glue {
	/*! Name of the channel driver that this glue is responsible for */
	const char *type;
	/*! Module that the RTP glue came from */
	struct ast_module *mod;
	/*!
	 * \brief Callback for retrieving the RTP instance carrying audio
	 * \note This function increases the reference count on the returned RTP instance.
	 */
	enum ast_rtp_glue_result (*get_rtp_info)(struct ast_channel *chan, struct ast_rtp_instance **instance);
	/*!
	 * \brief Callback for retrieving the RTP instance carrying video
	 * \note This function increases the reference count on the returned RTP instance.
	 */
	enum ast_rtp_glue_result (*get_vrtp_info)(struct ast_channel *chan, struct ast_rtp_instance **instance);
	/*!
	 * \brief Callback for retrieving the RTP instance carrying text
	 * \note This function increases the reference count on the returned RTP instance.
	 */
	enum ast_rtp_glue_result (*get_trtp_info)(struct ast_channel *chan, struct ast_rtp_instance **instance);
	/*! Callback for updating the destination that the remote side should send RTP to */
	int (*update_peer)(struct ast_channel *chan, struct ast_rtp_instance *instance, struct ast_rtp_instance *vinstance, struct ast_rtp_instance *tinstance, const struct ast_format_cap *cap, int nat_active);
	/*! Callback for retrieving codecs that the channel can do.  Result returned in result_cap*/
	void (*get_codec)(struct ast_channel *chan, struct ast_format_cap *result_cap);
	/*! Linked list information */
	AST_RWLIST_ENTRY(ast_rtp_glue) entry;
};

#define ast_rtp_engine_register(engine) ast_rtp_engine_register2(engine, ast_module_info->self)

/*!
 * \brief Register an RTP engine
 *
 * \param engine Structure of the RTP engine to register
 * \param module Module that the RTP engine is part of
 *
 * \retval 0 success
 * \retval -1 failure
 *
 * Example usage:
 *
 * \code
 * ast_rtp_engine_register2(&example_rtp_engine, NULL);
 * \endcode
 *
 * This registers the RTP engine declared as example_rtp_engine with the RTP engine core, but does not
 * associate a module with it.
 *
 * \note It is recommended that you use the ast_rtp_engine_register macro so that the module is
 *       associated with the RTP engine and use counting is performed.
 *
 * \since 1.8
 */
int ast_rtp_engine_register2(struct ast_rtp_engine *engine, struct ast_module *module);

/*!
 * \brief Unregister an RTP engine
 *
 * \param engine Structure of the RTP engine to unregister
 *
 * \retval 0 success
 * \retval -1 failure
 *
 * Example usage:
 *
 * \code
 * ast_rtp_engine_unregister(&example_rtp_engine);
 * \endcode
 *
 * This unregisters the RTP engine declared as example_rtp_engine from the RTP engine core. If a module
 * reference was provided when it was registered then this will only be called once the RTP engine is no longer in use.
 *
 * \since 1.8
 */
int ast_rtp_engine_unregister(struct ast_rtp_engine *engine);

int ast_rtp_engine_register_srtp(struct ast_srtp_res *srtp_res, struct ast_srtp_policy_res *policy_res);

void ast_rtp_engine_unregister_srtp(void);
int ast_rtp_engine_srtp_is_registered(void);

#define ast_rtp_glue_register(glue) ast_rtp_glue_register2(glue, ast_module_info->self)

/*!
 * \brief Register RTP glue
 *
 * \param glue The glue to register
 * \param module Module that the RTP glue is part of
 *
 * \retval 0 success
 * \retval -1 failure
 *
 * Example usage:
 *
 * \code
 * ast_rtp_glue_register2(&example_rtp_glue, NULL);
 * \endcode
 *
 * This registers the RTP glue declared as example_rtp_glue with the RTP engine core, but does not
 * associate a module with it.
 *
 * \note It is recommended that you use the ast_rtp_glue_register macro so that the module is
 *       associated with the RTP glue and use counting is performed.
 *
 * \since 1.8
 */
int ast_rtp_glue_register2(struct ast_rtp_glue *glue, struct ast_module *module);

/*!
 * \brief Unregister RTP glue
 *
 * \param glue The glue to unregister
 *
 * \retval 0 success
 * \retval -1 failure
 *
 * Example usage:
 *
 * \code
 * ast_rtp_glue_unregister(&example_rtp_glue);
 * \endcode
 *
 * This unregisters the RTP glue declared as example_rtp_gkue from the RTP engine core. If a module
 * reference was provided when it was registered then this will only be called once the RTP engine is no longer in use.
 *
 * \since 1.8
 */
int ast_rtp_glue_unregister(struct ast_rtp_glue *glue);

/*!
 * \brief Create a new RTP instance
 *
 * \param engine_name Name of the engine to use for the RTP instance
 * \param sched Scheduler context that the RTP engine may want to use
 * \param sa Address we want to bind to
 * \param data Unique data for the engine
 *
 * \retval non-NULL success
 * \retval NULL failure
 *
 * Example usage:
 *
 * \code
 * struct ast_rtp_instance *instance = NULL;
 * instance = ast_rtp_instance_new(NULL, sched, &sin, NULL);
 * \endcode
 *
 * This creates a new RTP instance using the default engine and asks the RTP engine to bind to the address given
 * in the address structure.
 *
 * \note The RTP engine does not have to use the address provided when creating an RTP instance. It may choose to use
 *       another depending on it's own configuration.
 *
 * \since 1.8
 */
struct ast_rtp_instance *ast_rtp_instance_new(const char *engine_name,
                struct ast_sched_context *sched, const struct ast_sockaddr *sa,
                void *data);

/*!
 * \brief Destroy an RTP instance
 *
 * \param instance The RTP instance to destroy
 *
 * \retval 0 success
 * \retval -1 failure
 *
 * Example usage:
 *
 * \code
 * ast_rtp_instance_destroy(instance);
 * \endcode
 *
 * This destroys the RTP instance pointed to by instance. Once this function returns instance no longer points to valid
 * memory and may not be used again.
 *
 * \since 1.8
 */
int ast_rtp_instance_destroy(struct ast_rtp_instance *instance);

/*!
 * \brief Set the data portion of an RTP instance
 *
 * \param instance The RTP instance to manipulate
 * \param data Pointer to data
 *
 * Example usage:
 *
 * \code
 * ast_rtp_instance_set_data(instance, blob);
 * \endcode
 *
 * This sets the data pointer on the RTP instance pointed to by 'instance' to
 * blob.
 *
 * \since 1.8
 */
void ast_rtp_instance_set_data(struct ast_rtp_instance *instance, void *data);

/*!
 * \brief Get the data portion of an RTP instance
 *
 * \param instance The RTP instance we want the data portion from
 *
 * Example usage:
 *
 * \code
 * struct *blob = ast_rtp_instance_get_data(instance);
 ( \endcode
 *
 * This gets the data pointer on the RTP instance pointed to by 'instance'.
 *
 * \since 1.8
 */
void *ast_rtp_instance_get_data(struct ast_rtp_instance *instance);

/*!
 * \brief Send a frame out over RTP
 *
 * \param instance The RTP instance to send frame out on
 * \param frame the frame to send out
 *
 * \retval 0 success
 * \retval -1 failure
 *
 * Example usage:
 *
 * \code
 * ast_rtp_instance_write(instance, frame);
 * \endcode
 *
 * This gives the frame pointed to by frame to the RTP engine being used for the instance
 * and asks that it be transmitted to the current remote address set on the RTP instance.
 *
 * \since 1.8
 */
int ast_rtp_instance_write(struct ast_rtp_instance *instance, struct ast_frame *frame);

/*!
 * \brief Receive a frame over RTP
 *
 * \param instance The RTP instance to receive frame on
 * \param rtcp Whether to read in RTCP or not
 *
 * \retval non-NULL success
 * \retval NULL failure
 *
 * Example usage:
 *
 * \code
 * struct ast_frame *frame;
 * frame = ast_rtp_instance_read(instance, 0);
 * \endcode
 *
 * This asks the RTP engine to read in RTP from the instance and return it as an Asterisk frame.
 *
 * \since 1.8
 */
struct ast_frame *ast_rtp_instance_read(struct ast_rtp_instance *instance, int rtcp);

/*!
 * \brief Set the address of the remote endpoint that we are sending RTP to
 *
 * \param instance The RTP instance to change the address on
 * \param address Address to set it to
 *
 * \retval 0 success
 * \retval -1 failure
 *
 * Example usage:
 *
 * \code
 * ast_rtp_instance_set_remote_address(instance, &sin);
 * \endcode
 *
 * This changes the remote address that RTP will be sent to on instance to the address given in the sin
 * structure.
 *
 * \since 1.8
 */
int ast_rtp_instance_set_remote_address(struct ast_rtp_instance *instance, const struct ast_sockaddr *address);


/*!
 * \brief Set the address of an an alternate RTP address to receive from
 *
 * \param instance The RTP instance to change the address on
 * \param address Address to set it to
 *
 * \retval 0 success
 * \retval -1 failure
 *
 * Example usage:
 *
 * \code
 * ast_rtp_instance_set_alt_remote_address(instance, &address);
 * \endcode
 *
 * This changes the alternate remote address that RTP will be sent to on instance to the address given in the sin
 * structure.
 *
 * \since 1.8
 */
int ast_rtp_instance_set_alt_remote_address(struct ast_rtp_instance *instance, const struct ast_sockaddr *address);

/*!
 * \brief Set the address that we are expecting to receive RTP on
 *
 * \param instance The RTP instance to change the address on
 * \param address Address to set it to
 *
 * \retval 0 success
 * \retval -1 failure
 *
 * Example usage:
 *
 * \code
 * ast_rtp_instance_set_local_address(instance, &sin);
 * \endcode
 *
 * This changes the local address that RTP is expected on to the address given in the sin
 * structure.
 *
 * \since 1.8
 */
int ast_rtp_instance_set_local_address(struct ast_rtp_instance *instance,
                const struct ast_sockaddr *address);

/*!
 * \brief Get the local address that we are expecting RTP on
 *
 * \param instance The RTP instance to get the address from
 * \param address The variable to store the address in
 *
 * Example usage:
 *
 * \code
 * struct ast_sockaddr address;
 * ast_rtp_instance_get_local_address(instance, &address);
 * \endcode
 *
 * This gets the local address that we are expecting RTP on and stores it in the 'address' structure.
 *
 * \since 1.8
 */
void ast_rtp_instance_get_local_address(struct ast_rtp_instance *instance, struct ast_sockaddr *address);

/*!
 * \brief Get the address of the local endpoint that we are sending RTP to, comparing its address to another
 *
 * \param instance The instance that we want to get the local address for
 * \param address An initialized address that may be overwritten if the local address is different
 *
 * \retval 0 address was not changed
 * \retval 1 address was changed
 * Example usage:
 *
 * \code
 * struct ast_sockaddr address;
 * int ret;
 * ret = ast_rtp_instance_get_and_cmp_local_address(instance, &address);
 * \endcode
 *
 * This retrieves the current local address set on the instance pointed to by instance and puts the value
 * into the address structure.
 *
 * \since 1.8
 */
int ast_rtp_instance_get_and_cmp_local_address(struct ast_rtp_instance *instance, struct ast_sockaddr *address);

/*!
 * \brief Get the address of the remote endpoint that we are sending RTP to
 *
 * \param instance The instance that we want to get the remote address for
 * \param address A structure to put the address into
 *
 * Example usage:
 *
 * \code
 * struct ast_sockaddr address;
 * ast_rtp_instance_get_remote_address(instance, &address);
 * \endcode
 *
 * This retrieves the current remote address set on the instance pointed to by instance and puts the value
 * into the address structure.
 *
 * \since 1.8
 */
void ast_rtp_instance_get_remote_address(struct ast_rtp_instance *instance, struct ast_sockaddr *address);

/*!
 * \brief Get the address of the remote endpoint that we are sending RTP to, comparing its address to another
 *
 * \param instance The instance that we want to get the remote address for
 * \param address An initialized address that may be overwritten if the remote address is different
 *
 * \retval 0 address was not changed
 * \retval 1 address was changed
 * Example usage:
 *
 * \code
 * struct ast_sockaddr address;
 * int ret;
 * ret = ast_rtp_instance_get_and_cmp_remote_address(instance, &address);
 * \endcode
 *
 * This retrieves the current remote address set on the instance pointed to by instance and puts the value
 * into the address structure.
 *
 * \since 1.8
 */

int ast_rtp_instance_get_and_cmp_remote_address(struct ast_rtp_instance *instance, struct ast_sockaddr *address);

/*!
 * \brief Set the value of an RTP instance extended property
 *
 * \param instance The RTP instance to set the extended property on
 * \param property The extended property to set
 * \param value The value to set the extended property to
 *
 * \since 1.8
 */
void ast_rtp_instance_set_extended_prop(struct ast_rtp_instance *instance, int property, void *value);

/*!
 * \brief Get the value of an RTP instance extended property
 *
 * \param instance The RTP instance to get the extended property on
 * \param property The extended property to get
 *
 * \since 1.8
 */
void *ast_rtp_instance_get_extended_prop(struct ast_rtp_instance *instance, int property);

/*!
 * \brief Set the value of an RTP instance property
 *
 * \param instance The RTP instance to set the property on
 * \param property The property to modify
 * \param value The value to set the property to
 *
 * Example usage:
 *
 * \code
 * ast_rtp_instance_set_prop(instance, AST_RTP_PROPERTY_NAT, 1);
 * \endcode
 *
 * This enables the AST_RTP_PROPERTY_NAT property on the instance pointed to by instance.
 *
 * \since 1.8
 */
void ast_rtp_instance_set_prop(struct ast_rtp_instance *instance, enum ast_rtp_property property, int value);

/*!
 * \brief Get the value of an RTP instance property
 *
 * \param instance The RTP instance to get the property from
 * \param property The property to get
 *
 * \retval Current value of the property
 *
 * Example usage:
 *
 * \code
 * ast_rtp_instance_get_prop(instance, AST_RTP_PROPERTY_NAT);
 * \endcode
 *
 * This returns the current value of the NAT property on the instance pointed to by instance.
 *
 * \since 1.8
 */
int ast_rtp_instance_get_prop(struct ast_rtp_instance *instance, enum ast_rtp_property property);

/*!
 * \brief Get the codecs structure of an RTP instance
 *
 * \param instance The RTP instance to get the codecs structure from
 *
 * Example usage:
 *
 * \code
 * struct ast_rtp_codecs *codecs = ast_rtp_instance_get_codecs(instance);
 * \endcode
 *
 * This gets the codecs structure on the RTP instance pointed to by 'instance'.
 *
 * \since 1.8
 */
struct ast_rtp_codecs *ast_rtp_instance_get_codecs(struct ast_rtp_instance *instance);

/*!
 * \brief Clear payload information from an RTP instance
 *
 * \param codecs The codecs structure that payloads will be cleared from
 * \param instance Optionally the instance that the codecs structure belongs to
 *
 * Example usage:
 *
 * \code
 * struct ast_rtp_codecs codecs;
 * ast_rtp_codecs_payloads_clear(&codecs, NULL);
 * \endcode
 *
 * This clears the codecs structure and puts it into a pristine state.
 *
 * \since 1.8
 */
void ast_rtp_codecs_payloads_clear(struct ast_rtp_codecs *codecs, struct ast_rtp_instance *instance);

/*!
 * \brief Set payload information on an RTP instance to the default
 *
 * \param codecs The codecs structure to set defaults on
 * \param instance Optionally the instance that the codecs structure belongs to
 *
 * Example usage:
 *
 * \code
 * struct ast_rtp_codecs codecs;
 * ast_rtp_codecs_payloads_default(&codecs, NULL);
 * \endcode
 *
 * This sets the default payloads on the codecs structure.
 *
 * \since 1.8
 */
void ast_rtp_codecs_payloads_default(struct ast_rtp_codecs *codecs, struct ast_rtp_instance *instance);

/*!
 * \brief Copy payload information from one RTP instance to another
 *
 * \param src The source codecs structure
 * \param dest The destination codecs structure that the values from src will be copied to
 * \param instance Optionally the instance that the dst codecs structure belongs to
 *
 * Example usage:
 *
 * \code
 * ast_rtp_codecs_payloads_copy(&codecs0, &codecs1, NULL);
 * \endcode
 *
 * This copies the payloads from the codecs0 structure to the codecs1 structure, overwriting any current values.
 *
 * \since 1.8
 */
void ast_rtp_codecs_payloads_copy(struct ast_rtp_codecs *src, struct ast_rtp_codecs *dest, struct ast_rtp_instance *instance);

/*!
 * \brief Record payload information that was seen in an m= SDP line
 *
 * \param codecs The codecs structure to muck with
 * \param instance Optionally the instance that the codecs structure belongs to
 * \param payload Numerical payload that was seen in the m= SDP line
 *
 * Example usage:
 *
 * \code
 * ast_rtp_codecs_payloads_set_m_type(&codecs, NULL, 0);
 * \endcode
 *
 * This records that the numerical payload '0' was seen in the codecs structure.
 *
 * \since 1.8
 */
void ast_rtp_codecs_payloads_set_m_type(struct ast_rtp_codecs *codecs, struct ast_rtp_instance *instance, int payload);

/*!
 * \brief Record payload information that was seen in an a=rtpmap: SDP line
 *
 * \param codecs The codecs structure to muck with
 * \param instance Optionally the instance that the codecs structure belongs to
 * \param payload Numerical payload that was seen in the a=rtpmap: SDP line
 * \param mimetype The string mime type that was seen
 * \param mimesubtype The strin mime sub type that was seen
 * \param options Optional options that may change the behavior of this specific payload
 *
 * \retval 0 success
 * \retval -1 failure, invalid payload numbe
 * \retval -2 failure, unknown mimetype
 *
 * Example usage:
 *
 * \code
 * ast_rtp_codecs_payloads_set_rtpmap_type(&codecs, NULL, 0, "audio", "PCMU", 0);
 * \endcode
 *
 * This records that the numerical payload '0' was seen with mime type 'audio' and sub mime type 'PCMU' in the codecs structure.
 *
 * \since 1.8
 */
int ast_rtp_codecs_payloads_set_rtpmap_type(struct ast_rtp_codecs *codecs, struct ast_rtp_instance *instance, int payload, char *mimetype, char *mimesubtype, enum ast_rtp_options options);

/*!
 * \brief Set payload type to a known MIME media type for a codec with a specific sample rate
 *
 * \param codecs RTP structure to modify
 * \param instance Optionally the instance that the codecs structure belongs to
 * \param pt Payload type entry to modify
 * \param mimetype top-level MIME type of media stream (typically "audio", "video", "text", etc.)
 * \param mimesubtype MIME subtype of media stream (typically a codec name)
 * \param options Zero or more flags from the ast_rtp_options enum
 * \param sample_rate The sample rate of the media stream
 *
 * This function 'fills in' an entry in the list of possible formats for
 * a media stream associated with an RTP structure.
 *
 * \retval 0 on success
 * \retval -1 if the payload type is out of range
 * \retval -2 if the mimeType/mimeSubtype combination was not found
 *
 * \since 1.8
 */
int ast_rtp_codecs_payloads_set_rtpmap_type_rate(struct ast_rtp_codecs *codecs, struct ast_rtp_instance *instance, int pt,
				  char *mimetype, char *mimesubtype,
				  enum ast_rtp_options options,
				  unsigned int sample_rate);

/*!
 * \brief Remove payload information
 *
 * \param codecs The codecs structure to muck with
 * \param instance Optionally the instance that the codecs structure belongs to
 * \param payload Numerical payload to unset
 *
 * Example usage:
 *
 * \code
 * ast_rtp_codecs_payloads_unset(&codecs, NULL, 0);
 * \endcode
 *
 * This clears the payload '0' from the codecs structure. It will be as if it was never set.
 *
 * \since 1.8
 */
void ast_rtp_codecs_payloads_unset(struct ast_rtp_codecs *codecs, struct ast_rtp_instance *instance, int payload);

/*!
 * \brief Retrieve payload information by payload
 *
 * \param codecs Codecs structure to look in
 * \param payload Numerical payload to look up
 *
 * \retval Payload information
 *
 * Example usage:
 *
 * \code
 * struct ast_rtp_payload_type payload_type;
 * payload_type = ast_rtp_codecs_payload_lookup(&codecs, 0);
 * \endcode
 *
 * This looks up the information for payload '0' from the codecs structure.
 *
 * \since 1.8
 */
struct ast_rtp_payload_type ast_rtp_codecs_payload_lookup(struct ast_rtp_codecs *codecs, int payload);

/*!
 * \brief Retrieve the actual ast_format stored on the codecs structure for a specific payload
 *
 * \param codecs Codecs structure to look in
 * \param payload Numerical payload to look up
 *
 * \retval pointer to format structure on success
 * \retval NULL on failure
 *
 * \since 1.10
 */
struct ast_format *ast_rtp_codecs_get_payload_format(struct ast_rtp_codecs *codecs, int payload);

/*!
 * \brief Get the sample rate associated with known RTP payload types
 *
 * \param asterisk_format True if the value in format is to be used.
 * \param An asterisk format
 * \param code from AST_RTP list
 *
 * \return the sample rate if the format was found, zero if it was not found
 *
 * \since 1.8
 */
unsigned int ast_rtp_lookup_sample_rate2(int asterisk_format, struct ast_format *format, int code);

/*!
 * \brief Retrieve all formats that were found
 *
 * \param codecs Codecs structure to look in
 * \param astformats A capabilities structure to put the Asterisk formats in.
 * \param nonastformats An integer to put the non-Asterisk formats in
 *
 * Example usage:
 *
 * \code
 * struct ast_format_cap *astformats = ast_format_cap_alloc_nolock()
 * int nonastformats;
 * ast_rtp_codecs_payload_formats(&codecs, &astformats, &nonastformats);
 * \endcode
 *
 * This retrieves all the formats known about in the codecs structure and puts the Asterisk ones in the integer
 * pointed to by astformats and the non-Asterisk ones in the integer pointed to by nonastformats.
 *
 * \since 1.8
 */
void ast_rtp_codecs_payload_formats(struct ast_rtp_codecs *codecs, struct ast_format_cap *astformats, int *nonastformats);

/*!
 * \brief Retrieve a payload based on whether it is an Asterisk format and the code
 *
 * \param codecs Codecs structure to look in
 * \param asterisk_format Non-zero if the given Asterisk format is present
 * \param format Asterisk format to look for
 * \param code The format to look for
 *
 * \retval Numerical payload
 *
 * Example usage:
 *
 * \code
 * int payload = ast_rtp_codecs_payload_code(&codecs, 1, ast_format_set(&tmp_fmt, AST_FORMAT_ULAW, 0), 0);
 * \endcode
 *
 * This looks for the numerical payload for ULAW in the codecs structure.
 *
 * \since 1.8
 */
int ast_rtp_codecs_payload_code(struct ast_rtp_codecs *codecs, int asterisk_format, const struct ast_format *format, int code);

/*!
 * \brief Retrieve mime subtype information on a payload
 *
 * \param asterisk_format Non-zero to look up using Asterisk format
 * \param format Asterisk format to look up
 * \param code RTP code to look up
 * \param options Additional options that may change the result
 *
 * \retval Mime subtype success
 * \retval NULL failure
 *
 * Example usage:
 *
 * \code
 * const char *subtype = ast_rtp_lookup_mime_subtype2(1, ast_format_set(&tmp_fmt, AST_FORMAT_ULAW, 0), 0, 0);
 * \endcode
 *
 * This looks up the mime subtype for the ULAW format.
 *
 * \since 1.8
 */
const char *ast_rtp_lookup_mime_subtype2(const int asterisk_format, struct ast_format *format, int code, enum ast_rtp_options options);

/*!
 * \brief Convert formats into a string and put them into a buffer
 *
 * \param buf Buffer to put the mime output into
 * \param ast_format_capability Asterisk Formats we are looking up.
 * \param rtp_capability RTP codes that we are looking up
 * \param asterisk_format Non-zero if the ast_format_capability structure is to be used, 0 if rtp_capability is to be used
 * \param options Additional options that may change the result
 *
 * \retval non-NULL success
 * \retval NULL failure
 *
 * Example usage:
 *
 * \code
 * char buf[256] = "";
 * struct ast_format tmp_fmt;
 * struct ast_format_cap *cap = ast_format_cap_alloc_nolock();
 * ast_format_cap_add(cap, ast_format_set(&tmp_fmt, AST_FORMAT_ULAW, 0));
 * ast_format_cap_add(cap, ast_format_set(&tmp_fmt, AST_FORMAT_GSM, 0));
 * char *mime = ast_rtp_lookup_mime_multiple2(&buf, sizeof(buf), cap, 0, 1, 0);
 * ast_format_cap_destroy(cap);
 * \endcode
 *
 * This returns the mime values for ULAW and ALAW in the buffer pointed to by buf.
 *
 * \since 1.8
 */
char *ast_rtp_lookup_mime_multiple2(struct ast_str *buf, struct ast_format_cap *ast_format_capability, int rtp_capability, const int asterisk_format, enum ast_rtp_options options);

/*!
 * \brief Set codec packetization preferences
 *
 * \param codecs Codecs structure to muck with
 * \param instance Optionally the instance that the codecs structure belongs to
 * \param prefs Codec packetization preferences
 *
 * Example usage:
 *
 * \code
 * ast_rtp_codecs_packetization_set(&codecs, NULL, &prefs);
 * \endcode
 *
 * This sets the packetization preferences pointed to by prefs on the codecs structure pointed to by codecs.
 *
 * \since 1.8
 */
void ast_rtp_codecs_packetization_set(struct ast_rtp_codecs *codecs, struct ast_rtp_instance *instance, struct ast_codec_pref *prefs);

/*!
 * \brief Begin sending a DTMF digit
 *
 * \param instance The RTP instance to send the DTMF on
 * \param digit What DTMF digit to send
 *
 * \retval 0 success
 * \retval -1 failure
 *
 * Example usage:
 *
 * \code
 * ast_rtp_instance_dtmf_begin(instance, '1');
 * \endcode
 *
 * This starts sending the DTMF '1' on the RTP instance pointed to by instance. It will
 * continue being sent until it is ended.
 *
 * \since 1.8
 */
int ast_rtp_instance_dtmf_begin(struct ast_rtp_instance *instance, char digit);

/*!
 * \brief Stop sending a DTMF digit
 *
 * \param instance The RTP instance to stop the DTMF on
 * \param digit What DTMF digit to stop
 *
 * \retval 0 success
 * \retval -1 failure
 *
 * Example usage:
 *
 * \code
 * ast_rtp_instance_dtmf_end(instance, '1');
 * \endcode
 *
 * This stops sending the DTMF '1' on the RTP instance pointed to by instance.
 *
 * \since 1.8
 */
int ast_rtp_instance_dtmf_end(struct ast_rtp_instance *instance, char digit);
int ast_rtp_instance_dtmf_end_with_duration(struct ast_rtp_instance *instance, char digit, unsigned int duration);

/*!
 * \brief Set the DTMF mode that should be used
 *
 * \param instance the RTP instance to set DTMF mode on
 * \param dtmf_mode The DTMF mode that is in use
 *
 * \retval 0 success
 * \retval -1 failure
 *
 * Example usage:
 *
 * \code
 * ast_rtp_instance_dtmf_mode_set(instance, AST_RTP_DTMF_MODE_RFC2833);
 * \endcode
 *
 * This sets the RTP instance to use RFC2833 for DTMF transmission and receiving.
 *
 * \since 1.8
 */
int ast_rtp_instance_dtmf_mode_set(struct ast_rtp_instance *instance, enum ast_rtp_dtmf_mode dtmf_mode);

/*!
 * \brief Get the DTMF mode of an RTP instance
 *
 * \param instance The RTP instance to get the DTMF mode of
 *
 * \retval DTMF mode
 *
 * Example usage:
 *
 * \code
 * enum ast_rtp_dtmf_mode dtmf_mode = ast_rtp_instance_dtmf_mode_get(instance);
 * \endcode
 *
 * This gets the DTMF mode set on the RTP instance pointed to by 'instance'.
 *
 * \since 1.8
 */
enum ast_rtp_dtmf_mode ast_rtp_instance_dtmf_mode_get(struct ast_rtp_instance *instance);

/*!
 * \brief Indicate that the RTP marker bit should be set on an RTP stream
 *
 * \param instance Instance that the new media source is feeding into
 *
 * Example usage:
 *
 * \code
 * ast_rtp_instance_update_source(instance);
 * \endcode
 *
 * This indicates that the source of media that is feeding the instance pointed to by
 * instance has been updated and that the marker bit should be set.
 *
 * \since 1.8
 */
void ast_rtp_instance_update_source(struct ast_rtp_instance *instance);

/*!
 * \brief Indicate a new source of audio has dropped in and the ssrc should change
 *
 * \param instance Instance that the new media source is feeding into
 *
 * Example usage:
 *
 * \code
 * ast_rtp_instance_change_source(instance);
 * \endcode
 *
 * This indicates that the source of media that is feeding the instance pointed to by
 * instance has changed and that the marker bit should be set and the SSRC updated.
 *
 * \since 1.8
 */
void ast_rtp_instance_change_source(struct ast_rtp_instance *instance);

/*!
 * \brief Set QoS parameters on an RTP session
 *
 * \param instance Instance to set the QoS parameters on
 * \param tos Terms of service value
 * \param cos Class of service value
 * \param desc What is setting the QoS values
 *
 * \retval 0 success
 * \retval -1 failure
 *
 * Example usage:
 *
 * \code
 * ast_rtp_instance_set_qos(instance, 0, 0, "Example");
 * \endcode
 *
 * This sets the TOS and COS values to 0 on the instance pointed to by instance.
 *
 * \since 1.8
 */
int ast_rtp_instance_set_qos(struct ast_rtp_instance *instance, int tos, int cos, const char *desc);

/*!
 * \brief Stop an RTP instance
 *
 * \param instance Instance that media is no longer going to at this time
 *
 * Example usage:
 *
 * \code
 * ast_rtp_instance_stop(instance);
 * \endcode
 *
 * This tells the RTP engine being used for the instance pointed to by instance
 * that media is no longer going to it at this time, but may in the future.
 *
 * \since 1.8
 */
void ast_rtp_instance_stop(struct ast_rtp_instance *instance);

/*!
 * \brief Get the file descriptor for an RTP session (or RTCP)
 *
 * \param instance Instance to get the file descriptor for
 * \param rtcp Whether to retrieve the file descriptor for RTCP or not
 *
 * \retval fd success
 * \retval -1 failure
 *
 * Example usage:
 *
 * \code
 * int rtp_fd = ast_rtp_instance_fd(instance, 0);
 * \endcode
 *
 * This retrieves the file descriptor for the socket carrying media on the instance
 * pointed to by instance.
 *
 * \since 1.8
 */
int ast_rtp_instance_fd(struct ast_rtp_instance *instance, int rtcp);

/*!
 * \brief Get the RTP glue that binds a channel to the RTP engine
 *
 * \param type Name of the glue we want
 *
 * \retval non-NULL success
 * \retval NULL failure
 *
 * Example usage:
 *
 * \code
 * struct ast_rtp_glue *glue = ast_rtp_instance_get_glue("Example");
 * \endcode
 *
 * This retrieves the RTP glue that has the name 'Example'.
 *
 * \since 1.8
 */
struct ast_rtp_glue *ast_rtp_instance_get_glue(const char *type);

/*!
 * \brief Bridge two channels that use RTP instances
 *
 * \param c0 First channel part of the bridge
 * \param c1 Second channel part of the bridge
 * \param flags Bridging flags
 * \param fo If a frame needs to be passed up it is stored here
 * \param rc Channel that passed the above frame up
 * \param timeoutms How long the channels should be bridged for
 *
 * \retval Bridge result
 *
 * \note This should only be used by channel drivers in their technology declaration.
 *
 * \since 1.8
 */
enum ast_bridge_result ast_rtp_instance_bridge(struct ast_channel *c0, struct ast_channel *c1, int flags, struct ast_frame **fo, struct ast_channel **rc, int timeoutms);

/*!
 * \brief Get the other RTP instance that an instance is bridged to
 *
 * \param instance The RTP instance that we want
 *
 * \retval non-NULL success
 * \retval NULL failure
 *
 * Example usage:
 *
 * \code
 * struct ast_rtp_instance *bridged = ast_rtp_instance_get_bridged(instance0);
 * \endcode
 *
 * This gets the RTP instance that instance0 is bridged to.
 *
 * \since 1.8
 */
struct ast_rtp_instance *ast_rtp_instance_get_bridged(struct ast_rtp_instance *instance);

/*!
 * \brief Make two channels compatible for early bridging
 *
 * \param c0 First channel part of the bridge
 * \param c1 Second channel part of the bridge
 *
 * \since 1.8
 */
void ast_rtp_instance_early_bridge_make_compatible(struct ast_channel *c0, struct ast_channel *c1);

/*!
 * \brief Early bridge two channels that use RTP instances
 *
 * \param c0 First channel part of the bridge
 * \param c1 Second channel part of the bridge
 *
 * \retval 0 success
 * \retval -1 failure
 *
 * \note This should only be used by channel drivers in their technology declaration.
 *
 * \since 1.8
 */
int ast_rtp_instance_early_bridge(struct ast_channel *c0, struct ast_channel *c1);

/*!
 * \brief Initialize RED support on an RTP instance
 *
 * \param instance The instance to initialize RED support on
 * \param buffer_time How long to buffer before sending
 * \param payloads Payload values
 * \param generations Number of generations
 *
 * \retval 0 success
 * \retval -1 failure
 *
 * \since 1.8
 */
int ast_rtp_red_init(struct ast_rtp_instance *instance, int buffer_time, int *payloads, int generations);

/*!
 * \brief Buffer a frame in an RTP instance for RED
 *
 * \param instance The instance to buffer the frame on
 * \param frame Frame that we want to buffer
 *
 * \retval 0 success
 * \retval -1 failure
 *
 * \since 1.8
 */
int ast_rtp_red_buffer(struct ast_rtp_instance *instance, struct ast_frame *frame);

/*!
 * \brief Retrieve statistics about an RTP instance
 *
 * \param instance Instance to get statistics on
 * \param stats Structure to put results into
 * \param stat What statistic(s) to retrieve
 *
 * \retval 0 success
 * \retval -1 failure
 *
 * Example usage:
 *
 * \code
 * struct ast_rtp_instance_stats stats;
 * ast_rtp_instance_get_stats(instance, &stats, AST_RTP_INSTANCE_STAT_ALL);
 * \endcode
 *
 * This retrieves all statistics the underlying RTP engine supports and puts the values into the
 * stats structure.
 *
 * \since 1.8
 */
int ast_rtp_instance_get_stats(struct ast_rtp_instance *instance, struct ast_rtp_instance_stats *stats, enum ast_rtp_instance_stat stat);

/*!
 * \brief Set standard statistics from an RTP instance on a channel
 *
 * \param chan Channel to set the statistics on
 * \param instance The RTP instance that statistics will be retrieved from
 *
 * Example usage:
 *
 * \code
 * ast_rtp_instance_set_stats_vars(chan, rtp);
 * \endcode
 *
 * This retrieves standard statistics from the RTP instance rtp and sets it on the channel pointed to
 * by chan.
 *
 * \since 1.8
 */
void ast_rtp_instance_set_stats_vars(struct ast_channel *chan, struct ast_rtp_instance *instance);

/*!
 * \brief Retrieve quality statistics about an RTP instance
 *
 * \param instance Instance to get statistics on
 * \param field What quality statistic to retrieve
 * \param buf What buffer to put the result into
 * \param size Size of the above buffer
 *
 * \retval non-NULL success
 * \retval NULL failure
 *
 * Example usage:
 *
 * \code
 * char quality[AST_MAX_USER_FIELD];
 * ast_rtp_instance_get_quality(instance, AST_RTP_INSTANCE_STAT_FIELD_QUALITY, &buf, sizeof(buf));
 * \endcode
 *
 * This retrieves general quality statistics and places a text representation into the buf pointed to by buf.
 *
 * \since 1.8
 */
char *ast_rtp_instance_get_quality(struct ast_rtp_instance *instance, enum ast_rtp_instance_stat_field field, char *buf, size_t size);

/*!
 * \brief Request that the underlying RTP engine provide audio frames in a specific format
 *
 * \param instance The RTP instance to change read format on
 * \param format Format that frames are wanted in
 *
 * \retval 0 success
 * \retval -1 failure
 *
 * Example usage:
 *
 * \code
 * struct ast_format tmp_fmt;
 * ast_rtp_instance_set_read_format(instance, ast_format_set(&tmp_fmt, AST_FORMAT_ULAW, 0));
 * \endcode
 *
 * This requests that the RTP engine provide audio frames in the ULAW format.
 *
 * \since 1.8
 */
int ast_rtp_instance_set_read_format(struct ast_rtp_instance *instance, struct ast_format *format);

/*!
 * \brief Tell underlying RTP engine that audio frames will be provided in a specific format
 *
 * \param instance The RTP instance to change write format on
 * \param format Format that frames will be provided in
 *
 * \retval 0 success
 * \retval -1 failure
 *
 * Example usage:
 *
 * \code
 * struct ast_format tmp_fmt;
 * ast_rtp_instance_set_write_format(instance, ast_format_set(&tmp_fmt, AST_FORMAT_ULAW, 0));
 * \endcode
 *
 * This tells the underlying RTP engine that audio frames will be provided to it in ULAW format.
 *
 * \since 1.8
 */
int ast_rtp_instance_set_write_format(struct ast_rtp_instance *instance, struct ast_format *format);

/*!
 * \brief Request that the underlying RTP engine make two RTP instances compatible with eachother
 *
 * \param chan Our own Asterisk channel
 * \param instance The first RTP instance
 * \param peer The peer Asterisk channel
 *
 * \retval 0 success
 * \retval -1 failure
 *
 * Example usage:
 *
 * \code
 * ast_rtp_instance_make_compatible(instance, peer);
 * \endcode
 *
 * This makes the RTP instance for 'peer' compatible with 'instance' and vice versa.
 *
 * \since 1.8
 */
int ast_rtp_instance_make_compatible(struct ast_channel *chan, struct ast_rtp_instance *instance, struct ast_channel *peer);

/*! \brief Request the formats that can be transcoded
 *
 * \param instance The RTP instance
 * \param to_endpoint Formats being sent/received towards the endpoint
 * \param to_asterisk Formats being sent/received towards Asterisk
 * \param result capabilities structure to store and return supported formats in.
 *
 * Example usage:
 *
 * \code
 * ast_rtp_instance_available_formats(instance, to_capabilities, from_capabilities, result_capabilities);
 * \endcode
 *
 * This sees if it is possible to have ulaw communicated to the endpoint but signed linear received into Asterisk.
 *
 * \since 1.8
 */
void ast_rtp_instance_available_formats(struct ast_rtp_instance *instance, struct ast_format_cap *to_endpoint, struct ast_format_cap *to_asterisk, struct ast_format_cap *result);

/*!
 * \brief Indicate to the RTP engine that packets are now expected to be sent/received on the RTP instance
 *
 * \param instance The RTP instance
 *
 * \retval 0 success
 * \retval -1 failure
 *
 * Example usage:
 *
 * \code
 * ast_rtp_instance_activate(instance);
 * \endcode
 *
 * This tells the underlying RTP engine of instance that packets will now flow.
 *
 * \since 1.8
 */
int ast_rtp_instance_activate(struct ast_rtp_instance *instance);

/*!
 * \brief Request that the underlying RTP engine send a STUN BIND request
 *
 * \param instance The RTP instance
 * \param suggestion The suggested destination
 * \param username Optionally a username for the request
 *
 * Example usage:
 *
 * \code
 * ast_rtp_instance_stun_request(instance, NULL, NULL);
 * \endcode
 *
 * This requests that the RTP engine send a STUN BIND request on the session pointed to by
 * 'instance'.
 *
 * \since 1.8
 */
void ast_rtp_instance_stun_request(struct ast_rtp_instance *instance, struct ast_sockaddr *suggestion, const char *username);

/*!
 * \brief Set the RTP timeout value
 *
 * \param instance The RTP instance
 * \param timeout Value to set the timeout to
 *
 * Example usage:
 *
 * \code
 * ast_rtp_instance_set_timeout(instance, 5000);
 * \endcode
 *
 * This sets the RTP timeout value on 'instance' to be 5000.
 *
 * \since 1.8
 */
void ast_rtp_instance_set_timeout(struct ast_rtp_instance *instance, int timeout);

/*!
 * \brief Set the RTP timeout value for when the instance is on hold
 *
 * \param instance The RTP instance
 * \param timeout Value to set the timeout to
 *
 * Example usage:
 *
 * \code
 * ast_rtp_instance_set_hold_timeout(instance, 5000);
 * \endcode
 *
 * This sets the RTP hold timeout value on 'instance' to be 5000.
 *
 * \since 1.8
 */
void ast_rtp_instance_set_hold_timeout(struct ast_rtp_instance *instance, int timeout);

/*!
 * \brief Set the RTP keepalive interval
 *
 * \param instance The RTP instance
 * \param period Value to set the keepalive interval to
 *
 * Example usage:
 *
 * \code
 * ast_rtp_instance_set_keepalive(instance, 5000);
 * \endcode
 *
 * This sets the RTP keepalive interval on 'instance' to be 5000.
 *
 * \since 1.8
 */
void ast_rtp_instance_set_keepalive(struct ast_rtp_instance *instance, int timeout);

/*!
 * \brief Get the RTP timeout value
 *
 * \param instance The RTP instance
 *
 * \retval timeout value
 *
 * Example usage:
 *
 * \code
 * int timeout = ast_rtp_instance_get_timeout(instance);
 * \endcode
 *
 * This gets the RTP timeout value for the RTP instance pointed to by 'instance'.
 *
 * \since 1.8
 */
int ast_rtp_instance_get_timeout(struct ast_rtp_instance *instance);

/*!
 * \brief Get the RTP timeout value for when an RTP instance is on hold
 *
 * \param instance The RTP instance
 *
 * \retval timeout value
 *
 * Example usage:
 *
 * \code
 * int timeout = ast_rtp_instance_get_hold_timeout(instance);
 * \endcode
 *
 * This gets the RTP hold timeout value for the RTP instance pointed to by 'instance'.
 *
 * \since 1.8
 */
int ast_rtp_instance_get_hold_timeout(struct ast_rtp_instance *instance);

/*!
 * \brief Get the RTP keepalive interval
 *
 * \param instance The RTP instance
 *
 * \retval period Keepalive interval value
 *
 * Example usage:
 *
 * \code
 * int interval = ast_rtp_instance_get_keepalive(instance);
 * \endcode
 *
 * This gets the RTP keepalive interval value for the RTP instance pointed to by 'instance'.
 *
 * \since 1.8
 */
int ast_rtp_instance_get_keepalive(struct ast_rtp_instance *instance);

/*!
 * \brief Get the RTP engine in use on an RTP instance
 *
 * \param instance The RTP instance
 *
 * \retval pointer to the engine
 *
 * Example usage:
 *
 * \code
 * struct ast_rtp_engine *engine = ast_rtp_instance_get_engine(instance);
 * \endcode
 *
 * This gets the RTP engine currently in use on the RTP instance pointed to by 'instance'.
 *
 * \since 1.8
 */
struct ast_rtp_engine *ast_rtp_instance_get_engine(struct ast_rtp_instance *instance);

/*!
 * \brief Get the RTP glue in use on an RTP instance
 *
 * \param instance The RTP instance
 *
 * \retval pointer to the glue
 *
 * Example:
 *
 * \code
 * struct ast_rtp_glue *glue = ast_rtp_instance_get_active_glue(instance);
 * \endcode
 *
 * This gets the RTP glue currently in use on the RTP instance pointed to by 'instance'.
 *
 * \since 1.8
 */
struct ast_rtp_glue *ast_rtp_instance_get_active_glue(struct ast_rtp_instance *instance);

/*!
 * \brief Get the channel that is associated with an RTP instance while in a bridge
 *
 * \param instance The RTP instance
 *
 * \retval pointer to the channel
 *
 * Example:
 *
 * \code
 * struct ast_channel *chan = ast_rtp_instance_get_chan(instance);
 * \endcode
 *
 * This gets the channel associated with the RTP instance pointed to by 'instance'.
 *
 * \note This will only return a channel while in a local or remote bridge.
 *
 * \since 1.8
 */
struct ast_channel *ast_rtp_instance_get_chan(struct ast_rtp_instance *instance);

/*!
 * \brief Send a comfort noise packet to the RTP instance
 *
 * \param instance The RTP instance
 * \param level Magnitude of the noise level
 *
 * \retval 0 Success
 * \retval non-zero Failure
 */
int ast_rtp_instance_sendcng(struct ast_rtp_instance *instance, int level);

int ast_rtp_instance_add_srtp_policy(struct ast_rtp_instance *instance, struct ast_srtp_policy *policy);
struct ast_srtp *ast_rtp_instance_get_srtp(struct ast_rtp_instance *instance);

/*! \brief Custom formats declared in codecs.conf at startup must be communicated to the rtp_engine
 * so their mime type can payload number can be initialized. */
int ast_rtp_engine_load_format(const struct ast_format *format);

/*! \brief Formats requiring the use of a format attribute interface must have that
 * interface registered in order for the rtp engine to handle it correctly.  If an
 * attribute interface is unloaded, this function must be called to notify the rtp_engine. */
int ast_rtp_engine_unload_format(const struct ast_format *format);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_RTP_ENGINE_H */
