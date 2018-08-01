/*
 * Prototypes for public functions only of internal interest,
 * normally not used by modules.
 * What goes here are typically *_init() routines.
 */

/*! \file
 *
 * \brief
 * Prototypes for public functions only of internal interest,
 *
 */


#ifndef _ASTERISK__PRIVATE_H
#define _ASTERISK__PRIVATE_H

/* Load settings from asterisk.conf, provided by options.c */
void load_asterisk_conf(void);
void set_asterisk_conf_path(const char *path);
void set_socket_path(const char *path);

int load_modules(void);		/*!< Provided by loader.c */
int modules_shutdown(void);		/*!< Provided by loader.c */
int load_pbx(void);			/*!< Provided by pbx.c */
int load_pbx_builtins(void);	/*!< Provided by pbx_builtins.c */
int load_pbx_functions_cli(void);	/*!< Provided by pbx_functions.c */
int load_pbx_variables(void);	/*!< Provided by pbx_variables.c */
int load_pbx_switch(void);		/*!< Provided by pbx_switch.c */
int load_pbx_app(void);		/*!< Provided by pbx_app.c */
int load_pbx_hangup_handler(void);	/*!< Provided by pbx_hangup_handler.c */
int init_logger(void);			/*!< Provided by logger.c */
void close_logger(void);		/*!< Provided by logger.c */
void logger_queue_start(void);		/*!< Provided by logger.c */
void clean_time_zones(void);			/*!< Provided by localtime.c */
int ast_term_init(void);		/*!< Provided by term.c */
int astdb_init(void);			/*!< Provided by db.c */
int ast_channels_init(void);		/*!< Provided by channel.c */
void ast_builtins_init(void);		/*!< Provided by cli.c */
int ast_cli_perms_init(int reload);	/*!< Provided by cli.c */
void dnsmgr_start_refresh(void);	/*!< Provided by dnsmgr.c */
int ast_dns_system_resolver_init(void); /*!< Provided by dns_system_resolver.c */
void threadstorage_init(void);		/*!< Provided by threadstorage.c */
int ast_device_state_engine_init(void);	/*!< Provided by devicestate.c */
int astobj2_init(void);			/*!< Provided by astobj2.c */
int ast_named_locks_init(void);		/*!< Provided by named_locks.c */
int ast_file_init(void);		/*!< Provided by file.c */
void ast_autoservice_init(void);	/*!< Provided by autoservice.c */
int ast_tps_init(void); 		/*!< Provided by taskprocessor.c */
int ast_timing_init(void);		/*!< Provided by timing.c */
void ast_stun_init(void);               /*!< Provided by stun.c */
int ast_ssl_init(void);                 /*!< Provided by ssl.c */
int ast_pj_init(void);                 /*!< Provided by libasteriskpj.c */
int ast_test_init(void);            /*!< Provided by test.c */
int ast_msg_init(void);             /*!< Provided by message.c */
void ast_msg_shutdown(void);        /*!< Provided by message.c */
int aco_init(void);             /*!< Provided by config_options.c */
int dns_core_init(void);        /*!< Provided by dns_core.c */

/*!
 * \brief Initialize malloc debug phase 1.
 *
 * \note Must be called first thing after forking.
 *
 * \return Nothing
 */
void load_astmm_phase_1(void);

/*!
 * \brief Initialize malloc debug phase 2.
 *
 * \return Nothing
 */
void load_astmm_phase_2(void);

/*!
 * \brief Initialize the bridging system.
 * \since 12.0.0
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_bridging_init(void);

/*!
 * \brief Initialize the local proxy channel.
 * \since 12.0.0
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_local_init(void);

/*!
 * \brief Process reload requests received during startup.
 *
 * This function requests that the loader execute the pending reload requests
 * that were queued during server startup.
 *
 * \note This function will do nothing if the server has not completely started
 *       up.  Once called, the reload queue is emptied, and further invocations
 *       will have no affect.
 */
void ast_process_pending_reloads(void);

/*! \brief Load XML documentation. Provided by xmldoc.c
 *  \retval 1 on error.
 *  \retval 0 on success.
 */
int ast_xmldoc_load_documentation(void);

/*! \brief initializes the rtp engine arrays */
int ast_rtp_engine_init(void);

/*!
 * \brief initializes the rtp engine arrays
 * \since 12.0.0
 */
int ast_parking_stasis_init(void);

/*!
 * \brief Endpoint support initialization.
 * \return 0 on success.
 * \return Non-zero on error.
 */
int ast_endpoint_init(void);

#endif /* _ASTERISK__PRIVATE_H */
