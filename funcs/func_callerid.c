/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Caller ID related dialplan functions
 * 
 * Copyright (C) 2005, Digium, Inc.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "asterisk.h"

/* ASTERISK_FILE_VERSION("$Revision$") */

#ifndef BUILTIN_FUNC
#include "asterisk/module.h"
#endif /* BUILTIN_FUNC */
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/logger.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/options.h"

static char *callerid_read(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len) 
{
	if (strncasecmp("name", data, 4) == 0) {
		if (chan->cid.cid_name) {
			ast_copy_string(buf, chan->cid.cid_name, len);
		}
	} else if (strncasecmp("num", data, 3) == 0 || strncasecmp("number", data, 6) == 0) {
		if (chan->cid.cid_num) {
			ast_copy_string(buf, chan->cid.cid_num, len);
		}
	} else if (strncasecmp("ani", data, 3) == 0) {
		if (chan->cid.cid_ani) {
			ast_copy_string(buf, chan->cid.cid_ani, len);
		}
	} else if (strncasecmp("dnid", data, 4) == 0) {
		if (chan->cid.cid_dnid) {
			ast_copy_string(buf, chan->cid.cid_dnid, len);
		}
	} else if (strncasecmp("rdnis", data, 5) == 0) {
		if (chan->cid.cid_rdnis) {
			ast_copy_string(buf, chan->cid.cid_rdnis, len);
		}
	} else {
		ast_log(LOG_ERROR, "Unknown callerid data type.\n");
	}

	return buf;
}

static void callerid_write(struct ast_channel *chan, char *cmd, char *data, const char *value) 
{
	if (!value)
                return;

        if (strncasecmp("name", data, 4) == 0) {
                ast_set_callerid(chan, NULL, value, NULL);
        } else if (strncasecmp("num", data, 3) == 0 || strncasecmp("number", data, 6) == 0) {
                ast_set_callerid(chan, value, NULL, NULL);
        } else if (strncasecmp("ani", data, 3) == 0) {
                ast_set_callerid(chan, NULL, NULL, value);
        } else if (strncasecmp("dnid", data, 4) == 0) {
                /* do we need to lock chan here? */
                if (chan->cid.cid_dnid)
                        free(chan->cid.cid_dnid);
                chan->cid.cid_dnid = ast_strlen_zero(value) ? NULL : strdup(value);
        } else if (strncasecmp("rdnis", data, 5) == 0) {
                /* do we need to lock chan here? */
                if (chan->cid.cid_rdnis)
                        free(chan->cid.cid_rdnis);
                chan->cid.cid_rdnis = ast_strlen_zero(value) ? NULL : strdup(value);
        } else {
                ast_log(LOG_ERROR, "Unknown callerid data type.\n");
        }
}

#ifndef BUILTIN_FUNC
static
#endif /* BUILTIN_FUNC */
struct ast_custom_function callerid_function = {
	.name = "CALLERID",
	.synopsis = "Gets or sets Caller*ID data on the channel.",
	.syntax = "CALLERID(datatype)",
	.desc = "Gets or sets Caller*ID data on the channel.  The allowable datatypes\n"
	"are \"name\", \"number\", \"ANI\", \"DNID\", \"RDNIS\".\n",
	.read = callerid_read,
	.write = callerid_write,
};

#ifndef BUILTIN_FUNC
static char *tdesc = "Caller ID related dialplan function";

int unload_module(void)
{
        return ast_custom_function_unregister(&callerid_function);
}

int load_module(void)
{
        return ast_custom_function_register(&callerid_function);
}

char *description(void)
{
	return tdesc;
}

int usecount(void)
{
	return 0;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
#endif /* BUILTIN_FUNC */

/*
Local Variables:
mode: C
c-file-style: "linux"
indent-tabs-mode: nil
End:
*/
