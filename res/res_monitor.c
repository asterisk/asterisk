#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <libgen.h>		//dirname()

#include <asterisk/lock.h>
#include <asterisk/channel.h>
#include <asterisk/logger.h>
#include <asterisk/file.h>
#include <asterisk/pbx.h>
#include <asterisk/module.h>
#include <asterisk/manager.h>
#include <asterisk/cli.h>
#include <asterisk/monitor.h>
#include <asterisk/app.h>
#include <asterisk/utils.h>
#include "../asterisk.h"
#include "../astconf.h"

#define AST_MONITOR_DIR	AST_SPOOL_DIR "/monitor"

static ast_mutex_t monitorlock = AST_MUTEX_INITIALIZER;

static unsigned long seq = 0;

static char *monitor_synopsis = "Monitor a channel";

static char *monitor_descrip = "Monitor([file_format|[fname_base]|[options]]):\n"
"Used to start monitoring a channel. The channel's input and output\n"
"voice packets are logged to files until the channel hangs up or\n"
"monitoring is stopped by the StopMonitor application.\n"
"      file_format -- optional, if not set, defaults to \"wav\"\n"
"      fname_base -- if set, changes the filename used to the one specified.\n"
"      options:\n"
"              'm' - when the recording ends mix the two leg files into one and\n"
"                    delete the two leg files.  If MONITOR_EXEC is set, the\n"
"                    application refernced in it will be executed instead of\n"
"                    soxmix and the raw leg files will NOT be deleted automatically.\n"
"                    soxmix or MONITOR_EXEC is handed 3 arguments, the two leg files\n"
"                    and a target mixed file name which is the same as the leg file names\n"
"                    only without the in/out designator.\n\n"
"                    Both MONITOR_EXEC and the Mix flag can be set from the\n"
"                    administrator interface\n";

static char *stopmonitor_synopsis = "Stop monitoring a channel";

static char *stopmonitor_descrip = "StopMonitor\n"
	"Stops monitoring a channel. Has no effect if the channel is not monitored\n";

static char *changemonitor_synopsis = "Change monitoring filename of a channel";

static char *changemonitor_descrip = "ChangeMonitor\n"
	"Changes monitoring filename of a channel. Has no effect if the channel is not monitored\n"
	"The option string may contain the following:\n"
	"	filename_base -- if set, changes the filename used to the one specified.\n";

/* Start monitoring a channel */
int ast_monitor_start(	struct ast_channel *chan, const char *format_spec,
						const char *fname_base, int need_lock )
{
	int res = 0;
	char tmp[256];

	if( need_lock ) {
		if (ast_mutex_lock(&chan->lock)) {
			ast_log(LOG_WARNING, "Unable to lock channel\n");
			return -1;
		}
	}

	if( !(chan->monitor) ) {
		struct ast_channel_monitor *monitor;
		char *channel_name, *p;

		/* Create monitoring directory if needed */
		if( mkdir( AST_MONITOR_DIR, 0770 ) < 0 ) {
			if( errno != EEXIST ) {
				ast_log(LOG_WARNING, "Unable to create audio monitor directory: %s\n",
						strerror( errno ) );
			}
		}

		monitor = malloc( sizeof( struct ast_channel_monitor ) );
		memset( monitor, 0, sizeof( struct ast_channel_monitor ) );

		/* Determine file names */
		if( fname_base && strlen( fname_base ) ) {
			int directory = strchr(fname_base, '/') ? 1 : 0;
			/* try creating the directory just in case it doesn't exist */
			if (directory) {
				char *name = strdup(fname_base);
				snprintf(tmp, sizeof(tmp), "mkdir -p %s",dirname(name));
				free(name);
				system(tmp);
			}
			snprintf(	monitor->read_filename, FILENAME_MAX, "%s/%s-in",
						directory ? "" : AST_MONITOR_DIR, fname_base );
			snprintf(	monitor->write_filename, FILENAME_MAX, "%s/%s-out",
						directory ? "" : AST_MONITOR_DIR, fname_base );
			strncpy(monitor->filename_base, fname_base, sizeof(monitor->filename_base) - 1);
		} else {
			ast_mutex_lock( &monitorlock );
			snprintf(	monitor->read_filename, FILENAME_MAX, "%s/audio-in-%ld",
						AST_MONITOR_DIR, seq );
			snprintf(	monitor->write_filename, FILENAME_MAX, "%s/audio-out-%ld",
						AST_MONITOR_DIR, seq );
			seq++;
			ast_mutex_unlock( &monitorlock );

			channel_name = strdup( chan->name );
			while( ( p = strchr( channel_name, '/' ) ) ) {
				*p = '-';
			}
			snprintf(	monitor->filename_base, FILENAME_MAX, "%s/%s",
						AST_MONITOR_DIR, channel_name );
			monitor->filename_changed = 1;
			free( channel_name );
		}

		monitor->stop = ast_monitor_stop;

		// Determine file format
		if( format_spec && strlen( format_spec ) ) {
			monitor->format = strdup( format_spec );
		} else {
			monitor->format = strdup( "wav" );
		}
		
		// open files
		if( ast_fileexists( monitor->read_filename, NULL, NULL ) > 0 ) {
			ast_filedelete( monitor->read_filename, NULL );
		}
		if( !(monitor->read_stream = ast_writefile(	monitor->read_filename,
						monitor->format, NULL,
						O_CREAT|O_TRUNC|O_WRONLY, 0, 0644 ) ) ) {
			ast_log(	LOG_WARNING, "Could not create file %s\n",
						monitor->read_filename );
			free( monitor );
			ast_mutex_unlock(&chan->lock);
			return -1;
		}
		if( ast_fileexists( monitor->write_filename, NULL, NULL ) > 0 ) {
			ast_filedelete( monitor->write_filename, NULL );
		}
		if( !(monitor->write_stream = ast_writefile( monitor->write_filename,
						monitor->format, NULL,
						O_CREAT|O_TRUNC|O_WRONLY, 0, 0644 ) ) ) {
			ast_log(	LOG_WARNING, "Could not create file %s\n",
						monitor->write_filename );
			ast_closestream( monitor->read_stream );
			free( monitor );
			ast_mutex_unlock(&chan->lock);
			return -1;
		}
		chan->monitor = monitor;
	} else {
		ast_log(	LOG_DEBUG,"Cannot start monitoring %s, already monitored\n",
					chan->name );
		res = -1;
	}

	if( need_lock ) {
		ast_mutex_unlock(&chan->lock);
	}
	return res;
}

/* Stop monitoring a channel */
int ast_monitor_stop( struct ast_channel *chan, int need_lock )
{
  char *execute=NULL;
  int soxmix =0;
	if(need_lock) {
		if(ast_mutex_lock(&chan->lock)) {
			ast_log(LOG_WARNING, "Unable to lock channel\n");
			return -1;
		}
	}

	if(chan->monitor) {
		char filename[ FILENAME_MAX ];

		if(chan->monitor->read_stream) {
			ast_closestream( chan->monitor->read_stream );
		}
		if(chan->monitor->write_stream) {
			ast_closestream( chan->monitor->write_stream );
		}

		if(chan->monitor->filename_changed&&strlen(chan->monitor->filename_base)) {
			if( ast_fileexists(chan->monitor->read_filename,NULL,NULL) > 0 ) {
				snprintf(	filename, FILENAME_MAX, "%s-in",
							chan->monitor->filename_base );
				if(ast_fileexists( filename, NULL, NULL ) > 0) {
					ast_filedelete( filename, NULL );
				}
				ast_filerename(	chan->monitor->read_filename, filename,
								chan->monitor->format );
			} else {
				ast_log(	LOG_WARNING, "File %s not found\n",
							chan->monitor->read_filename );
			}

			if(ast_fileexists(chan->monitor->write_filename,NULL,NULL) > 0) {
				snprintf(	filename, FILENAME_MAX, "%s-out",
							chan->monitor->filename_base );
				if( ast_fileexists( filename, NULL, NULL ) > 0 ) {
					ast_filedelete( filename, NULL );
				}
				ast_filerename(	chan->monitor->write_filename, filename,
								chan->monitor->format );
			} else {
				ast_log(	LOG_WARNING, "File %s not found\n",
							chan->monitor->write_filename );
			}
		}

		if (chan->monitor->joinfiles && !ast_strlen_zero(execute) && strlen(chan->monitor->filename_base)) {
			char tmp[1024];
			char tmp2[1024];
			char *format = !strcasecmp(chan->monitor->format,"wav49") ? "WAV" : chan->monitor->format;
			char *name = chan->monitor->filename_base;
			int directory = strchr(name, '/') ? 1 : 0;
			char *dir = directory ? "" : AST_MONITOR_DIR;

		        /* Set the execute application */
		        execute=pbx_builtin_getvar_helper(chan,"MONITOR_EXEC");
		        if (!execute || ast_strlen_zero(execute)) { 
			  execute = "nice -n 19 soxmix"; 
			  soxmix = 1;
			}			
			snprintf(tmp, sizeof(tmp), "%s %s/%s-in.%s %s/%s-out.%s %s/%s.%s &", execute, dir, name, format, dir, name, format, dir, name, format);
			if (soxmix) 
			  snprintf(tmp2,sizeof(tmp2), "%s& rm -f %s/%s-* &",tmp, dir ,name); /* remove legs when done mixing */
			ast_verbose("monitor executing %s\n",tmp);
			if (ast_safe_system(tmp) == -1)
			  ast_log(LOG_WARNING, "Execute of %s failed.\n",tmp);
		}
		
		free( chan->monitor->format );
		free( chan->monitor );
		chan->monitor = NULL;
	}

	if( need_lock ) {
		ast_mutex_unlock(&chan->lock);
	}
	return 0;
}

/* Change monitoring filename of a channel */
int ast_monitor_change_fname(	struct ast_channel *chan,
								const char *fname_base, int need_lock )
{
	char tmp[256];
	if( (!fname_base) || (!strlen(fname_base)) ) {
		ast_log(	LOG_WARNING,
					"Cannot change monitor filename of channel %s to null",
					chan->name );
		return -1;
	}
	
	if( need_lock ) {
		if (ast_mutex_lock(&chan->lock)) {
			ast_log(LOG_WARNING, "Unable to lock channel\n");
			return -1;
		}
	}

	if( chan->monitor ) {
		int directory = strchr(fname_base, '/') ? 1 : 0;
		/* try creating the directory just in case it doesn't exist */
		if (directory) {
			char *name = strdup(fname_base);
			snprintf(tmp, sizeof(tmp), "mkdir -p %s",dirname(name));
			free(name);
			system(tmp);
		}

		snprintf(	chan->monitor->filename_base, FILENAME_MAX, "%s/%s",
					directory ? "" : AST_MONITOR_DIR, fname_base );
	} else {
		ast_log(	LOG_WARNING,
					"Cannot change monitor filename of channel %s to %s, monitoring not started",
					chan->name, fname_base );
				
	}

	if( need_lock ) {
		ast_mutex_unlock(&chan->lock);
	}
	return 0;
}

static int start_monitor_exec(struct ast_channel *chan, void *data)
{
	char *arg = NULL;
	char *format = NULL;
	char *fname_base = NULL;
	char *options = NULL;
	int joinfiles = 0;
	int res;
	
	/* Parse arguments. */
	if( data && strlen((char*)data) ) {
		arg = strdup( (char*)data );
		format = arg;
		fname_base = strchr( arg, '|' );
		if( fname_base ) {
			*fname_base = 0;
			fname_base++;
			if ((options = strchr(fname_base, '|'))) {
				*options = 0;
				options++;
				if (strchr(options, 'm'))
					joinfiles = 1;
			}
		}
		
	}

	res = ast_monitor_start( chan, format, fname_base, 1 );
	if( res < 0 ) {
		res = ast_monitor_change_fname( chan, fname_base, 1 );
	}
	ast_monitor_setjoinfiles(chan, joinfiles);

	if( arg ) {
		free( arg );
	}

	return res;
}

static int stop_monitor_exec(struct ast_channel *chan, void *data)
{
	return ast_monitor_stop( chan, 1 );
}

static int change_monitor_exec(struct ast_channel *chan, void *data)
{
	return ast_monitor_change_fname( chan, (const char*)data, 1 );
}

static int start_monitor_action(struct mansession *s, struct message *m)
{
	struct ast_channel *c = NULL;
	char *name = astman_get_header(m, "Channel");
	char *fname = astman_get_header(m, "File");
	char *format = astman_get_header(m, "Format");
	char *mix = astman_get_header(m, "Mix");
	char *d;
	
	if((!name)||(!strlen(name))) {
		astman_send_error(s, m, "No channel specified");
		return 0;
	}
	c = ast_channel_walk_locked(NULL);
	while(c) {
		if (!strcasecmp(c->name, name)) {
			break;
		}
		ast_mutex_unlock(&c->lock);
		c = ast_channel_walk_locked(c);
	}
	if (!c) {
		astman_send_error(s, m, "No such channel");
		return 0;
	}
     
	if( (!fname) || (!strlen(fname)) ) {
		// No filename base specified, default to channel name as per CLI
		fname = malloc (FILENAME_MAX);
		memset( fname, 0, FILENAME_MAX);
		strncpy( fname, c->name, FILENAME_MAX-1);
		// Channels have the format technology/channel_name - have to replace that / 
		if( (d=strchr( fname, '/')) ) *d='-';
	}
	
	if( ast_monitor_start( c, format, fname, 1 ) ) {
		if( ast_monitor_change_fname( c, fname, 1 ) ) {
			astman_send_error(s, m, "Could not start monitoring channel");
			return 0;
		}
	}

	if(ast_true(mix)) {
	  ast_monitor_setjoinfiles( c, 1);
	}

	ast_mutex_unlock(&c->lock);
	astman_send_ack(s, m, "Started monitoring channel");
	return 0;
}

static int stop_monitor_action(struct mansession *s, struct message *m)
{
	struct ast_channel *c = NULL;
	char *name = astman_get_header(m, "Channel");
	int res;
	if((!name)||(!strlen(name))) {
		astman_send_error(s, m, "No channel specified");
		return 0;
	}
	c = ast_channel_walk_locked(NULL);
	while(c) {
		if (!strcasecmp(c->name, name)) {
			break;
		}
		ast_mutex_unlock(&c->lock);
		c = ast_channel_walk_locked(c);
	}
	if (!c) {
		astman_send_error(s, m, "No such channel");
		return 0;
	}
	res = ast_monitor_stop( c, 1 );
	ast_mutex_unlock(&c->lock);
	if( res ) {
		astman_send_error(s, m, "Could not stop monitoring channel");
		return 0;
	}
	astman_send_ack(s, m, "Stopped monitoring channel");
	return 0;
}

static int change_monitor_action(struct mansession *s, struct message *m)
{
	struct ast_channel *c = NULL;
	char *name = astman_get_header(m, "Channel");
	char *fname = astman_get_header(m, "File");
	if((!name) || (!strlen(name))) {
		astman_send_error(s, m, "No channel specified");
		return 0;
	}
	if ((!fname)||(!strlen(fname))) {
		astman_send_error(s, m, "No filename specified");
		return 0;
	}
	c = ast_channel_walk_locked(NULL);
	while(c) {
		if (!strcasecmp(c->name, name)) {
			break;
		}
		ast_mutex_unlock(&c->lock);
		c = ast_channel_walk_locked(c);
	}
	if (!c) {
		astman_send_error(s, m, "No such channel");
		return 0;
	}
	if( ast_monitor_change_fname( c, fname, 1 ) ) {
		astman_send_error(s, m, "Could not change monitored filename of channel");
		return 0;
	}
	astman_send_ack(s, m, "Stopped monitoring channel");
	return 0;
}

void ast_monitor_setjoinfiles(struct ast_channel *chan, int turnon)
{
	if (chan->monitor)
		chan->monitor->joinfiles = turnon;
}

int load_module(void)
{
	ast_register_application( "Monitor", start_monitor_exec, monitor_synopsis, monitor_descrip );
	ast_register_application( "StopMonitor", stop_monitor_exec, stopmonitor_synopsis, stopmonitor_descrip );
	ast_register_application( "ChangeMonitor", change_monitor_exec, changemonitor_synopsis, changemonitor_descrip );
	ast_manager_register( "Monitor", EVENT_FLAG_CALL, start_monitor_action, monitor_synopsis );
	ast_manager_register( "StopMonitor", EVENT_FLAG_CALL, stop_monitor_action, stopmonitor_synopsis );
	ast_manager_register( "ChangeMonitor", EVENT_FLAG_CALL, change_monitor_action, changemonitor_synopsis );

	return 0;
}

int unload_module(void)
{
	ast_unregister_application( "Monitor" );
	ast_unregister_application( "StopMonitor" );
	return 0;
}

char *description(void)
{
	return "Call Monitoring Resource";
}

int usecount(void)
{
	/* Never allow monitor to be unloaded because it will
	   unresolve needed symbols in the channel */
#if 0
	int res;
	STANDARD_USECOUNT(res);
	return res;
#else
	return 1;
#endif
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
