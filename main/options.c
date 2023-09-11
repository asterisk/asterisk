/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2018, CFWare, LLC
 *
 * Corey Farrell <git@cfware.com>
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
 *
 * \brief Symbols related to asterisk.conf options and paths.
 *
 * \author Corey Farrell <git@cfware.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"
#include "asterisk/_private.h"
#include "asterisk/app.h"
#include "asterisk/config.h"
#include "asterisk/logger.h"
#include "asterisk/options.h"
#include "asterisk/paths.h"
#include "asterisk/pbx.h"
#include "asterisk/rtp_engine.h"
#include "asterisk/strings.h"
#include "asterisk/utils.h"

#include "../defaults.h"

#include <sys/time.h>
#include <sys/resource.h>


/*! Default minimum DTMF digit length - 80ms */
#define AST_MIN_DTMF_DURATION 80

#define DEFAULT_MONITOR_DIR DEFAULT_SPOOL_DIR "/monitor"
#define DEFAULT_RECORDING_DIR DEFAULT_SPOOL_DIR "/recording"

/*! \defgroup main_options Main Configuration Options
 * \brief Main configuration options from asterisk.conf or OS command line on starting Asterisk.
 * \arg \ref asterisk.conf "Config_ast"
 * \note Some of them can be changed in the CLI
 */
/*! @{ */

struct ast_flags ast_options = { AST_DEFAULT_OPTIONS };

/*! Maximum active system verbosity level. */
int ast_verb_sys_level;

/*! Verbosity level */
int option_verbose;
/*! Debug level */
int option_debug;
/*! Trace level */
int option_trace;
/*! Default to -1 to know if we have read the level from pjproject yet. */
int ast_pjproject_max_log_level = -1;
int ast_option_pjproject_log_level;
int ast_option_pjproject_cache_pools;
/*! Max load avg on system */
double ast_option_maxload;
/*! Max number of active calls */
int ast_option_maxcalls;
/*! Max number of open file handles (files, sockets) */
int ast_option_maxfiles;
/*! Minimum duration of DTMF. */
unsigned int option_dtmfminduration = AST_MIN_DTMF_DURATION;
#if defined(HAVE_SYSINFO)
/*! Minimum amount of free system memory - stop accepting calls if free memory falls below this watermark */
long option_minmemfree;
#endif
int ast_option_rtpusedynamic = 1;
unsigned int ast_option_rtpptdynamic = 35;

/*! @} */

struct ast_eid ast_eid_default;

/* XXX tmpdir is a subdir of the spool directory, and no way to remap it */
char record_cache_dir[AST_CACHE_DIR_LEN] = DEFAULT_TMP_DIR;

char ast_defaultlanguage[MAX_LANGUAGE] = DEFAULT_LANGUAGE;

struct _cfg_paths {
	char cache_dir[PATH_MAX];
	char config_dir[PATH_MAX];
	char module_dir[PATH_MAX];
	char spool_dir[PATH_MAX];
	char monitor_dir[PATH_MAX];
	char recording_dir[PATH_MAX];
	char var_dir[PATH_MAX];
	char data_dir[PATH_MAX];
	char log_dir[PATH_MAX];
	char agi_dir[PATH_MAX];
	char run_dir[PATH_MAX];
	char key_dir[PATH_MAX];

	char config_file[PATH_MAX];
	char db_path[PATH_MAX];
	char sbin_dir[PATH_MAX];
	char pid_path[PATH_MAX];
	char socket_path[PATH_MAX];
	char run_user[PATH_MAX];
	char run_group[PATH_MAX];
	char system_name[128];
	char ctl_perms[PATH_MAX];
	char ctl_owner[PATH_MAX];
	char ctl_group[PATH_MAX];
	char ctl_file[PATH_MAX];
};

static struct _cfg_paths cfg_paths = {
	.cache_dir = DEFAULT_CACHE_DIR,
	.config_dir = DEFAULT_CONFIG_DIR,
	.module_dir = DEFAULT_MODULE_DIR,
	.spool_dir = DEFAULT_SPOOL_DIR,
	.monitor_dir = DEFAULT_MONITOR_DIR,
	.recording_dir = DEFAULT_RECORDING_DIR,
	.var_dir = DEFAULT_VAR_DIR,
	.data_dir = DEFAULT_DATA_DIR,
	.log_dir = DEFAULT_LOG_DIR,
	.agi_dir = DEFAULT_AGI_DIR,
	.run_dir = DEFAULT_RUN_DIR,
	.key_dir = DEFAULT_KEY_DIR,

	.config_file = DEFAULT_CONFIG_FILE,
	.db_path = DEFAULT_DB,
	.sbin_dir = DEFAULT_SBIN_DIR,
	.pid_path = DEFAULT_PID,
	.socket_path = DEFAULT_SOCKET,
	.ctl_file = "asterisk.ctl",
};

const char *ast_config_AST_CACHE_DIR	= cfg_paths.cache_dir;
const char *ast_config_AST_CONFIG_DIR	= cfg_paths.config_dir;
const char *ast_config_AST_CONFIG_FILE	= cfg_paths.config_file;
const char *ast_config_AST_MODULE_DIR	= cfg_paths.module_dir;
const char *ast_config_AST_SPOOL_DIR	= cfg_paths.spool_dir;
const char *ast_config_AST_MONITOR_DIR	= cfg_paths.monitor_dir;
const char *ast_config_AST_RECORDING_DIR	= cfg_paths.recording_dir;
const char *ast_config_AST_VAR_DIR	= cfg_paths.var_dir;
const char *ast_config_AST_DATA_DIR	= cfg_paths.data_dir;
const char *ast_config_AST_LOG_DIR	= cfg_paths.log_dir;
const char *ast_config_AST_AGI_DIR	= cfg_paths.agi_dir;
const char *ast_config_AST_KEY_DIR	= cfg_paths.key_dir;
const char *ast_config_AST_RUN_DIR	= cfg_paths.run_dir;
const char *ast_config_AST_SBIN_DIR = cfg_paths.sbin_dir;

const char *ast_config_AST_DB		= cfg_paths.db_path;
const char *ast_config_AST_PID		= cfg_paths.pid_path;
const char *ast_config_AST_SOCKET	= cfg_paths.socket_path;
const char *ast_config_AST_RUN_USER	= cfg_paths.run_user;
const char *ast_config_AST_RUN_GROUP	= cfg_paths.run_group;
const char *ast_config_AST_SYSTEM_NAME	= cfg_paths.system_name;

const char *ast_config_AST_CTL_PERMISSIONS = cfg_paths.ctl_perms;
const char *ast_config_AST_CTL_OWNER = cfg_paths.ctl_owner;
const char *ast_config_AST_CTL_GROUP = cfg_paths.ctl_group;
const char *ast_config_AST_CTL = cfg_paths.ctl_file;

/*! \brief Set maximum open files */
static void set_ulimit(int value)
{
	struct rlimit l = {0, 0};

	if (value <= 0) {
		ast_log(LOG_WARNING, "Unable to change max files open to invalid value %i\n",value);
		return;
	}

	l.rlim_cur = value;
	l.rlim_max = value;

	if (setrlimit(RLIMIT_NOFILE, &l)) {
		ast_log(LOG_WARNING, "Unable to disable core size resource limit: %s\n",strerror(errno));
		return;
	}

	ast_log(LOG_NOTICE, "Setting max files open to %d\n",value);

	return;
}

void set_asterisk_conf_path(const char *path)
{
	ast_copy_string(cfg_paths.config_file, path, sizeof(cfg_paths.config_file));
}

void set_socket_path(const char *path)
{
	ast_copy_string(cfg_paths.socket_path, path, sizeof(cfg_paths.socket_path));
}

void load_asterisk_conf(void)
{
	struct ast_config *cfg;
	struct ast_variable *v;
	char hostname[MAXHOSTNAMELEN] = "";
	struct ast_flags config_flags = { CONFIG_FLAG_NOREALTIME };
	struct {
		unsigned int dbdir:1;
		unsigned int keydir:1;
	} found = { 0, 0 };
	/* Default to false for security */
	int live_dangerously = 0;
	int option_debug_new = 0;
	int option_trace_new = 0;
	int option_verbose_new = 0;

	/* init with buildtime config */
#ifdef REF_DEBUG
	/* The REF_DEBUG compiler flag is now only used to enable refdebug by default.
	 * Support for debugging reference counts is always compiled in. */
	ast_set2_flag(&ast_options, 1, AST_OPT_FLAG_REF_DEBUG);
#endif

	ast_set_default_eid(&ast_eid_default);

	cfg = ast_config_load2(ast_config_AST_CONFIG_FILE, "" /* core, can't reload */, config_flags);

	/* If AST_OPT_FLAG_EXEC_INCLUDES was previously enabled with -X turn it off now.
	 * Using #exec from other configs requires that it be enabled from asterisk.conf. */
	ast_clear_flag(&ast_options, AST_OPT_FLAG_EXEC_INCLUDES);

	/* no asterisk.conf? no problem, use buildtime config! */
	if (cfg == CONFIG_STATUS_FILEMISSING || cfg == CONFIG_STATUS_FILEUNCHANGED || cfg == CONFIG_STATUS_FILEINVALID) {
		fprintf(stderr, "Unable to open specified master config file '%s', using built-in defaults\n", ast_config_AST_CONFIG_FILE);
		return;
	}

	for (v = ast_variable_browse(cfg, "files"); v; v = v->next) {
		if (!strcasecmp(v->name, "astctlpermissions")) {
			ast_copy_string(cfg_paths.ctl_perms, v->value, sizeof(cfg_paths.ctl_perms));
		} else if (!strcasecmp(v->name, "astctlowner")) {
			ast_copy_string(cfg_paths.ctl_owner, v->value, sizeof(cfg_paths.ctl_owner));
		} else if (!strcasecmp(v->name, "astctlgroup")) {
			ast_copy_string(cfg_paths.ctl_group, v->value, sizeof(cfg_paths.ctl_group));
		} else if (!strcasecmp(v->name, "astctl")) {
			ast_copy_string(cfg_paths.ctl_file, v->value, sizeof(cfg_paths.ctl_file));
		}
	}

	for (v = ast_variable_browse(cfg, "directories"); v; v = v->next) {
		if (!strcasecmp(v->name, "astcachedir")) {
			ast_copy_string(cfg_paths.cache_dir, v->value, sizeof(cfg_paths.cache_dir));
		} else if (!strcasecmp(v->name, "astetcdir")) {
			ast_copy_string(cfg_paths.config_dir, v->value, sizeof(cfg_paths.config_dir));
		} else if (!strcasecmp(v->name, "astspooldir")) {
			ast_copy_string(cfg_paths.spool_dir, v->value, sizeof(cfg_paths.spool_dir));
			snprintf(cfg_paths.monitor_dir, sizeof(cfg_paths.monitor_dir), "%s/monitor", v->value);
			snprintf(cfg_paths.recording_dir, sizeof(cfg_paths.recording_dir), "%s/recording", v->value);
		} else if (!strcasecmp(v->name, "astvarlibdir")) {
			ast_copy_string(cfg_paths.var_dir, v->value, sizeof(cfg_paths.var_dir));
			if (!found.dbdir) {
				snprintf(cfg_paths.db_path, sizeof(cfg_paths.db_path), "%s/astdb", v->value);
			}
		} else if (!strcasecmp(v->name, "astdbdir")) {
			snprintf(cfg_paths.db_path, sizeof(cfg_paths.db_path), "%s/astdb", v->value);
			found.dbdir = 1;
		} else if (!strcasecmp(v->name, "astdatadir")) {
			ast_copy_string(cfg_paths.data_dir, v->value, sizeof(cfg_paths.data_dir));
			if (!found.keydir) {
				snprintf(cfg_paths.key_dir, sizeof(cfg_paths.key_dir), "%s/keys", v->value);
			}
		} else if (!strcasecmp(v->name, "astkeydir")) {
			snprintf(cfg_paths.key_dir, sizeof(cfg_paths.key_dir), "%s/keys", v->value);
			found.keydir = 1;
		} else if (!strcasecmp(v->name, "astlogdir")) {
			ast_copy_string(cfg_paths.log_dir, v->value, sizeof(cfg_paths.log_dir));
		} else if (!strcasecmp(v->name, "astagidir")) {
			ast_copy_string(cfg_paths.agi_dir, v->value, sizeof(cfg_paths.agi_dir));
		} else if (!strcasecmp(v->name, "astrundir")) {
			snprintf(cfg_paths.pid_path, sizeof(cfg_paths.pid_path), "%s/%s", v->value, "asterisk.pid");
			ast_copy_string(cfg_paths.run_dir, v->value, sizeof(cfg_paths.run_dir));
		} else if (!strcasecmp(v->name, "astmoddir")) {
			ast_copy_string(cfg_paths.module_dir, v->value, sizeof(cfg_paths.module_dir));
		} else if (!strcasecmp(v->name, "astsbindir")) {
			ast_copy_string(cfg_paths.sbin_dir, v->value, sizeof(cfg_paths.sbin_dir));
		}
	}

	/* Combine astrundir and astctl settings. */
	snprintf(cfg_paths.socket_path, sizeof(cfg_paths.socket_path), "%s/%s",
		ast_config_AST_RUN_DIR, ast_config_AST_CTL);

	for (v = ast_variable_browse(cfg, "options"); v; v = v->next) {
		/* verbose level (-v at startup) */
		if (!strcasecmp(v->name, "verbose")) {
			option_verbose_new = atoi(v->value);
		/* whether or not to force timestamping in CLI verbose output. (-T at startup) */
		} else if (!strcasecmp(v->name, "timestamp")) {
			ast_set2_flag(&ast_options, ast_true(v->value), AST_OPT_FLAG_TIMESTAMP);
		/* whether or not to support #exec in config files */
		} else if (!strcasecmp(v->name, "execincludes")) {
			ast_set2_flag(&ast_options, ast_true(v->value), AST_OPT_FLAG_EXEC_INCLUDES);
		/* debug level (-d at startup) */
		} else if (!strcasecmp(v->name, "debug")) {
			option_debug_new = 0;
			if (sscanf(v->value, "%30d", &option_debug_new) != 1) {
				option_debug_new = ast_true(v->value) ? 1 : 0;
			}
		} else if (!strcasecmp(v->name, "trace")) {
			option_trace_new = 0;
			if (sscanf(v->value, "%30d", &option_trace_new) != 1) {
				option_trace_new = ast_true(v->value) ? 1 : 0;
			}
		} else if (!strcasecmp(v->name, "refdebug")) {
			ast_set2_flag(&ast_options, ast_true(v->value), AST_OPT_FLAG_REF_DEBUG);
#if HAVE_WORKING_FORK
		/* Disable forking (-f at startup) */
		} else if (!strcasecmp(v->name, "nofork")) {
			ast_set2_flag(&ast_options, ast_true(v->value), AST_OPT_FLAG_NO_FORK);
		/* Always fork, even if verbose or debug are enabled (-F at startup) */
		} else if (!strcasecmp(v->name, "alwaysfork")) {
			ast_set2_flag(&ast_options, ast_true(v->value), AST_OPT_FLAG_ALWAYS_FORK);
#endif
		/* Run quietly (-q at startup ) */
		} else if (!strcasecmp(v->name, "quiet")) {
			ast_set2_flag(&ast_options, ast_true(v->value), AST_OPT_FLAG_QUIET);
		/* Run as console (-c at startup, implies nofork) */
		} else if (!strcasecmp(v->name, "console")) {
			if (!ast_opt_remote) {
				ast_set2_flag(&ast_options, ast_true(v->value), AST_OPT_FLAG_NO_FORK | AST_OPT_FLAG_CONSOLE);
			}
		/* Run with high priority if the O/S permits (-p at startup) */
		} else if (!strcasecmp(v->name, "highpriority")) {
			ast_set2_flag(&ast_options, ast_true(v->value), AST_OPT_FLAG_HIGH_PRIORITY);
		/* Initialize RSA auth keys (IAX2) (-i at startup) */
		} else if (!strcasecmp(v->name, "initcrypto")) {
			ast_set2_flag(&ast_options, ast_true(v->value), AST_OPT_FLAG_INIT_KEYS);
		/* Disable ANSI colors for console (-c at startup) */
		} else if (!strcasecmp(v->name, "nocolor")) {
			ast_set2_flag(&ast_options, ast_true(v->value), AST_OPT_FLAG_NO_COLOR);
		/* Disable some usage warnings for picky people :p */
		} else if (!strcasecmp(v->name, "dontwarn")) {
			ast_set2_flag(&ast_options, ast_true(v->value), AST_OPT_FLAG_DONT_WARN);
		/* Dump core in case of crash (-g) */
		} else if (!strcasecmp(v->name, "dumpcore")) {
			ast_set2_flag(&ast_options, ast_true(v->value), AST_OPT_FLAG_DUMP_CORE);
		/* Cache recorded sound files to another directory during recording */
		} else if (!strcasecmp(v->name, "cache_record_files")) {
			ast_set2_flag(&ast_options, ast_true(v->value), AST_OPT_FLAG_CACHE_RECORD_FILES);
#if !defined(LOW_MEMORY)
		/* Cache media frames for performance */
		} else if (!strcasecmp(v->name, "cache_media_frames")) {
			ast_set2_flag(&ast_options, ast_true(v->value), AST_OPT_FLAG_CACHE_MEDIA_FRAMES);
#endif
		/* Specify cache directory */
		} else if (!strcasecmp(v->name, "record_cache_dir")) {
			ast_copy_string(record_cache_dir, v->value, AST_CACHE_DIR_LEN);
		/* Build transcode paths via SLINEAR, instead of directly */
		} else if (!strcasecmp(v->name, "transcode_via_sln")) {
			ast_set2_flag(&ast_options, ast_true(v->value), AST_OPT_FLAG_TRANSCODE_VIA_SLIN);
		/* Transmit SLINEAR silence while a channel is being recorded or DTMF is being generated on a channel */
		} else if (!strcasecmp(v->name, "transmit_silence_during_record") || !strcasecmp(v->name, "transmit_silence")) {
			ast_set2_flag(&ast_options, ast_true(v->value), AST_OPT_FLAG_TRANSMIT_SILENCE);
		} else if (!strcasecmp(v->name, "mindtmfduration")) {
			if (sscanf(v->value, "%30u", &option_dtmfminduration) != 1) {
				option_dtmfminduration = AST_MIN_DTMF_DURATION;
			}
		} else if (!strcasecmp(v->name, "rtp_use_dynamic")) {
			ast_option_rtpusedynamic = ast_true(v->value);
		/* http://www.iana.org/assignments/rtp-parameters
		 * RTP dynamic payload types start at 96 normally; extend down to 0 */
		} else if (!strcasecmp(v->name, "rtp_pt_dynamic")) {
			ast_parse_arg(v->value, PARSE_UINT32|PARSE_IN_RANGE,
			              &ast_option_rtpptdynamic, 0, AST_RTP_PT_FIRST_DYNAMIC);
		} else if (!strcasecmp(v->name, "maxcalls")) {
			if ((sscanf(v->value, "%30d", &ast_option_maxcalls) != 1) || (ast_option_maxcalls < 0)) {
				ast_option_maxcalls = 0;
			}
		} else if (!strcasecmp(v->name, "maxload")) {
			double test[1];

			if (getloadavg(test, 1) == -1) {
				ast_log(LOG_ERROR, "Cannot obtain load average on this system. 'maxload' option disabled.\n");
				ast_option_maxload = 0.0;
			} else if ((sscanf(v->value, "%30lf", &ast_option_maxload) != 1) || (ast_option_maxload < 0.0)) {
				ast_option_maxload = 0.0;
			}
		/* Set the maximum amount of open files */
		} else if (!strcasecmp(v->name, "maxfiles")) {
			ast_option_maxfiles = atoi(v->value);
			if (!ast_opt_remote) {
				set_ulimit(ast_option_maxfiles);
			}
		/* What user to run as */
		} else if (!strcasecmp(v->name, "runuser")) {
			ast_copy_string(cfg_paths.run_user, v->value, sizeof(cfg_paths.run_user));
		/* What group to run as */
		} else if (!strcasecmp(v->name, "rungroup")) {
			ast_copy_string(cfg_paths.run_group, v->value, sizeof(cfg_paths.run_group));
		} else if (!strcasecmp(v->name, "systemname")) {
			ast_copy_string(cfg_paths.system_name, v->value, sizeof(cfg_paths.system_name));
		} else if (!strcasecmp(v->name, "autosystemname")) {
			if (ast_true(v->value)) {
				if (!gethostname(hostname, sizeof(hostname) - 1)) {
					ast_copy_string(cfg_paths.system_name, hostname, sizeof(cfg_paths.system_name));
				} else {
					if (ast_strlen_zero(ast_config_AST_SYSTEM_NAME)){
						ast_copy_string(cfg_paths.system_name, "localhost", sizeof(cfg_paths.system_name));
					}
					ast_log(LOG_ERROR, "Cannot obtain hostname for this system.  Using '%s' instead.\n", ast_config_AST_SYSTEM_NAME);
				}
			}
		} else if (!strcasecmp(v->name, "languageprefix")) {
			ast_language_is_prefix = ast_true(v->value);
		} else if (!strcasecmp(v->name, "defaultlanguage")) {
			ast_copy_string(ast_defaultlanguage, v->value, MAX_LANGUAGE);
		} else if (!strcasecmp(v->name, "lockmode")) {
			if (!strcasecmp(v->value, "lockfile")) {
				ast_set_lock_type(AST_LOCK_TYPE_LOCKFILE);
			} else if (!strcasecmp(v->value, "flock")) {
				ast_set_lock_type(AST_LOCK_TYPE_FLOCK);
			} else {
				ast_log(LOG_WARNING, "'%s' is not a valid setting for the lockmode option, "
					"defaulting to 'lockfile'\n", v->value);
				ast_set_lock_type(AST_LOCK_TYPE_LOCKFILE);
			}
#if defined(HAVE_SYSINFO)
		} else if (!strcasecmp(v->name, "minmemfree")) {
			/* specify the minimum amount of free memory to retain.  Asterisk should stop accepting new calls
			 * if the amount of free memory falls below this watermark */
			if ((sscanf(v->value, "%30ld", &option_minmemfree) != 1) || (option_minmemfree < 0)) {
				option_minmemfree = 0;
			}
#endif
		} else if (!strcasecmp(v->name, "entityid")) {
			struct ast_eid tmp_eid;
			if (!ast_str_to_eid(&tmp_eid, v->value)) {
				ast_eid_default = tmp_eid;
			} else {
				ast_log(LOG_WARNING, "Invalid Entity ID '%s' provided\n", v->value);
			}
		} else if (!strcasecmp(v->name, "lightbackground")) {
			ast_set2_flag(&ast_options, ast_true(v->value), AST_OPT_FLAG_LIGHT_BACKGROUND);
		} else if (!strcasecmp(v->name, "forceblackbackground")) {
			ast_set2_flag(&ast_options, ast_true(v->value), AST_OPT_FLAG_FORCE_BLACK_BACKGROUND);
		} else if (!strcasecmp(v->name, "hideconnect")) {
			ast_set2_flag(&ast_options, ast_true(v->value), AST_OPT_FLAG_HIDE_CONSOLE_CONNECT);
		} else if (!strcasecmp(v->name, "lockconfdir")) {
			ast_set2_flag(&ast_options, ast_true(v->value),	AST_OPT_FLAG_LOCK_CONFIG_DIR);
		} else if (!strcasecmp(v->name, "stdexten")) {
			/* Choose how to invoke the extensions.conf stdexten */
			if (!strcasecmp(v->value, "gosub")) {
				ast_clear_flag(&ast_options, AST_OPT_FLAG_STDEXTEN_MACRO);
			} else if (!strcasecmp(v->value, "macro")) {
				ast_set_flag(&ast_options, AST_OPT_FLAG_STDEXTEN_MACRO);
			} else {
				ast_log(LOG_WARNING,
					"'%s' is not a valid setting for the stdexten option, defaulting to 'gosub'\n",
					v->value);
				ast_clear_flag(&ast_options, AST_OPT_FLAG_STDEXTEN_MACRO);
			}
		} else if (!strcasecmp(v->name, "live_dangerously")) {
			live_dangerously = ast_true(v->value);
		} else if (!strcasecmp(v->name, "hide_messaging_ami_events")) {
			ast_set2_flag(&ast_options, ast_true(v->value), AST_OPT_FLAG_HIDE_MESSAGING_AMI_EVENTS);
		} else if (!strcasecmp(v->name, "sounds_search_custom_dir")) {
			ast_set2_flag(&ast_options, ast_true(v->value), AST_OPT_FLAG_SOUNDS_SEARCH_CUSTOM);
		}
	}
	if (!ast_opt_remote) {
		pbx_live_dangerously(live_dangerously);
		astman_live_dangerously(live_dangerously);
	}

	option_debug += option_debug_new;
	option_trace += option_trace_new;
	option_verbose += option_verbose_new;

	ast_config_destroy(cfg);
}
