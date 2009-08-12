/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Copyright (C) 2005, Oxymium sarl
 * Manuel Guesdon <mguesdon@oxymium.net> - LDAP RealTime Driver Author/Adaptor
 *
 * Copyright (C) 2007, Digium, Inc.
 * Russell Bryant <russell@digium.com>
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
 *
 */

/*! \file
 *
 * \brief ldap plugin for portable configuration engine (ARA)
 *
 * \author Mark Spencer <markster@digium.com>
 * \author Manuel Guesdon
 * \author Carl-Einar Thorner <cthorner@voicerd.com>
 * \author Russell Bryant <russell@digium.com>
 *
 * \arg http://www.openldap.org
 */

/*** MODULEINFO
	<depend>ldap</depend>
 ***/

#include "asterisk.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <ldap.h>

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/channel.h"
#include "asterisk/logger.h"
#include "asterisk/config.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/options.h"
#include "asterisk/cli.h"
#include "asterisk/utils.h"
#include "asterisk/strings.h"
#include "asterisk/pbx.h"
#include "asterisk/linkedlists.h"

#define RES_CONFIG_LDAP_CONF "res_ldap.conf"
#define RES_CONFIG_LDAP_DEFAULT_BASEDN "asterisk"

AST_MUTEX_DEFINE_STATIC(ldap_lock);

static LDAP *ldapConn;
static char url[512];
static char user[512];
static char pass[50];
static char base_distinguished_name[512];
static int version = 3;
static time_t connect_time;

static int parse_config(void);
static int ldap_reconnect(void);
static char *realtime_ldap_status(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);

struct category_and_metric {
	const char *name;
	int metric;
	const char *variable_name;
	const char *variable_value;
	int var_metric; /*!< For organizing variables (particularly includes and switch statments) within a context */
};

/*! \brief Table configuration */
struct ldap_table_config {
	char *table_name;                 /*!< table name */
	char *additional_filter;          /*!< additional filter        */
	struct ast_variable *attributes;  /*!< attribute names conversion */
	struct ast_variable *delimiters;  /*!< the current delimiter is semicolon, so we are not using this variable */
	AST_LIST_ENTRY(ldap_table_config) entry;
	/* TODO: Make proxies work */
};

/*! \brief Should be locked before using it */
static AST_LIST_HEAD_NOLOCK_STATIC(table_configs, ldap_table_config);
static struct ldap_table_config *base_table_config;
static struct ldap_table_config *static_table_config;

static struct ast_cli_entry ldap_cli[] = {
	AST_CLI_DEFINE(realtime_ldap_status, "Shows connection information for the LDAP RealTime driver"),
};

/*! \brief Create a new table_config */
static struct ldap_table_config *table_config_new(const char *table_name)
{
	struct ldap_table_config *p;

	if (!(p = ast_calloc(1, sizeof(*p))))
		return NULL;

	if (table_name) {
		if (!(p->table_name = ast_strdup(table_name))) {
			free(p);
			return NULL;
		}
	}

	return p;
}

/*! \brief Find a table_config - Should be locked before using it 
 *  \note This function assumes ldap_lock to be locked. */
static struct ldap_table_config *table_config_for_table_name(const char *table_name)
{
	struct ldap_table_config *c = NULL;

	AST_LIST_TRAVERSE(&table_configs, c, entry) {
		if (!strcmp(c->table_name, table_name))
			break;
	}

	return c;
}

/*! \brief Find variable by name */
static struct ast_variable *variable_named(struct ast_variable *var, const char *name)
{
	for (; var; var = var->next) {
		if (!strcasecmp(name, var->name))
			break;
	}

	return var;
}

/*! \brief for the semicolon delimiter
	\param somestr - pointer to a string

	\return number of occurances of the delimiter(semicolon)
 */
static int semicolon_count_str(const char *somestr)
{
	int count = 0;

	for (; *somestr; somestr++) {
		if (*somestr == ';')
			count++;
	}

	return count;
} 

/* takes a linked list of \a ast_variable variables, finds the one with the name variable_value
 * and returns the number of semicolons in the value for that \a ast_variable
 */
static int semicolon_count_var(struct ast_variable *var)
{
	struct ast_variable *var_value = variable_named(var, "variable_value");

	if (!var_value)
		return 0;

	ast_debug(1, "LINE(%d) semicolon_count_var: %s\n", __LINE__, var_value->value);

	return semicolon_count_str(var_value->value);
}

/*! \brief add attribute to table config - Should be locked before using it */
static void ldap_table_config_add_attribute(struct ldap_table_config *table_config,
	const char *attribute_name, const char *attribute_value)
{
	struct ast_variable *var;

	if (ast_strlen_zero(attribute_name) || ast_strlen_zero(attribute_value))
		return;

	if (!(var = ast_variable_new(attribute_name, attribute_value, table_config->table_name)))
		return;

	if (table_config->attributes)
		var->next = table_config->attributes;
	table_config->attributes = var;
}

/*! \brief Free table_config 
 *  \note assumes ldap_lock to be locked */
static void table_configs_free(void)
{
	struct ldap_table_config *c;

	while ((c = AST_LIST_REMOVE_HEAD(&table_configs, entry))) {
		if (c->table_name)
			free(c->table_name);
		if (c->additional_filter)
			free(c->additional_filter);
		if (c->attributes)
			ast_variables_destroy(c->attributes);
		free(c);
	}

	base_table_config = NULL;
	static_table_config = NULL;
}

/*! \brief Convert variable name to ldap attribute name - Should be locked before using it */
static const char *convert_attribute_name_to_ldap(struct ldap_table_config *table_config,
	const char *attribute_name)
{
	int i = 0;
	struct ldap_table_config *configs[] = { table_config, base_table_config };

	for (i = 0; i < ARRAY_LEN(configs); i++) {
		struct ast_variable *attribute;

		if (!configs[i])
			continue;

		attribute = configs[i]->attributes;
		for (; attribute; attribute = attribute->next) {
			if (!strcasecmp(attribute_name, attribute->name))
				return attribute->value;
		}
	}

	return attribute_name;
}

/*! \brief Convert ldap attribute name to variable name - Should be locked before using it */
static const char *convert_attribute_name_from_ldap(struct ldap_table_config *table_config,
						    const char *attribute_name)
{
	int i = 0;
	struct ldap_table_config *configs[] = { table_config, base_table_config };

	for (i = 0; i < ARRAY_LEN(configs); i++) {
		struct ast_variable *attribute;

		if (!configs[i])
			continue;

		attribute = configs[i]->attributes;
		for (; attribute; attribute = attribute->next) {
			if (strcasecmp(attribute_name, attribute->value) == 0)
				return attribute->name;
		}
	}

	return attribute_name;
}

/*! \brief Get variables from ldap entry attributes - Should be locked before using it
 * \return a linked list of ast_variable variables.
 **/
static struct ast_variable *realtime_ldap_entry_to_var(struct ldap_table_config *table_config,
	LDAPMessage *ldap_entry)
{
	BerElement *ber = NULL;
	struct ast_variable *var = NULL;
	struct ast_variable *prev = NULL;
	int is_delimited = 0;
	int i = 0;
	char *ldap_attribute_name;
	struct berval *value;
	int pos = 0;

	ldap_attribute_name = ldap_first_attribute(ldapConn, ldap_entry, &ber);

	while (ldap_attribute_name) {
		struct berval **values = NULL;
		const char *attribute_name = convert_attribute_name_from_ldap(table_config, ldap_attribute_name);
		int is_realmed_password_attribute = strcasecmp(attribute_name, "md5secret") == 0;

		values = ldap_get_values_len(ldapConn, ldap_entry, ldap_attribute_name); /* these are freed at the end */
		if (values) {
			struct berval **v;
			char *valptr;

			for (v = values; *v; v++) {
				value = *v;
				valptr = value->bv_val;
				ast_debug(2, "LINE(%d) attribute_name: %s LDAP value: %s\n", __LINE__, attribute_name, valptr);
				if (is_realmed_password_attribute) {
					if (!strncasecmp(valptr, "{md5}", 5)) {
						valptr += 5;
					} else {
						valptr = NULL;
					}
					ast_debug(2, "md5: %s\n", valptr);
				}
				if (valptr) {
					/* ok, so looping through all delimited values except the last one (not, last character is not delimited...) */
					if (is_delimited) {
						i = 0;
						pos = 0;
						while (!ast_strlen_zero(valptr + i)) {
							if (valptr[i] == ';'){
								valptr[i] = '\0';
								if (prev) {
									prev->next = ast_variable_new(attribute_name, &valptr[pos], table_config->table_name);
									if (prev->next) {
										prev = prev->next;
									}
								} else {
									prev = var = ast_variable_new(attribute_name, &valptr[pos], table_config->table_name);
								}
								pos = i + 1;
							}
							i++;
						}
					}
					/* for the last delimited value or if the value is not delimited: */
					if (prev) {
						prev->next = ast_variable_new(attribute_name, &valptr[pos], table_config->table_name);
						if (prev->next) {
							prev = prev->next;
						}
					} else {
						prev = var = ast_variable_new(attribute_name, &valptr[pos], table_config->table_name);
					}
				}
			}
			ldap_value_free_len(values);
		}
		ldap_attribute_name = ldap_next_attribute(ldapConn, ldap_entry, ber);
	}
	ber_free(ber, 0);

	return var;
}

/*! \brief Get variables from ldap entry attributes - Should be locked before using it
 *
 * The results are freed outside this function so is the \a vars array.
 *	
 * \return \a vars - an array of ast_variable variables terminated with a null.
 **/
static struct ast_variable **realtime_ldap_result_to_vars(struct ldap_table_config *table_config,
	LDAPMessage *ldap_result_msg, unsigned int *entries_count_ptr)
{
	struct ast_variable **vars;
	int i = 0;
	int tot_count = 0;
	int entry_index = 0;
	LDAPMessage *ldap_entry = NULL;
	BerElement *ber = NULL;
	struct ast_variable *var = NULL;
	struct ast_variable *prev = NULL;
	int is_delimited = 0;
	char *delim_value = NULL;
	int delim_tot_count = 0;
	int delim_count = 0;

	/* First find the total count */
	ldap_entry = ldap_first_entry(ldapConn, ldap_result_msg);

	for (tot_count = 0; ldap_entry; tot_count++){ 
		tot_count += semicolon_count_var(realtime_ldap_entry_to_var(table_config, ldap_entry));
		ldap_entry = ldap_next_entry(ldapConn, ldap_entry);
	}

	if (entries_count_ptr)
		*entries_count_ptr = tot_count;
	/* Now that we have the total count we allocate space and create the variables
	 * Remember that each element in vars is a linked list that points to realtime variable.
	 * If the we are dealing with a static realtime variable we create a new element in the \a vars array for each delimited
	 * value in \a variable_value; otherwise, we keep \a vars static and increase the length of the linked list of variables in the array element.
	 * This memory must be freed outside of this function. */
	vars = ast_calloc(sizeof(struct ast_variable *), tot_count + 1);

	ldap_entry = ldap_first_entry(ldapConn, ldap_result_msg);

	i = 0;

	/* For each static realtime variable we may create several entries in the \a vars array if it's delimited */
	for (entry_index = 0; ldap_entry; ) { 
		int pos = 0;
		delim_value = NULL;
		delim_tot_count = 0;
		delim_count = 0;
		
		do { /* while delim_count */

			/* Starting new static var */
			char *ldap_attribute_name = ldap_first_attribute(ldapConn, ldap_entry, &ber);
			struct berval *value;
			while (ldap_attribute_name) {
			
				const char *attribute_name =
					convert_attribute_name_from_ldap(table_config, ldap_attribute_name);
				int is_realmed_password_attribute = strcasecmp(attribute_name, "md5secret") == 0;
				struct berval **values = NULL;

				values = ldap_get_values_len(ldapConn, ldap_entry, ldap_attribute_name);
				if (values) {
					struct berval **v;
					char *valptr;

					for (v = values; *v; v++) {
						value = *v;
						valptr = value->bv_val;
						if (is_realmed_password_attribute) {
							if (strncasecmp(valptr, "{md5}", 5) == 0) {
								valptr += 5;
							} else {
								valptr = NULL;
							}
							ast_debug(2, "md5: %s\n", valptr);
						}
						if (valptr) {
							if (delim_value == NULL 
								&& !is_realmed_password_attribute 
								&& (static_table_config != table_config || strcmp(attribute_name, "variable_value") == 0)) {

								delim_value = ast_strdup(valptr);

								if ((delim_tot_count = semicolon_count_str(delim_value)) > 0) {
									ast_debug(4, "LINE(%d) is delimited %d times: %s\n", __LINE__, delim_tot_count, delim_value);
									is_delimited = 1;
								}
							}

							if (is_delimited != 0 
								&& !is_realmed_password_attribute 
								&& (static_table_config != table_config || strcmp(attribute_name, "variable_value") == 0) ) {
								/* for non-Static RealTime, first */

								for (i = pos; !ast_strlen_zero(valptr + i); i++) {
									ast_debug(4, "LINE(%d) DELIM pos: %d i: %d\n", __LINE__, pos, i);
									if (delim_value[i] == ';') {
										delim_value[i] = '\0';

										ast_debug(2, "LINE(%d) DELIM - attribute_name: %s value: %s pos: %d\n", __LINE__, attribute_name, &delim_value[pos], pos);
							
										if (prev) {
											prev->next = ast_variable_new(attribute_name, &delim_value[pos], table_config->table_name);
											if (prev->next) {
												prev = prev->next;
											}
										} else {
											prev = var = ast_variable_new(attribute_name, &delim_value[pos], table_config->table_name);
										}
										pos = i + 1;

										if (static_table_config == table_config) {
											break;
										}
									}
								}
								if (ast_strlen_zero(valptr + i)) {
									ast_debug(4, "LINE(%d) DELIM pos: %d i: %d delim_count: %d\n", __LINE__, pos, i, delim_count);
									/* Last delimited value */
									ast_debug(4, "LINE(%d) DELIM - attribute_name: %s value: %s pos: %d\n", __LINE__, attribute_name, &delim_value[pos], pos);
									if (prev) {
										prev->next = ast_variable_new(attribute_name, &delim_value[pos], table_config->table_name);
										if (prev->next) {
											prev = prev->next;
										}
									} else {
										prev = var = ast_variable_new(attribute_name, &delim_value[pos], table_config->table_name);
									}
									/* Remembering to free memory */
									is_delimited = 0;
									pos = 0;
								}
								free(delim_value);
								delim_value = NULL;
								
								ast_debug(4, "LINE(%d) DELIM pos: %d i: %d\n", __LINE__, pos, i);
							} else {
								/* not delimited */
								if (delim_value) {
									free(delim_value);
									delim_value = NULL;
								}
								ast_debug(2, "LINE(%d) attribute_name: %s value: %s\n", __LINE__, attribute_name, valptr);

								if (prev) {
									prev->next = ast_variable_new(attribute_name, valptr, table_config->table_name);
									if (prev->next) {
										prev = prev->next;
									}
								} else {
									prev = var = ast_variable_new(attribute_name, valptr, table_config->table_name);
								}
							}
						}
					} /*!< for (v = values; *v; v++) */
					ldap_value_free_len(values);
				}/*!< if (values) */
				ldap_attribute_name = ldap_next_attribute(ldapConn, ldap_entry, ber);
			} /*!< while (ldap_attribute_name) */
			ber_free(ber, 0);
			if (static_table_config == table_config) {
				if (option_debug > 2) {
					const struct ast_variable *tmpdebug = variable_named(var, "variable_name");
					const struct ast_variable *tmpdebug2 = variable_named(var, "variable_value");
					if (tmpdebug && tmpdebug2) {
						ast_debug(3, "LINE(%d) Added to vars - %s = %s\n", __LINE__, tmpdebug->value, tmpdebug2->value);
					}
				}
				vars[entry_index++] = var;
				prev = NULL;
			}

			delim_count++;
		} while (delim_count <= delim_tot_count && static_table_config == table_config);

		if (static_table_config != table_config) {
			ast_debug(3, "LINE(%d) Added to vars - non static\n", __LINE__);
				
			vars[entry_index++] = var;
			prev = NULL;
		}
		ldap_entry = ldap_next_entry(ldapConn, ldap_entry);
	} /*!< end for loop over ldap_entry */

	return vars;
}


static int is_ldap_connect_error(int err)
{
	return (err == LDAP_SERVER_DOWN
			|| err == LDAP_TIMEOUT || err == LDAP_CONNECT_ERROR);
}

/*! \brief Get LDAP entry by dn and return attributes as variables  - Should be locked before using it 
	This is used for setting the default values of an object(i.e., with accountBaseDN)
*/
static struct ast_variable *ldap_loadentry(struct ldap_table_config *table_config,
					   const char *dn)
{
	if (!table_config) {
		ast_log(LOG_ERROR, "No table config\n");
		return NULL;
	} else {
		struct ast_variable **vars = NULL;
		struct ast_variable *var = NULL;
		int result = -1;
		LDAPMessage *ldap_result_msg = NULL;
		int tries = 0;

		ast_debug(2, "ldap_loadentry dn=%s\n", dn);

		do {
			result = ldap_search_ext_s(ldapConn, dn, LDAP_SCOPE_BASE,
					   "(objectclass=*)", NULL, 0, NULL, NULL, NULL, LDAP_NO_LIMIT, &ldap_result_msg);
			if (result != LDAP_SUCCESS && is_ldap_connect_error(result)) {
				ast_log(LOG_WARNING,
					"Failed to query database. Try %d/3\n",
					tries + 1);
				tries++;
				if (tries < 3) {
					usleep(500000L * tries);
					if (ldapConn) {
						ldap_unbind_ext_s(ldapConn, NULL, NULL);
						ldapConn = NULL;
					}
					if (!ldap_reconnect())
						break;
				}
			}
		} while (result != LDAP_SUCCESS && tries < 3 && is_ldap_connect_error(result));

		if (result != LDAP_SUCCESS) {
			ast_log(LOG_WARNING,
					"Failed to query database. Check debug for more info.\n");
			ast_debug(2, "dn=%s\n", dn);
			ast_debug(2, "Query Failed because: %s\n",
				ldap_err2string(result));
			ast_mutex_unlock(&ldap_lock);
			return NULL;
		} else {
			int num_entry = 0;
			unsigned int *entries_count_ptr = NULL; /*!< not using this */
			if ((num_entry = ldap_count_entries(ldapConn, ldap_result_msg)) > 0) {
				ast_debug(3, "num_entry: %d\n", num_entry);

				vars = realtime_ldap_result_to_vars(table_config, ldap_result_msg, entries_count_ptr);
				if (num_entry > 1)
					ast_log(LOG_NOTICE, "More than one entry for dn=%s. Take only 1st one\n", dn);
			} else {
				ast_debug(2, "Could not find any entry dn=%s.\n", dn);
			}
		}
		ldap_msgfree(ldap_result_msg);

		/* Chopping \a vars down to one variable */
		if (vars != NULL) {
			struct ast_variable **p = vars;
			p++;
			var = *p;
			while (var) {
				ast_variables_destroy(var);
				p++;
			}
			vars = ast_realloc(vars, sizeof(struct ast_variable *));
		}

		var = *vars;

		return var;
	}
}

/*! \brief caller should free returned pointer */
static char *substituted(struct ast_channel *channel, const char *string)
{
#define MAXRESULT	2048
	char *ret_string = NULL;

	if (!ast_strlen_zero(string)) {
		ret_string = ast_calloc(1, MAXRESULT);
		pbx_substitute_variables_helper(channel, string, ret_string, MAXRESULT - 1);
	}
	ast_debug(2, "substituted: string: '%s' => '%s' \n",
		string, ret_string);
	return ret_string;
}

/*! \brief caller should free returned pointer */
static char *cleaned_basedn(struct ast_channel *channel, const char *basedn)
{
	char *cbasedn = NULL;
	if (basedn) {
		char *p = NULL;
		cbasedn = substituted(channel, basedn);
		if (*cbasedn == '"') {
			cbasedn++;
			if (!ast_strlen_zero(cbasedn)) {
				int len = strlen(cbasedn);
				if (cbasedn[len - 1] == '"')
					cbasedn[len - 1] = '\0';

			}
		}
		p = cbasedn;
		while (*p) {
			if (*p == '|')
				*p = ',';
			p++;
		}
	}
	ast_debug(2, "basedn: '%s' => '%s' \n", basedn, cbasedn);
	return cbasedn;
}

/*! \brief Replace \<search\> by \<by\> in string. No check is done on string allocated size ! */
static int replace_string_in_string(char *string, const char *search, const char *by)
{
	int search_len = strlen(search);
	int by_len = strlen(by);
	int replaced = 0;
	char *p = strstr(string, search);
	if (p) {
		replaced = 1;
		while (p) {
			if (by_len == search_len)
				memcpy(p, by, by_len);
			else {
				memmove(p + by_len, p + search_len,
						strlen(p + search_len) + 1);
				memcpy(p, by, by_len);
			}
			p = strstr(p + by_len, search);
		}
	}
	return replaced;
}

/*! \brief Append a name=value filter string. The filter string can grow. */
static void append_var_and_value_to_filter(struct ast_str **filter,
	struct ldap_table_config *table_config,
	const char *name, const char *value)
{
	char *new_name = NULL;
	char *new_value = NULL;
	char *like_pos = strstr(name, " LIKE");

	ast_debug(2, "name='%s' value='%s'\n", name, value);

	if (like_pos) {
		int len = like_pos - name;
		name = new_name = ast_strdupa(name);
		new_name[len] = '\0';
		value = new_value = ast_strdupa(value);
		replace_string_in_string(new_value, "\\_", "_");
		replace_string_in_string(new_value, "%", "*");
	}

	name = convert_attribute_name_to_ldap(table_config, name);

	ast_str_append(filter, 0, "(%s=%s)", name, value);
}

/*! \brief LDAP base function 
 * \return a null terminated array of ast_variable (one per entry) or NULL if no entry is found or if an error occured
 * caller should free the returned array and ast_variables
 * \param entries_count_ptr is a pointer to found entries count (can be NULL)
 * \param basedn is the base DN
 * \param table_name is the table_name (used dor attribute convertion and additional filter)
 * \param ap contains null terminated list of pairs name/value
*/
static struct ast_variable **realtime_ldap_base_ap(unsigned int *entries_count_ptr,
	const char *basedn, const char *table_name, va_list ap)
{
	struct ast_variable **vars = NULL;
	const char *newparam = NULL;
	const char *newval = NULL;
	struct ldap_table_config *table_config = NULL;
	char *clean_basedn = cleaned_basedn(NULL, basedn);
	struct ast_str *filter = NULL;
	int tries = 0;
	int result = 0;
	LDAPMessage *ldap_result_msg = NULL;

	if (!table_name) {
		ast_log(LOG_WARNING, "No table_name specified.\n");
		ast_free(clean_basedn);
		return NULL;
	} 

	if (!(filter = ast_str_create(80))) {
		ast_free(clean_basedn);
		return NULL;
	}

	/* Get the first parameter and first value in our list of passed paramater/value pairs  */
	newparam = va_arg(ap, const char *);
	newval = va_arg(ap, const char *);

	if (!newparam || !newval) {
		ast_log(LOG_WARNING, "Realtime retrieval requires at least 1 parameter"
			" and 1 value to search on.\n");
		ast_free(filter);
		ast_free(clean_basedn);
		return NULL;
	}

	ast_mutex_lock(&ldap_lock);

	/* We now have our complete statement; Lets connect to the server and execute it.  */
	if (!ldap_reconnect()) {
		ast_mutex_unlock(&ldap_lock);
		ast_free(filter);
		ast_free(clean_basedn);
		return NULL;
	}

	table_config = table_config_for_table_name(table_name);
	if (!table_config) {
		ast_log(LOG_WARNING, "No table named '%s'.\n", table_name);
		ast_mutex_unlock(&ldap_lock);
		ast_free(filter);
		ast_free(clean_basedn);
		return NULL;
	}

	ast_str_append(&filter, 0, "(&");

	if (table_config && table_config->additional_filter)
		ast_str_append(&filter, 0, "%s", table_config->additional_filter);
	if (table_config != base_table_config && base_table_config && 
		base_table_config->additional_filter) {
		ast_str_append(&filter, 0, "%s", base_table_config->additional_filter);
	}

	/* Create the first part of the query using the first parameter/value pairs we just extracted */
	/*   If there is only 1 set, then we have our query. Otherwise, loop thru the list and concat */

	append_var_and_value_to_filter(&filter, table_config, newparam, newval);
	while ((newparam = va_arg(ap, const char *))) {
		newval = va_arg(ap, const char *);
		append_var_and_value_to_filter(&filter, table_config, newparam, newval);
	}
	ast_str_append(&filter, 0, ")");

	do {
		/* freeing ldap_result further down */
		result = ldap_search_ext_s(ldapConn, clean_basedn,
				  LDAP_SCOPE_SUBTREE, ast_str_buffer(filter), NULL, 0, NULL, NULL, NULL, LDAP_NO_LIMIT,
				  &ldap_result_msg);
		if (result != LDAP_SUCCESS && is_ldap_connect_error(result)) {
			ast_log(LOG_DEBUG, "Failed to query database. Try %d/10\n",
				tries + 1);
			if (++tries < 10) {
				usleep(1);
				if (ldapConn) {
					ldap_unbind_ext_s(ldapConn, NULL, NULL);
					ldapConn = NULL;
				}
				if (!ldap_reconnect())
					break;
			}
		}
	} while (result != LDAP_SUCCESS && tries < 10 && is_ldap_connect_error(result));

	if (result != LDAP_SUCCESS) {
		ast_log(LOG_WARNING, "Failed to query database. Check debug for more info.\n");
		ast_log(LOG_WARNING, "Query: %s\n", ast_str_buffer(filter));
		ast_log(LOG_WARNING, "Query Failed because: %s\n", ldap_err2string(result));
	} else {
		/* this is where we create the variables from the search result 
		 * freeing this \a vars outside this function */
		if (ldap_count_entries(ldapConn, ldap_result_msg) > 0) {
			/* is this a static var or some other? they are handled different for delimited values */
			vars = realtime_ldap_result_to_vars(table_config, ldap_result_msg, entries_count_ptr);
		} else {
			ast_debug(1, "Could not find any entry matching %s in base dn %s.\n",
				ast_str_buffer(filter), clean_basedn);
		}

		ldap_msgfree(ldap_result_msg);

		/* TODO: get the default variables from the accountBaseDN, not implemented with delimited values */
		if (vars) {
			struct ast_variable **p = vars;
			while (*p) {
				struct ast_variable *append_var = NULL;
				struct ast_variable *tmp = *p;
				while (tmp) {
					if (strcasecmp(tmp->name, "accountBaseDN") == 0) {
						/* Get the variable to compare with for the defaults */
						struct ast_variable *base_var = ldap_loadentry(table_config, tmp->value);
						
						while (base_var) {
							struct ast_variable *next = base_var->next;
							struct ast_variable *test_var = *p;
							int base_var_found = 0;

							/* run throught the default values and fill it inn if it is missing */
							while (test_var) {
								if (strcasecmp(test_var->name, base_var->name) == 0) {
									base_var_found = 1;
									break;
								} else
									test_var = test_var->next;
							}
							if (base_var_found) {
								base_var->next = NULL;
								ast_variables_destroy(base_var);
								base_var = next;
							} else {
								if (append_var)
									base_var->next = append_var;
								else
									base_var->next = NULL;
								append_var = base_var;
								base_var = next;
							}
						}
					}
					if (!tmp->next && append_var) {
						tmp->next = append_var;
						tmp = NULL;
					} else
						tmp = tmp->next;
				}
				p++;
			}
		}
	}

	if (filter)
		ast_free(filter);

	if (clean_basedn)
		ast_free(clean_basedn);

	ast_mutex_unlock(&ldap_lock);

	return vars;
}

/*! \brief same as realtime_ldap_base_ap but take variable arguments count list */
static struct ast_variable **realtime_ldap_base(unsigned int *entries_count_ptr,
	const char *basedn, const char *table_name, ...)
{
	struct ast_variable **vars = NULL;
	va_list ap;

	va_start(ap, table_name);
	vars = realtime_ldap_base_ap(entries_count_ptr, basedn, table_name, ap);
	va_end(ap);

	return vars;
}

/*! \brief See Asterisk doc
*
* For Realtime Dynamic(i.e., switch, queues, and directory) -- I think
*/
static struct ast_variable *realtime_ldap(const char *basedn,
					  const char *table_name, va_list ap)
{
	struct ast_variable **vars = realtime_ldap_base_ap(NULL, basedn, table_name, ap);
	struct ast_variable *var = NULL;

	if (vars) {
		struct ast_variable *last_var = NULL;
		struct ast_variable **p = vars;
		while (*p) {
			if (last_var) {
				while (last_var->next)
					last_var = last_var->next;
				last_var->next = *p;
			} else {
				var = *p;
				last_var = var;
			}
			p++;
		}
		free(vars);
	}
	return var;
}

/*! \brief See Asterisk doc
*
* this function will be called for the switch statment if no match is found with the realtime_ldap function(i.e. it is a failover);
* however, the ast_load_realtime wil match on wildcharacters also depending on what the mode is set to
* this is an area of asterisk that could do with a lot of modification
* I think this function returns Realtime dynamic objects
*/
static struct ast_config *realtime_multi_ldap(const char *basedn,
      const char *table_name, va_list ap)
{
	struct ast_variable **vars =
		realtime_ldap_base_ap(NULL, basedn, table_name, ap);
	struct ast_config *cfg = NULL;

	if (vars) {
		cfg = ast_config_new();
		if (!cfg) {
			ast_log(LOG_ERROR, "Unable to create a config!\n");
		} else {
			struct ast_variable **p = vars;

			while (*p) {
				struct ast_category *cat = NULL;
				cat = ast_category_new("", table_name, -1);
				if (!cat) {
					ast_log(LOG_ERROR, "Unable to create a new category!\n");
					break;
				} else {
					struct ast_variable *var = *p;
					while (var) {
						struct ast_variable *next = var->next;
						var->next = NULL;
						ast_variable_append(cat, var);
						var = next;
					}
				}
				ast_category_append(cfg, cat);
				p++;
			}
		}
		free(vars);
	}
	return cfg;

}

/*! 
 * \brief Sorting alogrithm for qsort to find the order of the variables \a a and \a b
 * \param a pointer to category_and_metric struct
 * \param b pointer to category_and_metric struct
 *
 * \retval -1 for if b is greater
 * \retval 0 zero for equal
 * \retval 1 if a is greater
 */
static int compare_categories(const void *a, const void *b)
{
	const struct category_and_metric *as = a;
	const struct category_and_metric *bs = b;

	if (as->metric < bs->metric)
		return -1;
	else if (as->metric > bs->metric)
		return 1;
	else if (as->metric == bs->metric && strcmp(as->name, bs->name) != 0)
		return strcmp(as->name, bs->name);

	/* if the metric and the category name is the same, we check the variable metric */
	if (as->var_metric < bs->var_metric)
		return -1;
	else if (as->var_metric > bs->var_metric)
		return 1;

	return 0;
}

/*! \brief See Asterisk doc
 *
*	This is for Static Realtime (again: I think...)
*	
*	load the configuration stuff for the .conf files
*	called on a reload
*/
static struct ast_config *config_ldap(const char *basedn, const char *table_name,
	const char *file, struct ast_config *cfg, struct ast_flags config_flags, const char *sugg_incl, const char *who_asked)
{
	unsigned int vars_count = 0;
	struct ast_variable **vars;
	int i = 0;
	struct ast_variable *new_v = NULL;
	struct ast_category *cur_cat = NULL;
	const char *last_category = NULL;
	int last_category_metric = 0;
	struct category_and_metric *categories;
	struct ast_variable **p;

	if (ast_strlen_zero(file) || !strcasecmp(file, RES_CONFIG_LDAP_CONF)) {
		ast_log(LOG_ERROR, "Cannot configure myself.\n");
		return NULL;
	}

	vars = realtime_ldap_base(&vars_count, basedn, table_name, "filename",
				file, "commented", "FALSE", NULL);

	if (!vars) {
		ast_log(LOG_WARNING, "Could not find config '%s' in database.\n", file);
		return NULL;
	}

	/*!\note Since the items come back in random order, they need to be sorted
	 * first, and since the data could easily exceed stack size, this is
	 * allocated from the heap.
	 */
	if (!(categories = ast_calloc(sizeof(*categories), vars_count)))
		return NULL;

	for (vars_count = 0, p = vars; *p; p++) {
		struct ast_variable *category = variable_named(*p, "category");
		struct ast_variable *cat_metric = variable_named(*p, "cat_metric");
		struct ast_variable *var_name = variable_named(*p, "variable_name");
		struct ast_variable *var_val = variable_named(*p, "variable_value");
		struct ast_variable *var_metric = variable_named(*p, "var_metric");
		struct ast_variable *dn = variable_named(*p, "dn");
			
		ast_debug(1, "category: %s\n", category->value);
		ast_debug(1, "var_name: %s\n", var_name->value);
		ast_debug(1, "var_val: %s\n", var_val->value);
		ast_debug(1, "cat_metric: %s\n", cat_metric->value);

		if (!category) {
			ast_log(LOG_ERROR,
					"No category name in entry '%s'  for file '%s'.\n",
					(dn ? dn->value : "?"), file);
		} else if (!cat_metric) {
			ast_log(LOG_ERROR,
					"No category metric in entry '%s'(category: %s) for file '%s'.\n",
					(dn ? dn->value : "?"), category->value, file);
		} else if (!var_metric) {
			ast_log(LOG_ERROR,
					"No variable metric in entry '%s'(category: %s) for file '%s'.\n",
					(dn ? dn->value : "?"), category->value, file);
		} else if (!var_name) {
			ast_log(LOG_ERROR,
					"No variable name in entry '%s' (category: %s metric: %s) for file '%s'.\n",
					(dn ? dn->value : "?"), category->value,
					cat_metric->value, file);
		} else if (!var_val) {
			ast_log(LOG_ERROR,
					"No variable value in entry '%s' (category: %s metric: %s variable: %s) for file '%s'.\n",
					(dn ? dn->value : "?"), category->value,
					cat_metric->value, var_name->value, file);
		} else {
			categories[vars_count].name = category->value;
			categories[vars_count].metric = atoi(cat_metric->value);
			categories[vars_count].variable_name = var_name->value;
			categories[vars_count].variable_value = var_val->value;
			categories[vars_count].var_metric = atoi(var_metric->value);
			vars_count++;
		}
	}

	qsort(categories, vars_count, sizeof(*categories), compare_categories);

	for (i = 0; i < vars_count; i++) {
		if (!strcmp(categories[i].variable_name, "#include")) {
			struct ast_flags flags = { 0 };
			if (!ast_config_internal_load(categories[i].variable_value, cfg, flags, "", who_asked))
				break;
			continue;
		}

		if (!last_category || strcmp(last_category, categories[i].name) ||
			last_category_metric != categories[i].metric) {
			cur_cat = ast_category_new(categories[i].name, table_name, -1);
			if (!cur_cat)
				break;
			last_category = categories[i].name;
			last_category_metric = categories[i].metric;
			ast_category_append(cfg, cur_cat);
		}

		if (!(new_v = ast_variable_new(categories[i].variable_name, categories[i].variable_value, table_name)))
			break;

		ast_variable_append(cur_cat, new_v);
	}

	free(vars);
	free(categories);

	return cfg;
}

/* \brief Function to update a set of values in ldap
static
*/
static int update_ldap(const char *basedn, const char *table_name, const char *attribute,
	const char *lookup, va_list ap)
{
	int error = 0;
	LDAPMessage *ldap_entry = NULL;
	LDAPMod **ldap_mods;
	const char *newparam = NULL;
	const char *newval = NULL;
	char *dn;
	int num_entries = 0;
	int i = 0;
	int mods_size = 0;
	int mod_exists = 0;
	struct ldap_table_config *table_config = NULL;
	char *clean_basedn = NULL;
	struct ast_str *filter = NULL;
	int tries = 0;
	int result = 0;
	LDAPMessage *ldap_result_msg = NULL;

	if (!table_name) {
		ast_log(LOG_WARNING, "No table_name specified.\n");
		return -1;
	} 

	if (!(filter = ast_str_create(80)))
		return -1;

	if (!attribute || !lookup) {
		ast_log(LOG_WARNING,
				"LINE(%d): search parameters are empty.\n", __LINE__);
		return -1;
	}
	ast_mutex_lock(&ldap_lock);

	/* We now have our complete statement; Lets connect to the server and execute it.  */
	if (!ldap_reconnect()) {
		ast_mutex_unlock(&ldap_lock);
		return -1;
	}

	table_config = table_config_for_table_name(table_name);
	if (!table_config) {
		ast_log(LOG_WARNING, "No table named '%s'.\n", table_name);
		ast_mutex_unlock(&ldap_lock);
		return -1;
	}

	clean_basedn = cleaned_basedn(NULL, basedn);

	/* Create the filter with the table additional filter and the parameter/value pairs we were given */
	ast_str_append(&filter, 0, "(&");
	if (table_config && table_config->additional_filter) {
		ast_str_append(&filter, 0, "%s", table_config->additional_filter);
	}
	if (table_config != base_table_config && base_table_config
		&& base_table_config->additional_filter) {
		ast_str_append(&filter, 0, "%s", base_table_config->additional_filter);
	}
	append_var_and_value_to_filter(&filter, table_config, attribute, lookup);
	ast_str_append(&filter, 0, ")");

	/* Create the modification array with the parameter/value pairs we were given, 
	 * if there are several parameters with the same name, we collect them into 
	 * one parameter/value pair and delimit them with a semicolon */
	newparam = va_arg(ap, const char *);
	newparam = convert_attribute_name_to_ldap(table_config, newparam);
	newval = va_arg(ap, const char *);
	if (!newparam || !newval) {
		ast_log(LOG_WARNING,
				"LINE(%d): need at least one parameter to modify.\n", __LINE__);
		return -1;
	}

	mods_size = 2; /* one for the first param/value pair and one for the the terminating NULL */
	ldap_mods = ast_calloc(sizeof(LDAPMod *), mods_size);
	ldap_mods[0] = ast_calloc(1, sizeof(LDAPMod));

	ldap_mods[0]->mod_op = LDAP_MOD_REPLACE;
	ldap_mods[0]->mod_type = ast_calloc(sizeof(char), strlen(newparam) + 1);
	strcpy(ldap_mods[0]->mod_type, newparam);

	ldap_mods[0]->mod_values = ast_calloc(sizeof(char), 2);
	ldap_mods[0]->mod_values[0] = ast_calloc(sizeof(char), strlen(newval) + 1);
	strcpy(ldap_mods[0]->mod_values[0], newval);

	while ((newparam = va_arg(ap, const char *))) {
		newparam = convert_attribute_name_to_ldap(table_config, newparam);
		newval = va_arg(ap, const char *);
		mod_exists = 0;

		for (i = 0; i < mods_size - 1; i++) {
			if (ldap_mods[i]&& !strcmp(ldap_mods[i]->mod_type, newparam)) {
				/* We have the parameter allready, adding the value as a semicolon delimited value */
				ldap_mods[i]->mod_values[0] = ast_realloc(ldap_mods[i]->mod_values[0], sizeof(char) * (strlen(ldap_mods[i]->mod_values[0]) + strlen(newval) + 2));
				strcat(ldap_mods[i]->mod_values[0], ";");
				strcat(ldap_mods[i]->mod_values[0], newval);
				mod_exists = 1;	
				break;
			}
		}

		/* create new mod */
		if (!mod_exists) {
			mods_size++;
			ldap_mods = ast_realloc(ldap_mods, sizeof(LDAPMod *) * mods_size);
			ldap_mods[mods_size - 1] = NULL;
			
			ldap_mods[mods_size - 2] = ast_calloc(1, sizeof(LDAPMod));

			ldap_mods[mods_size - 2]->mod_type = ast_calloc(sizeof(char), strlen(newparam) + 1);
			strcpy(ldap_mods[mods_size - 2]->mod_type, newparam);

			if (strlen(newval) == 0) {
				ldap_mods[mods_size - 2]->mod_op = LDAP_MOD_DELETE;
			} else {
				ldap_mods[mods_size - 2]->mod_op = LDAP_MOD_REPLACE;

				ldap_mods[mods_size - 2]->mod_values = ast_calloc(sizeof(char *), 2);
				ldap_mods[mods_size - 2]->mod_values[0] = ast_calloc(sizeof(char), strlen(newval) + 1);
				strcpy(ldap_mods[mods_size - 2]->mod_values[0], newval);
			}
		}
	}
	/* freeing ldap_mods further down */

	do {
		/* freeing ldap_result further down */
		result = ldap_search_ext_s(ldapConn, clean_basedn,
				  LDAP_SCOPE_SUBTREE, ast_str_buffer(filter), NULL, 0, NULL, NULL, NULL, LDAP_NO_LIMIT,
				  &ldap_result_msg);
		if (result != LDAP_SUCCESS && is_ldap_connect_error(result)) {
			ast_log(LOG_WARNING, "Failed to query database. Try %d/3\n",
				tries + 1);
			tries++;
			if (tries < 3) {
				usleep(500000L * tries);
				if (ldapConn) {
					ldap_unbind_ext_s(ldapConn, NULL, NULL);
					ldapConn = NULL;
				}
				if (!ldap_reconnect())
					break;
			}
		}
	} while (result != LDAP_SUCCESS && tries < 3 && is_ldap_connect_error(result));

	if (result != LDAP_SUCCESS) {
		ast_log(LOG_WARNING, "Failed to query directory. Check debug for more info.\n");
		ast_log(LOG_WARNING, "Query: %s\n", ast_str_buffer(filter));
		ast_log(LOG_WARNING, "Query Failed because: %s\n",
			ldap_err2string(result));

		ast_mutex_unlock(&ldap_lock);
		free(filter);
		free(clean_basedn);
		ldap_msgfree(ldap_result_msg);
		ldap_mods_free(ldap_mods, 0);
		return -1;
	}
	/* Ready to update */
	if ((num_entries = ldap_count_entries(ldapConn, ldap_result_msg)) > 0) {
		ast_debug(3, "LINE(%d) Modifying %s=%s hits: %d\n", __LINE__, attribute, lookup, num_entries);
		for (i = 0; option_debug > 2 && i < mods_size - 1; i++) {
			if (ldap_mods[i]->mod_op != LDAP_MOD_DELETE) {
				ast_debug(3, "LINE(%d) %s=%s \n", __LINE__, ldap_mods[i]->mod_type, ldap_mods[i]->mod_values[0]);
			} else {
				ast_debug(3, "LINE(%d) deleting %s \n", __LINE__, ldap_mods[i]->mod_type);
			}
		}
		ldap_entry = ldap_first_entry(ldapConn, ldap_result_msg);

		for (i = 0; ldap_entry; i++) { 
			dn = ldap_get_dn(ldapConn, ldap_entry);
			if ((error = ldap_modify_ext_s(ldapConn, dn, ldap_mods, NULL, NULL)) != LDAP_SUCCESS) 
				ast_log(LOG_ERROR, "Couldn't modify dn:%s because %s", dn, ldap_err2string(error));

			ldap_entry = ldap_next_entry(ldapConn, ldap_entry);
		}
	}

	ast_mutex_unlock(&ldap_lock);
	free(filter);
	free(clean_basedn);
	ldap_msgfree(ldap_result_msg);
	ldap_mods_free(ldap_mods, 0);
	return num_entries;
}

static int update2_ldap(const char *basedn, const char *table_name, va_list ap)
{
	int error = 0;
	LDAPMessage *ldap_entry = NULL;
	LDAPMod **ldap_mods;
	const char *newparam = NULL;
	const char *newval = NULL;
	char *dn;
	int num_entries = 0;
	int i = 0;
	int mods_size = 0;
	int mod_exists = 0;
	struct ldap_table_config *table_config = NULL;
	char *clean_basedn = NULL;
	struct ast_str *filter = NULL;
	int tries = 0;
	int result = 0;
	LDAPMessage *ldap_result_msg = NULL;

	if (!table_name) {
		ast_log(LOG_WARNING, "No table_name specified.\n");
		return -1;
	} 

	if (!(filter = ast_str_create(80)))
		return -1;

	ast_mutex_lock(&ldap_lock);

	/* We now have our complete statement; Lets connect to the server and execute it.  */
	if (!ldap_reconnect()) {
		ast_mutex_unlock(&ldap_lock);
		ast_free(filter);
		return -1;
	}

	table_config = table_config_for_table_name(table_name);
	if (!table_config) {
		ast_log(LOG_WARNING, "No table named '%s'.\n", table_name);
		ast_mutex_unlock(&ldap_lock);
		ast_free(filter);
		return -1;
	}

	clean_basedn = cleaned_basedn(NULL, basedn);

	/* Create the filter with the table additional filter and the parameter/value pairs we were given */
	ast_str_append(&filter, 0, "(&");
	if (table_config && table_config->additional_filter) {
		ast_str_append(&filter, 0, "%s", table_config->additional_filter);
	}
	if (table_config != base_table_config && base_table_config
		&& base_table_config->additional_filter) {
		ast_str_append(&filter, 0, "%s", base_table_config->additional_filter);
	}

	/* Get multiple lookup keyfields and values */
	while ((newparam = va_arg(ap, const char *))) {
		newval = va_arg(ap, const char *);
		append_var_and_value_to_filter(&filter, table_config, newparam, newval);
	}
	ast_str_append(&filter, 0, ")");

	/* Create the modification array with the parameter/value pairs we were given, 
	 * if there are several parameters with the same name, we collect them into 
	 * one parameter/value pair and delimit them with a semicolon */
	newparam = va_arg(ap, const char *);
	newparam = convert_attribute_name_to_ldap(table_config, newparam);
	newval = va_arg(ap, const char *);
	if (!newparam || !newval) {
		ast_log(LOG_WARNING,
				"LINE(%d): need at least one parameter to modify.\n", __LINE__);
		ast_free(filter);
		ast_free(clean_basedn);
		return -1;
	}

	mods_size = 2; /* one for the first param/value pair and one for the the terminating NULL */
	ldap_mods = ast_calloc(sizeof(LDAPMod *), mods_size);
	ldap_mods[0] = ast_calloc(1, sizeof(LDAPMod));

	ldap_mods[0]->mod_op = LDAP_MOD_REPLACE;
	ldap_mods[0]->mod_type = ast_calloc(sizeof(char), strlen(newparam) + 1);
	strcpy(ldap_mods[0]->mod_type, newparam);

	ldap_mods[0]->mod_values = ast_calloc(sizeof(char), 2);
	ldap_mods[0]->mod_values[0] = ast_calloc(sizeof(char), strlen(newval) + 1);
	strcpy(ldap_mods[0]->mod_values[0], newval);

	while ((newparam = va_arg(ap, const char *))) {
		newparam = convert_attribute_name_to_ldap(table_config, newparam);
		newval = va_arg(ap, const char *);
		mod_exists = 0;

		for (i = 0; i < mods_size - 1; i++) {
			if (ldap_mods[i]&& !strcmp(ldap_mods[i]->mod_type, newparam)) {
				/* We have the parameter allready, adding the value as a semicolon delimited value */
				ldap_mods[i]->mod_values[0] = ast_realloc(ldap_mods[i]->mod_values[0], sizeof(char) * (strlen(ldap_mods[i]->mod_values[0]) + strlen(newval) + 2));
				strcat(ldap_mods[i]->mod_values[0], ";");
				strcat(ldap_mods[i]->mod_values[0], newval);
				mod_exists = 1;	
				break;
			}
		}

		/* create new mod */
		if (!mod_exists) {
			mods_size++;
			ldap_mods = ast_realloc(ldap_mods, sizeof(LDAPMod *) * mods_size);
			ldap_mods[mods_size - 1] = NULL;
			ldap_mods[mods_size - 2] = ast_calloc(1, sizeof(LDAPMod));

			ldap_mods[mods_size - 2]->mod_op = LDAP_MOD_REPLACE;

			ldap_mods[mods_size - 2]->mod_type = ast_calloc(sizeof(char), strlen(newparam) + 1);
			strcpy(ldap_mods[mods_size - 2]->mod_type, newparam);

			ldap_mods[mods_size - 2]->mod_values = ast_calloc(sizeof(char *), 2);
			ldap_mods[mods_size - 2]->mod_values[0] = ast_calloc(sizeof(char), strlen(newval) + 1);
			strcpy(ldap_mods[mods_size - 2]->mod_values[0], newval);
		}
	}
	/* freeing ldap_mods further down */

	do {
		/* freeing ldap_result further down */
		result = ldap_search_ext_s(ldapConn, clean_basedn,
				  LDAP_SCOPE_SUBTREE, ast_str_buffer(filter), NULL, 0, NULL, NULL, NULL, LDAP_NO_LIMIT,
				  &ldap_result_msg);
		if (result != LDAP_SUCCESS && is_ldap_connect_error(result)) {
			ast_log(LOG_WARNING, "Failed to query database. Try %d/3\n",
				tries + 1);
			tries++;
			if (tries < 3) {
				usleep(500000L * tries);
				if (ldapConn) {
					ldap_unbind_ext_s(ldapConn, NULL, NULL);
					ldapConn = NULL;
				}
				if (!ldap_reconnect())
					break;
			}
		}
	} while (result != LDAP_SUCCESS && tries < 3 && is_ldap_connect_error(result));

	if (result != LDAP_SUCCESS) {
		ast_log(LOG_WARNING, "Failed to query directory. Check debug for more info.\n");
		ast_log(LOG_WARNING, "Query: %s\n", ast_str_buffer(filter));
		ast_log(LOG_WARNING, "Query Failed because: %s\n",
			ldap_err2string(result));

		ast_mutex_unlock(&ldap_lock);
		free(filter);
		free(clean_basedn);
		ldap_msgfree(ldap_result_msg);
		ldap_mods_free(ldap_mods, 0);
		return -1;
	}
	/* Ready to update */
	if ((num_entries = ldap_count_entries(ldapConn, ldap_result_msg)) > 0) {
		for (i = 0; option_debug > 2 && i < mods_size - 1; i++)
			ast_debug(3, "LINE(%d) %s=%s \n", __LINE__, ldap_mods[i]->mod_type, ldap_mods[i]->mod_values[0]);

		ldap_entry = ldap_first_entry(ldapConn, ldap_result_msg);

		for (i = 0; ldap_entry; i++) { 
			dn = ldap_get_dn(ldapConn, ldap_entry);
			if ((error = ldap_modify_ext_s(ldapConn, dn, ldap_mods, NULL, NULL)) != LDAP_SUCCESS) 
				ast_log(LOG_ERROR, "Couldn't modify dn:%s because %s", dn, ldap_err2string(error));

			ldap_entry = ldap_next_entry(ldapConn, ldap_entry);
		}
	}

	ast_mutex_unlock(&ldap_lock);
	if (filter)
		free(filter);
	if (clean_basedn)
		free(clean_basedn);
	ldap_msgfree(ldap_result_msg);
	ldap_mods_free(ldap_mods, 0);
	return num_entries;
}

static struct ast_config_engine ldap_engine = {
	.name = "ldap",
	.load_func = config_ldap,
	.realtime_func = realtime_ldap,
	.realtime_multi_func = realtime_multi_ldap,
	.update_func = update_ldap,
	.update2_func = update2_ldap,
};

static int load_module(void)
{
	if (parse_config() < 0) {
		ast_log(LOG_NOTICE, "Cannot load LDAP RealTime driver.\n");
		return 0;
	}

	ast_mutex_lock(&ldap_lock);

	if (!ldap_reconnect()) 
		ast_log(LOG_WARNING, "Couldn't establish connection. Check debug.\n");

	ast_config_engine_register(&ldap_engine);
	ast_verb(1, "LDAP RealTime driver loaded.\n");
	ast_cli_register_multiple(ldap_cli, ARRAY_LEN(ldap_cli));

	ast_mutex_unlock(&ldap_lock);

	return 0;
}

static int unload_module(void)
{
	/* Aquire control before doing anything to the module itself. */
	ast_mutex_lock(&ldap_lock);

	table_configs_free();

	if (ldapConn) {
		ldap_unbind_ext_s(ldapConn, NULL, NULL);
		ldapConn = NULL;
	}
	ast_cli_unregister_multiple(ldap_cli, ARRAY_LEN(ldap_cli));
	ast_config_engine_deregister(&ldap_engine);
	ast_verb(1, "LDAP RealTime unloaded.\n");

	/* Unlock so something else can destroy the lock. */
	ast_mutex_unlock(&ldap_lock);

	return 0;
}

static int reload(void)
{
	/* Aquire control before doing anything to the module itself. */
	ast_mutex_lock(&ldap_lock);

	if (ldapConn) {
		ldap_unbind_ext_s(ldapConn, NULL, NULL);
		ldapConn = NULL;
	}

	if (parse_config() < 0) {
		ast_log(LOG_NOTICE, "Cannot reload LDAP RealTime driver.\n");
		ast_mutex_unlock(&ldap_lock);
		return 0;
	}		

	if (!ldap_reconnect()) 
		ast_log(LOG_WARNING, "Couldn't establish connection. Check debug.\n");

	ast_verb(2, "LDAP RealTime reloaded.\n");

	/* Done reloading. Release lock so others can now use driver. */
	ast_mutex_unlock(&ldap_lock);

	return 0;
}

int parse_config(void)
{
	struct ast_config *config;
	struct ast_flags config_flags = {0};
	const char *s, *host;
	int port;
	char *category_name = NULL;

	config = ast_config_load(RES_CONFIG_LDAP_CONF, config_flags);
	if (config == CONFIG_STATUS_FILEMISSING || config == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_WARNING, "Cannot load configuration %s\n", RES_CONFIG_LDAP_CONF);
		return -1;
	}

	if (!(s = ast_variable_retrieve(config, "_general", "user"))) {
		ast_log(LOG_WARNING, "No directory user found, anonymous binding as default.\n");
		user[0] = '\0';
	} else 
		ast_copy_string(user, s, sizeof(user));

	if (!ast_strlen_zero(user)) {
		if (!(s = ast_variable_retrieve(config, "_general", "pass"))) {
			ast_log(LOG_WARNING, "No directory password found, using 'asterisk' as default.\n");
			ast_copy_string(pass, "asterisk", sizeof(pass));
		} else {
			ast_copy_string(pass, s, sizeof(pass));
		}
	}

	/* URL is preferred, use host and port if not found */
	if ((s = ast_variable_retrieve(config, "_general", "url"))) {
		ast_copy_string(url, s, sizeof(url));
	} else if ((host = ast_variable_retrieve(config, "_general", "host"))) {
		if (!(s = ast_variable_retrieve(config, "_general", "port")) || sscanf(s, "%5d", &port) != 1 || port > 65535) {
			ast_log(LOG_NOTICE, "No directory port found, using 389 as default.\n");
			port = 389;
		}

		snprintf(url, sizeof(url), "ldap://%s:%d", host, port);
	} else {
		ast_log(LOG_ERROR, "No directory URL or host found.\n");
		ast_config_destroy(config);
		return -1;
	}

	if (!(s = ast_variable_retrieve(config, "_general", "basedn"))) {
		ast_log(LOG_ERROR, "No LDAP base dn found, using '%s' as default.\n", RES_CONFIG_LDAP_DEFAULT_BASEDN);
		ast_copy_string(base_distinguished_name, RES_CONFIG_LDAP_DEFAULT_BASEDN, sizeof(base_distinguished_name));
	} else 
		ast_copy_string(base_distinguished_name, s, sizeof(base_distinguished_name));

	if (!(s = ast_variable_retrieve(config, "_general", "version")) && !(s = ast_variable_retrieve(config, "_general", "protocol"))) {
		ast_log(LOG_NOTICE, "No explicit LDAP version found, using 3 as default.\n");
		version = 3;
	} else if (sscanf(s, "%30d", &version) != 1 || version < 1 || version > 6) {
		ast_log(LOG_WARNING, "Invalid LDAP version '%s', using 3 as default.\n", s);
		version = 3;
	}

	table_configs_free();

	while ((category_name = ast_category_browse(config, category_name))) {
		int is_general = (strcasecmp(category_name, "_general") == 0);
		int is_config = (strcasecmp(category_name, "config") == 0); /*!< using the [config] context for Static RealTime */
		struct ast_variable *var = ast_variable_browse(config, category_name);
		
		if (var) {
			struct ldap_table_config *table_config =
				table_config_for_table_name(category_name);
			if (!table_config) {
				table_config = table_config_new(category_name);
				AST_LIST_INSERT_HEAD(&table_configs, table_config, entry);
				if (is_general)
					base_table_config = table_config;
				if (is_config)
					static_table_config = table_config;
			}
			for (; var; var = var->next) {
				if (!strcasecmp(var->name, "additionalFilter")) {
					table_config->additional_filter = ast_strdup(var->value);
				} else {
					ldap_table_config_add_attribute(table_config, var->name, var->value);
				}
			}
		}
	}

	ast_config_destroy(config);

	return 1;
}

/*! \note ldap_lock should have been locked before calling this function. */
static int ldap_reconnect(void)
{
	int bind_result = 0;
	struct berval cred;

	if (ldapConn) {
		ast_debug(2, "Everything seems fine.\n");
		return 1;
	}

	if (ast_strlen_zero(url)) {
		ast_log(LOG_ERROR, "Not enough parameters to connect to ldap database\n");
		return 0;
	}

	if (LDAP_SUCCESS != ldap_initialize(&ldapConn, url)) {
		ast_log(LOG_ERROR, "Failed to init ldap connection to '%s'. Check debug for more info.\n", url);
		return 0;
	}

	if (LDAP_OPT_SUCCESS != ldap_set_option(ldapConn, LDAP_OPT_PROTOCOL_VERSION, &version)) {
		ast_log(LOG_WARNING, "Unable to set LDAP protocol version to %d, falling back to default.\n", version);
	}

	if (!ast_strlen_zero(user)) {
		ast_debug(2, "bind to '%s' as user '%s'\n", url, user);
		cred.bv_val = (char *) pass;
		cred.bv_len = strlen(pass);
		bind_result = ldap_sasl_bind_s(ldapConn, user, LDAP_SASL_SIMPLE, &cred, NULL, NULL, NULL);
	} else {
		ast_debug(2, "bind %s anonymously\n", url);
		cred.bv_val = NULL;
		cred.bv_len = 0;
		bind_result = ldap_sasl_bind_s(ldapConn, NULL, LDAP_SASL_SIMPLE, &cred, NULL, NULL, NULL);
	}
	if (bind_result == LDAP_SUCCESS) {
		ast_debug(2, "Successfully connected to database.\n");
		connect_time = time(NULL);
		return 1;
	} else {
		ast_log(LOG_WARNING, "bind failed: %s\n", ldap_err2string(bind_result));
		ldap_unbind_ext_s(ldapConn, NULL, NULL);
		ldapConn = NULL;
		return 0;
	}
}

static char *realtime_ldap_status(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char status[256], credentials[100] = "";
	int ctimesec = time(NULL) - connect_time;

	switch (cmd) {
	case CLI_INIT:
		e->command = "realtime show ldap status";
		e->usage =
			"Usage: realtime show ldap status\n"
			"               Shows connection information for the LDAP RealTime driver\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (!ldapConn)
		return CLI_FAILURE;

	if (!ast_strlen_zero(url)) 
		snprintf(status, sizeof(status), "Connected to '%s', baseDN %s", url, base_distinguished_name);

	if (!ast_strlen_zero(user))
		snprintf(credentials, sizeof(credentials), " with username %s", user);

	if (ctimesec > 31536000) {
		ast_cli(a->fd, "%s%s for %d years, %d days, %d hours, %d minutes, %d seconds.\n",
				status, credentials, ctimesec / 31536000,
				(ctimesec % 31536000) / 86400, (ctimesec % 86400) / 3600,
				(ctimesec % 3600) / 60, ctimesec % 60);
	} else if (ctimesec > 86400) {
		ast_cli(a->fd, "%s%s for %d days, %d hours, %d minutes, %d seconds.\n",
				status, credentials, ctimesec / 86400, (ctimesec % 86400) / 3600,
				(ctimesec % 3600) / 60, ctimesec % 60);
	} else if (ctimesec > 3600) {
		ast_cli(a->fd, "%s%s for %d hours, %d minutes, %d seconds.\n",
				status, credentials, ctimesec / 3600, (ctimesec % 3600) / 60,
				ctimesec % 60);
	} else if (ctimesec > 60) {
		ast_cli(a->fd, "%s%s for %d minutes, %d seconds.\n", status, credentials,
					ctimesec / 60, ctimesec % 60);
	} else {
		ast_cli(a->fd, "%s%s for %d seconds.\n", status, credentials, ctimesec);
	}

	return CLI_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "LDAP realtime interface",
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
);
