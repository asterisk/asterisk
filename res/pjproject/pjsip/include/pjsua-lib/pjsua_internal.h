/* $Id$ */
/* 
 * Copyright (C) 2008-2011 Teluu Inc. (http://www.teluu.com)
 * Copyright (C) 2003-2008 Benny Prijono <benny@prijono.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
 */
#ifndef __PJSUA_INTERNAL_H__
#define __PJSUA_INTERNAL_H__

/** 
 * This is the private header used by pjsua library implementation. 
 * Applications should not include this file.
 */

PJ_BEGIN_DECL

/** Forward decl of pjsua call */
typedef struct pjsua_call pjsua_call;

/** Forward decl of pjsua call media */
typedef struct pjsua_call_media pjsua_call_media;


/**
 * Call's media stream.
 */
struct pjsua_call_media
{
    pjsua_call		*call;	    /**< Parent call.			    */
    pjmedia_type	 type;	    /**< Media type.			    */
    unsigned		 idx;       /**< This media index in parent call.   */
    pjsua_call_media_status state;  /**< Media state.			    */
    pjsua_call_media_status prev_state;/**< Previous media state.           */
    pjmedia_dir		 dir;       /**< Media direction.		    */

    /** The stream */
    struct {
	/** Audio stream */
	struct {
	    pjmedia_stream *stream;    /**< The audio stream.		    */
	    int		    conf_slot; /**< Slot # in conference bridge.    */
	} a;

	/** Video stream */
	struct {
	    pjmedia_vid_stream  *stream;    /**< The video stream.	    */
	    pjsua_vid_win_id	 cap_win_id;/**< The video capture window   */
	    pjsua_vid_win_id	 rdr_win_id;/**< The video render window    */
	    pjmedia_vid_dev_index cap_dev;  /**< The video capture device   */
	    pjmedia_vid_dev_index rdr_dev;  /**< The video-in render device */
	} v;

    } strm;

    pj_uint32_t		 ssrc;	    /**< RTP SSRC			    */
    pj_uint32_t		 rtp_tx_ts; /**< Initial RTP timestamp for sender.  */
    pj_uint16_t		 rtp_tx_seq;/**< Initial RTP sequence for sender.   */
    pj_uint8_t		 rtp_tx_seq_ts_set;
				    /**< Bitmask flags if initial RTP sequence
				         and/or timestamp for sender are set.
					 bit 0/LSB : sequence flag
					 bit 1     : timestamp flag 	    */

    pjmedia_transport	*tp;        /**< Current media transport (can be 0) */
    pj_status_t		 tp_ready;  /**< Media transport status.	    */
    pj_status_t		 tp_result; /**< Media transport creation result.   */
    pjmedia_transport	*tp_orig;   /**< Original media transport	    */
    pj_bool_t		 tp_auto_del; /**< May delete media transport       */
    pjsua_med_tp_st	 tp_st;     /**< Media transport state		    */
    pj_bool_t            use_custom_med_tp;/**< Use custom media transport? */
    pj_sockaddr		 rtp_addr;  /**< Current RTP source address
					    (used to update ICE default
					    address)			    */
    pjmedia_srtp_use	 rem_srtp_use; /**< Remote's SRTP usage policy.	    */
    pj_timestamp	 last_req_keyframe;/**< Last TX keyframe request.   */

    pjsua_med_tp_state_cb      med_init_cb;/**< Media transport
                                                initialization callback.    */

    /** Media transport creation callback. */
    pj_status_t (*med_create_cb)(pjsua_call_media *call_med,
                                 pj_status_t status, int security_level,
                                 int *sip_err_code);
};

/**
 * Maximum number of SDP "m=" lines to be supported.
 */
#define PJSUA_MAX_CALL_MEDIA		PJMEDIA_MAX_SDP_MEDIA

/* Call answer's list. */
typedef struct call_answer
{
    PJ_DECL_LIST_MEMBER(struct call_answer);
    pjsua_msg_data  *msg_data;      /**< Answer's headers list.       */
    pj_str_t        *reason;        /**< Answer's reason phrase.      */
    unsigned         code;          /**< Answer's status code.        */
} call_answer;


/** 
 * Structure to be attached to invite dialog. 
 * Given a dialog "dlg", application can retrieve this structure
 * by accessing dlg->mod_data[pjsua.mod.id].
 */
struct pjsua_call
{
    unsigned		 index;	    /**< Index in pjsua array.		    */
    pjsua_call_setting	 opt;	    /**< Call setting.			    */
    pjsip_inv_session	*inv;	    /**< The invite session.		    */
    void		*user_data; /**< User/application data.		    */
    pjsip_status_code	 last_code; /**< Last status code seen.		    */
    pj_str_t		 last_text; /**< Last status text seen.		    */
    pj_time_val		 start_time;/**< First INVITE sent/received.	    */
    pj_time_val		 res_time;  /**< First response sent/received.	    */
    pj_time_val		 conn_time; /**< Connected/confirmed time.	    */
    pj_time_val		 dis_time;  /**< Disconnect time.		    */
    pjsua_acc_id	 acc_id;    /**< Account index being used.	    */
    int			 secure_level;/**< Signaling security level.	    */
    pjsua_call_hold_type call_hold_type; /**< How to do call hold.	    */
    pj_bool_t		 local_hold;/**< Flag for call-hold by local.	    */
    void		*hold_msg;  /**< Outgoing hold tx_data.		    */

    unsigned		 med_cnt;   /**< Number of media in SDP.	    */
    pjsua_call_media     media[PJSUA_MAX_CALL_MEDIA]; /**< Array of media   */
    unsigned		 med_prov_cnt;/**< Number of provisional media.	    */
    pjsua_call_media	 media_prov[PJSUA_MAX_CALL_MEDIA];
				    /**< Array of provisional media.	    */

    int			 audio_idx; /**< First active audio media.	    */
    pj_mutex_t          *med_ch_mutex;/**< Media channel callback's mutex.  */
    pjsua_med_tp_state_cb   med_ch_cb;/**< Media channel callback.	    */
    pjsua_med_tp_state_info med_ch_info;/**< Media channel info.            */

    pjsip_evsub		*xfer_sub;  /**< Xfer server subscription, if this
					 call was triggered by xfer.	    */
    pj_stun_nat_type	 rem_nat_type; /**< NAT type of remote endpoint.    */

    char    last_text_buf_[128];    /**< Buffer for last_text.		    */

    struct {
	pj_timer_entry	 reinv_timer;/**< Reinvite retry timer.		    */
	pj_uint32_t	 sdp_ver;    /**< SDP version of the bad answer     */
	int		 retry_cnt;  /**< Retry count.			    */
        pj_bool_t        pending;    /**< Pending until CONFIRMED state     */
    } lock_codec;		     /**< Data for codec locking when answer
					  contains multiple codecs.	    */

    struct {
        pjsip_dialog        *dlg;    /**< Call dialog.                      */
        pjmedia_sdp_session *rem_sdp;/**< Remote SDP.                       */
        pj_pool_t           *pool_prov;/**< Provisional pool.               */
        pj_bool_t            med_ch_deinit;/**< Media channel de-init-ed?   */
        union {
            struct {
                pjsua_msg_data  *msg_data;/**< Headers for outgoing INVITE. */
            } out_call;
            struct {
                call_answer      answers;/**< A list of call answers.       */
            } inc_call;
        } call_var;
    } async_call;                      /**< Temporary storage for async
                                            outgoing/incoming call.         */

    pj_bool_t		 rem_offerer;  /**< Was remote SDP offerer?	    */
    unsigned		 rem_aud_cnt;  /**< No of active audio in last remote
					    offer.			    */
    unsigned		 rem_vid_cnt;  /**< No of active video in last remote
					    offer.			    */
};


/**
 * Server presence subscription list head.
 */
struct pjsua_srv_pres
{
    PJ_DECL_LIST_MEMBER(struct pjsua_srv_pres);
    pjsip_evsub	    *sub;	    /**< The evsub.			    */
    char	    *remote;	    /**< Remote URI.			    */
    int		     acc_id;	    /**< Account ID.			    */
    pjsip_dialog    *dlg;	    /**< Dialog.			    */
    int		     expires;	    /**< "expires" value in the request.    */
};

/**
 * Account
 */
typedef struct pjsua_acc
{
    pj_pool_t	    *pool;	    /**< Pool for this account.		*/
    pjsua_acc_config cfg;	    /**< Account configuration.		*/
    pj_bool_t	     valid;	    /**< Is this account valid?		*/

    int		     index;	    /**< Index in accounts array.	*/
    pj_str_t	     display;	    /**< Display name, if any.		*/
    pj_str_t	     user_part;	    /**< User part of local URI.	*/
    pj_str_t	     contact;	    /**< Our Contact header.		*/
    pj_str_t         reg_contact;   /**< Contact header for REGISTER.
				         It may be different than acc
				         contact if outbound is used    */

    pj_str_t	     srv_domain;    /**< Host part of reg server.	*/
    int		     srv_port;	    /**< Port number of reg server.	*/

    pjsip_regc	    *regc;	    /**< Client registration session.   */
    pj_status_t	     reg_last_err;  /**< Last registration error.	*/
    int		     reg_last_code; /**< Last status last register.	*/

    struct {
	pj_bool_t	 active;    /**< Flag of reregister status.	*/
	pj_timer_entry   timer;	    /**< Timer for reregistration.	*/
	void		*reg_tp;    /**< Transport for registration.	*/
	unsigned	 attempt_cnt; /**< Attempt counter.		*/
    } auto_rereg;		    /**< Reregister/reconnect data.	*/

    pj_timer_entry   ka_timer;	    /**< Keep-alive timer for UDP.	*/
    pjsip_transport *ka_transport;  /**< Transport for keep-alive.	*/
    pj_sockaddr	     ka_target;	    /**< Destination address for K-A	*/
    unsigned	     ka_target_len; /**< Length of ka_target.		*/

    pjsip_route_hdr  route_set;	    /**< Complete route set inc. outbnd.*/
    pj_uint32_t	     global_route_crc; /** CRC of global route setting. */
    pj_uint32_t	     local_route_crc;  /** CRC of account route setting.*/

    unsigned         rfc5626_status;/**< SIP outbound status:
                                           0: not used
                                           1: requested
                                           2: acknowledged by servers   */
    pj_str_t	     rfc5626_instprm;/**< SIP outbound instance param.  */
    pj_str_t         rfc5626_regprm;/**< SIP outbound reg param.        */

    unsigned	     cred_cnt;	    /**< Number of credentials.		*/
    pjsip_cred_info  cred[PJSUA_ACC_MAX_PROXIES]; /**< Complete creds.	*/

    pj_bool_t	     online_status; /**< Our online status.		*/
    pjrpid_element   rpid;	    /**< RPID element information.	*/
    pjsua_srv_pres   pres_srv_list; /**< Server subscription list.	*/
    pjsip_publishc  *publish_sess;  /**< Client publication session.	*/
    pj_bool_t	     publish_state; /**< Last published online status	*/

    pjsip_evsub	    *mwi_sub;	    /**< MWI client subscription	*/
    pjsip_dialog    *mwi_dlg;	    /**< Dialog for MWI sub.		*/
} pjsua_acc;


/**
 *Transport.
 */
typedef struct pjsua_transport_data
{
    int			     index;
    pjsip_transport_type_e   type;
    pjsip_host_port	     local_name;

    union {
	pjsip_transport	    *tp;
	pjsip_tpfactory	    *factory;
	void		    *ptr;
    } data;

} pjsua_transport_data;


/** Maximum length of subscription termination reason. */
#define PJSUA_BUDDY_SUB_TERM_REASON_LEN	    32

/**
 * Buddy data.
 */
typedef struct pjsua_buddy
{
    pj_pool_t		*pool;	    /**< Pool for this buddy.		*/
    unsigned		 index;	    /**< Buddy index.			*/
    void		*user_data; /**< Application data.		*/
    pj_str_t		 uri;	    /**< Buddy URI.			*/
    pj_str_t		 contact;   /**< Contact learned from subscrp.	*/
    pj_str_t		 name;	    /**< Buddy name.			*/
    pj_str_t		 display;   /**< Buddy display name.		*/
    pj_str_t		 host;	    /**< Buddy host.			*/
    unsigned		 port;	    /**< Buddy port.			*/
    pj_bool_t		 monitor;   /**< Should we monitor?		*/
    pjsip_dialog	*dlg;	    /**< The underlying dialog.		*/
    pjsip_evsub		*sub;	    /**< Buddy presence subscription	*/
    unsigned		 term_code; /**< Subscription termination code	*/
    pj_str_t		 term_reason;/**< Subscription termination reason */
    pjsip_pres_status	 status;    /**< Buddy presence status.		*/
    pj_timer_entry	 timer;	    /**< Resubscription timer		*/
} pjsua_buddy;


/**
 * File player/recorder data.
 */
typedef struct pjsua_file_data
{
    pj_bool_t	     type;  /* 0=player, 1=playlist */
    pjmedia_port    *port;
    pj_pool_t	    *pool;
    unsigned	     slot;
} pjsua_file_data;


/**
 * Additional parameters for conference bridge.
 */
typedef struct pjsua_conf_setting
{
    unsigned	channel_count;
    unsigned	samples_per_frame;
    unsigned	bits_per_sample;
} pjsua_conf_setting;

typedef struct pjsua_stun_resolve
{
    PJ_DECL_LIST_MEMBER(struct pjsua_stun_resolve);

    pj_pool_t		*pool;	    /**< Pool		    */
    unsigned		 count;	    /**< # of entries	    */
    pj_str_t		*srv;	    /**< Array of entries   */
    unsigned		 idx;	    /**< Current index	    */
    void		*token;	    /**< App token	    */
    pj_stun_resolve_cb	 cb;	    /**< App callback	    */
    pj_bool_t		 blocking;  /**< Blocking?	    */
    pj_status_t		 status;    /**< Session status	    */
    pj_sockaddr		 addr;	    /**< Result		    */
    pj_stun_sock	*stun_sock; /**< Testing STUN sock  */
} pjsua_stun_resolve;

/* See also pjsua_vid_win_type_name() */
typedef enum pjsua_vid_win_type
{
    PJSUA_WND_TYPE_NONE,
    PJSUA_WND_TYPE_PREVIEW,
    PJSUA_WND_TYPE_STREAM
} pjsua_vid_win_type;

typedef struct pjsua_vid_win
{
    pjsua_vid_win_type		 type;		/**< Type.		*/
    pj_pool_t			*pool;		/**< Own pool.		*/
    unsigned	 		 ref_cnt;	/**< Reference counter.	*/
    pjmedia_vid_port		*vp_cap;	/**< Capture vidport.	*/
    pjmedia_vid_port		*vp_rend;	/**< Renderer vidport	*/
    pjmedia_port		*tee;		/**< Video tee		*/
    pjmedia_vid_dev_index	 preview_cap_id;/**< Capture dev id	*/
    pj_bool_t			 preview_running;/**< Preview is started*/
    pj_bool_t			 is_native; 	/**< Preview is by dev  */
} pjsua_vid_win;


typedef struct pjsua_timer_list
{
    PJ_DECL_LIST_MEMBER(struct pjsua_timer_list);
    pj_timer_entry         entry;
    void                  (*cb)(void *user_data);
    void                   *user_data;
} pjsua_timer_list;


/**
 * Global pjsua application data.
 */
struct pjsua_data
{

    /* Control: */
    pj_caching_pool	 cp;	    /**< Global pool factory.		*/
    pj_pool_t		*pool;	    /**< pjsua's private pool.		*/
    pj_mutex_t		*mutex;	    /**< Mutex protection for this data	*/
    unsigned		 mutex_nesting_level; /**< Mutex nesting level.	*/
    pj_thread_t		*mutex_owner; /**< Mutex owner.			*/
    pjsua_state		 state;	    /**< Library state.			*/

    /* Logging: */
    pjsua_logging_config log_cfg;   /**< Current logging config.	*/
    pj_oshandle_t	 log_file;  /**<Output log file handle		*/

    /* SIP: */
    pjsip_endpoint	*endpt;	    /**< Global endpoint.		*/
    pjsip_module	 mod;	    /**< pjsua's PJSIP module.		*/
    pjsua_transport_data tpdata[8]; /**< Array of transports.		*/
    pjsip_tp_state_callback old_tp_cb; /**< Old transport callback.	*/

    /* Threading: */
    pj_bool_t		 thread_quit_flag;  /**< Thread quit flag.	*/
    pj_thread_t		*thread[4];	    /**< Array of threads.	*/

    /* STUN and resolver */
    pj_stun_config	 stun_cfg;  /**< Global STUN settings.		*/
    pj_sockaddr		 stun_srv;  /**< Resolved STUN server address	*/
    pj_status_t		 stun_status; /**< STUN server status.		*/
    pjsua_stun_resolve	 stun_res;  /**< List of pending STUN resolution*/
    pj_dns_resolver	*resolver;  /**< DNS resolver.			*/

    /* Detected NAT type */
    pj_stun_nat_type	 nat_type;	/**< NAT type.			*/
    pj_status_t		 nat_status;	/**< Detection status.		*/
    pj_bool_t		 nat_in_progress; /**< Detection in progress	*/

    /* List of outbound proxies: */
    pjsip_route_hdr	 outbound_proxy;

    /* Account: */
    unsigned		 acc_cnt;	     /**< Number of accounts.	*/
    pjsua_acc_id	 default_acc;	     /**< Default account ID	*/
    pjsua_acc		 acc[PJSUA_MAX_ACC]; /**< Account array.	*/
    pjsua_acc_id	 acc_ids[PJSUA_MAX_ACC]; /**< Acc sorted by prio*/

    /* Calls: */
    pjsua_config	 ua_cfg;		/**< UA config.		*/
    unsigned		 call_cnt;		/**< Call counter.	*/
    pjsua_call		 calls[PJSUA_MAX_CALLS];/**< Calls array.	*/
    pjsua_call_id	 next_call_id;		/**< Next call id to use*/

    /* Buddy; */
    unsigned		 buddy_cnt;		    /**< Buddy count.	*/
    pjsua_buddy		 buddy[PJSUA_MAX_BUDDIES];  /**< Buddy array.	*/

    /* Presence: */
    pj_timer_entry	 pres_timer;/**< Presence refresh timer.	*/

    /* Media: */
    pjsua_media_config   media_cfg; /**< Media config.			*/
    pjmedia_endpt	*med_endpt; /**< Media endpoint.		*/
    pjsua_conf_setting	 mconf_cfg; /**< Additionan conf. bridge. param */
    pjmedia_conf	*mconf;	    /**< Conference bridge.		*/
    pj_bool_t		 is_mswitch;/**< Are we using audio switchboard
				         (a.k.a APS-Direct)		*/

    /* Sound device */
    pjmedia_aud_dev_index cap_dev;  /**< Capture device ID.		*/
    pjmedia_aud_dev_index play_dev; /**< Playback device ID.		*/
    pj_uint32_t		 aud_svmask;/**< Which settings to save		*/
    pjmedia_aud_param	 aud_param; /**< User settings to sound dev	*/
    pj_bool_t		 aud_open_cnt;/**< How many # device is opened	*/
    pj_bool_t		 no_snd;    /**< No sound (app will manage it)	*/
    pj_pool_t		*snd_pool;  /**< Sound's private pool.		*/
    pjmedia_snd_port	*snd_port;  /**< Sound port.			*/
    pj_timer_entry	 snd_idle_timer;/**< Sound device idle timer.	*/
    pjmedia_master_port	*null_snd;  /**< Master port for null sound.	*/
    pjmedia_port	*null_port; /**< Null port.			*/
    pj_bool_t		 snd_is_on; /**< Media flow is currently active */

    /* Video device */
    pjmedia_vid_dev_index vcap_dev;  /**< Capture device ID.		*/
    pjmedia_vid_dev_index vrdr_dev;  /**< Playback device ID.		*/

    /* File players: */
    unsigned		 player_cnt;/**< Number of file players.	*/
    pjsua_file_data	 player[PJSUA_MAX_PLAYERS];/**< Array of players.*/

    /* File recorders: */
    unsigned		 rec_cnt;   /**< Number of file recorders.	*/
    pjsua_file_data	 recorder[PJSUA_MAX_RECORDERS];/**< Array of recs.*/

    /* Video windows */
#if PJSUA_HAS_VIDEO
    pjsua_vid_win	 win[PJSUA_MAX_VID_WINS]; /**< Array of windows	*/
#endif

    /* Timer entry list */
    pjsua_timer_list	 timer_list;
    pj_mutex_t          *timer_mutex;
};


extern struct pjsua_data pjsua_var;

/**
 * Get the instance of pjsua
 */
PJ_DECL(struct pjsua_data*) pjsua_get_var(void);



/**
 * IM callback data.
 */
typedef struct pjsua_im_data
{
    pjsua_acc_id     acc_id;
    pjsua_call_id    call_id;
    pj_str_t	     to;
    pj_str_t	     body;
    void	    *user_data;
} pjsua_im_data;

pj_status_t pjsua_media_apply_xml_control(pjsua_call_id call_id,
					  const pj_str_t *xml_st);


/**
 * Duplicate IM data.
 */
PJ_INLINE(pjsua_im_data*) pjsua_im_data_dup(pj_pool_t *pool, 
					    const pjsua_im_data *src)
{
    pjsua_im_data *dst;

    dst = (pjsua_im_data*) pj_pool_alloc(pool, sizeof(*dst));
    dst->acc_id = src->acc_id;
    dst->call_id = src->call_id;
    pj_strdup_with_null(pool, &dst->to, &src->to);
    dst->user_data = src->user_data;
    pj_strdup_with_null(pool, &dst->body, &src->body);

    return dst;
}


#if 1

PJ_INLINE(void) PJSUA_LOCK()
{
    pj_mutex_lock(pjsua_var.mutex);
    pjsua_var.mutex_owner = pj_thread_this();
    ++pjsua_var.mutex_nesting_level;
}

PJ_INLINE(void) PJSUA_UNLOCK()
{
    if (--pjsua_var.mutex_nesting_level == 0)
	pjsua_var.mutex_owner = NULL;
    pj_mutex_unlock(pjsua_var.mutex);
}

PJ_INLINE(pj_status_t) PJSUA_TRY_LOCK()
{
    pj_status_t status;
    status = pj_mutex_trylock(pjsua_var.mutex);
    if (status == PJ_SUCCESS) {
	pjsua_var.mutex_owner = pj_thread_this();
	++pjsua_var.mutex_nesting_level;
    }
    return status;
}

PJ_INLINE(pj_bool_t) PJSUA_LOCK_IS_LOCKED()
{
    return pjsua_var.mutex_owner == pj_thread_this();
}

#else
#define PJSUA_LOCK()
#define PJSUA_TRY_LOCK()	PJ_SUCCESS
#define PJSUA_UNLOCK()
#define PJSUA_LOCK_IS_LOCKED()	PJ_TRUE
#endif

/* Core */
void pjsua_set_state(pjsua_state new_state);

/******
 * STUN resolution
 */
/* Resolve the STUN server */
pj_status_t resolve_stun_server(pj_bool_t wait);

/** 
 * Normalize route URI (check for ";lr" and append one if it doesn't
 * exist and pjsua_config.force_lr is set.
 */
pj_status_t normalize_route_uri(pj_pool_t *pool, pj_str_t *uri);

/**
 * Handle incoming invite request.
 */
pj_bool_t pjsua_call_on_incoming(pjsip_rx_data *rdata);

/*
 * Media channel.
 */
pj_status_t pjsua_media_channel_init(pjsua_call_id call_id,
				     pjsip_role_e role,
				     int security_level,
				     pj_pool_t *tmp_pool,
				     const pjmedia_sdp_session *rem_sdp,
				     int *sip_err_code,
                                     pj_bool_t async,
                                     pjsua_med_tp_state_cb cb);
pj_status_t pjsua_media_channel_create_sdp(pjsua_call_id call_id, 
					   pj_pool_t *pool,
					   const pjmedia_sdp_session *rem_sdp,
					   pjmedia_sdp_session **p_sdp,
					   int *sip_err_code);
pj_status_t pjsua_media_channel_update(pjsua_call_id call_id,
				       const pjmedia_sdp_session *local_sdp,
				       const pjmedia_sdp_session *remote_sdp);
pj_status_t pjsua_media_channel_deinit(pjsua_call_id call_id);

pj_status_t pjsua_call_media_init(pjsua_call_media *call_med,
                                  pjmedia_type type,
				  const pjsua_transport_config *tcfg,
				  int security_level,
				  int *sip_err_code,
                                  pj_bool_t async,
                                  pjsua_med_tp_state_cb cb);
void pjsua_set_media_tp_state(pjsua_call_media *call_med, pjsua_med_tp_st tp_st);

void pjsua_media_prov_clean_up(pjsua_call_id call_id);

/* Callback to receive media events */
pj_status_t call_media_on_event(pjmedia_event *event,
                                void *user_data);

/**
 * Init presence.
 */
pj_status_t pjsua_pres_init();

/*
 * Start presence subsystem.
 */
pj_status_t pjsua_pres_start(void);

/**
 * Refresh presence subscriptions
 */
void pjsua_pres_refresh(void);

/*
 * Update server subscription (e.g. when our online status has changed)
 */
void pjsua_pres_update_acc(int acc_id, pj_bool_t force);

/*
 * Shutdown presence.
 */
void pjsua_pres_shutdown(unsigned flags);

/**
 * Init presence for aoocunt.
 */
pj_status_t pjsua_pres_init_acc(int acc_id);

/**
 * Send PUBLISH
 */
pj_status_t pjsua_pres_init_publish_acc(int acc_id);

/**
 *  Send un-PUBLISH
 */
void pjsua_pres_unpublish(pjsua_acc *acc, unsigned flags);

/**
 * Terminate server subscription for the account 
 */
void pjsua_pres_delete_acc(int acc_id, unsigned flags);

/**
 * Init IM module handler to handle incoming MESSAGE outside dialog.
 */
pj_status_t pjsua_im_init(void);

/**
 * Start MWI subscription
 */
void pjsua_start_mwi(pjsua_acc *acc);

/**
 * Init call subsystem.
 */
pj_status_t pjsua_call_subsys_init(const pjsua_config *cfg);

/**
 * Start call subsystem.
 */
pj_status_t pjsua_call_subsys_start(void);

/**
 * Init media subsystems.
 */
pj_status_t pjsua_media_subsys_init(const pjsua_media_config *cfg);

/**
 * Start pjsua media subsystem.
 */
pj_status_t pjsua_media_subsys_start(void);

/**
 * Destroy pjsua media subsystem.
 */
pj_status_t pjsua_media_subsys_destroy(unsigned flags);

/**
 * Private: check if we can accept the message.
 *	    If not, then p_accept header will be filled with a valid
 *	    Accept header.
 */
pj_bool_t pjsua_im_accept_pager(pjsip_rx_data *rdata,
				pjsip_accept_hdr **p_accept_hdr);

/**
 * Private: process pager message.
 *	    This may trigger pjsua_ui_on_pager() or pjsua_ui_on_typing().
 */
void pjsua_im_process_pager(int call_id, const pj_str_t *from,
			    const pj_str_t *to, pjsip_rx_data *rdata);


/**
 * Create Accept header for MESSAGE.
 */
pjsip_accept_hdr* pjsua_im_create_accept(pj_pool_t *pool);

/*
 * Add additional headers etc in msg_data specified by application
 * when sending requests.
 */
void pjsua_process_msg_data(pjsip_tx_data *tdata,
			    const pjsua_msg_data *msg_data);


/*
 * Add route_set to outgoing requests
 */
void pjsua_set_msg_route_set( pjsip_tx_data *tdata,
			      const pjsip_route_hdr *route_set );


/*
 * Simple version of MIME type parsing (it doesn't support parameters)
 */
void pjsua_parse_media_type( pj_pool_t *pool,
			     const pj_str_t *mime,
			     pjsip_media_type *media_type);


/*
 * Internal function to init transport selector from transport id.
 */
void pjsua_init_tpselector(pjsua_transport_id tp_id,
			   pjsip_tpselector *sel);

pjsip_dialog* on_dlg_forked(pjsip_dialog *first_set, pjsip_rx_data *res);
pj_status_t acquire_call(const char *title,
                         pjsua_call_id call_id,
                         pjsua_call **p_call,
                         pjsip_dialog **p_dlg);
const char *good_number(char *buf, pj_int32_t val);
void print_call(const char *title,
                int call_id,
                char *buf, pj_size_t size);

/*
 * Audio
 */
pj_status_t pjsua_aud_subsys_init(void);
pj_status_t pjsua_aud_subsys_start(void);
pj_status_t pjsua_aud_subsys_destroy(void);
void pjsua_aud_stop_stream(pjsua_call_media *call_med);
pj_status_t pjsua_aud_channel_update(pjsua_call_media *call_med,
                                     pj_pool_t *tmp_pool,
                                     pjmedia_stream_info *si,
				     const pjmedia_sdp_session *local_sdp,
				     const pjmedia_sdp_session *remote_sdp);

/*
 * Video
 */
pj_status_t pjsua_vid_subsys_init(void);
pj_status_t pjsua_vid_subsys_start(void);
pj_status_t pjsua_vid_subsys_destroy(void);
void pjsua_vid_stop_stream(pjsua_call_media *call_med);
pj_status_t pjsua_vid_channel_init(pjsua_call_media *call_med);
pj_status_t pjsua_vid_channel_update(pjsua_call_media *call_med,
				     pj_pool_t *tmp_pool,
				     pjmedia_vid_stream_info *si,
				     const pjmedia_sdp_session *local_sdp,
				     const pjmedia_sdp_session *remote_sdp);

#if PJSUA_HAS_VIDEO
PJ_DECL(void) pjsua_vid_win_reset(pjsua_vid_win_id wid);
#else
#  define pjsua_vid_win_reset(wid)
#endif


PJ_END_DECL

#endif	/* __PJSUA_INTERNAL_H__ */

