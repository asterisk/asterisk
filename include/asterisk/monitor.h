#ifndef _MONITOR_H
#define _MONITOR_H

#include <stdio.h>

#include "asterisk/channel.h"

struct ast_channel;

/*! Responsible for channel monitoring data */
struct ast_channel_monitor
{
	struct ast_filestream *read_stream;
	struct ast_filestream *write_stream;
	char read_filename[ FILENAME_MAX ];
	char write_filename[ FILENAME_MAX ];
	char filename_base[ FILENAME_MAX ];
	int filename_changed;
	char *format;
	int joinfiles;
	int (*stop)( struct ast_channel *chan, int need_lock);
};

/* Start monitoring a channel */
int ast_monitor_start(	struct ast_channel *chan, const char *format_spec,
						const char *fname_base, int need_lock );

/* Stop monitoring a channel */
int ast_monitor_stop( struct ast_channel *chan, int need_lock);

/* Change monitoring filename of a channel */
int ast_monitor_change_fname(	struct ast_channel *chan,
								const char *fname_base, int need_lock );

void ast_monitor_setjoinfiles(struct ast_channel *chan, int turnon);

#endif /* _MONITOR_H */
