/*
* Asterisk -- An open source telephony toolkit.
*
* Copyright (C) 2018, CFWare, LLC.
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

#ifndef FEATURES_CONFIG_H_
#define FEATURES_CONFIG_H_

int load_features_config(void);

int reload_features_config(void);

void unload_features_config(void);

#endif /* FEATURES_CONFIG_H_ */
