/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2026, Sangoma Technologies Corporation
 *
 * George Joseph <gjoseph@sangoma.com>
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
 * \author George Joseph <gjoseph@sangoma.com>
 *
 * \brief Protected header for the CDR and CEL Custom Backends
 *
 * \warning This file should be included only by CDR and CEL backends.
 *
 */

#ifndef _RES_CDREL_CUSTOM_H
#define _RES_CDREL_CUSTOM_H

/*! \enum Backend Types */
enum cdrel_backend_type {
	cdrel_backend_text = 0, /*!< Text file: DSV or JSON */
	cdrel_backend_db,       /*!< Database (currently only sqlite3) */
	cdrel_backend_type_end, /*!< Sentinel */
};

/*! \enum Record Types */
enum cdrel_record_type {
	cdrel_record_cdr = 0,   /*!< Call Detail Records */
	cdrel_record_cel,       /*!< Channel Event Log records */
	cdrel_record_type_end,  /*!< Sentinel */
};

/*! \struct Forward declaration of a configuration */
struct cdrel_config;

/*! \struct Vector to hold all configurations in a config file */
AST_VECTOR(cdrel_configs, struct cdrel_config *);

/*!
 * \brief Perform initial module load.
 *
 * Needs to be called by each "custom" module
 *
 * \param backend_type One of \ref cdrel_backend_type.
 * \param record_type One of \ref cdrel_record_type.
 * \param config_filename The config file name.
 * \param backend_name The name to register the backend as.
 * \param logging_cb The logging callback to register with CDR or CEL.
 * \returns A pointer to a VECTOR or config objects read from the config file.
 */
struct cdrel_configs *cdrel_load_module(enum cdrel_backend_type backend_type,
	enum cdrel_record_type record_type, const char *config_filename,
	const char *backend_name, void *logging_cb);

/*!
 * \brief Perform module reload.
 *
 * Needs to be called by each "custom" module
 *
 * \warning This function MUST be called with the module's config_lock held
 * for writing to prevent reloads from happening while we're logging.
 *
 * \param backend_type One of \ref cdrel_backend_type.
 * \param record_type One of \ref cdrel_record_type.
 * \param configs A pointer to the VECTOR of config objects returned by \ref cdrel_load_module.
 * \param config_filename The config file name.
 * \retval AST_MODULE_LOAD_SUCCESS
 * \retval AST_MODULE_LOAD_DECLINE
 */
int cdrel_reload_module(enum cdrel_backend_type backend_type, enum cdrel_record_type record_type,
	struct cdrel_configs **configs, const char *config_filename);

/*!
 * \brief Perform module unload.
 *
 * Needs to be called by each "custom" module
 *
 * \warning This function MUST be called with the module's config_lock held
 * for writing to prevent the module from being unloaded while we're logging.
 *
 * \param backend_type One of \ref cdrel_backend_type.
 * \param record_type One of \ref cdrel_record_type.
 * \param configs A pointer to the VECTOR of config objects returned by \ref cdrel_load_module.
 * \param backend_name The backend name to unregister.
 * \retval 0 Success.
 * \retval -1 Failure.
 */
int cdrel_unload_module(enum cdrel_backend_type backend_type, enum cdrel_record_type record_type,
	struct cdrel_configs *configs, const char *backend_name);

/*!
 * \brief Log a record. The module's \ref logging_cb must call this.
 *
 * \warning This function MUST be called with the module's config_lock held
 * for reading to prevent reloads from happening while we're logging.
 *
 * \param configs A pointer to the VECTOR of config objects returned by \ref cdrel_load_module.
 * \param data A pointer to an ast_cdr or ast_event object to log.
 * \retval 0 Success.
 * \retval -1 Failure.
 */
int cdrel_logger(struct cdrel_configs *configs, void *data);

#endif /* _RES_CDREL_CUSTOM_H */
