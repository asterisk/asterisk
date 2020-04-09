/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2005 - 2010, Digium, Inc.
 *
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
 */

/*!
 * \file
 *
 * \author Russell Bryant <russell@digium.com>
 *
 * \brief A menu-driven system for Asterisk module selection
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <getopt.h>
#include <ctype.h>

#include <libxml/parser.h>
#include <libxml/tree.h>

#include "autoconfig.h"
#include "linkedlists.h"
#include "menuselect.h"

#define ARRAY_LEN(a) (size_t) (sizeof(a) / sizeof(0[a]))

#ifdef MENUSELECT_DEBUG
static FILE *debug;
#endif

/*! The list of categories */
struct categories categories = AST_LIST_HEAD_NOLOCK_INIT_VALUE;

/*!
   We have to maintain a pointer to the root of the trees generated from reading
   the build options XML files so that we can free it when we're done.  We don't
   copy any of the information over from these trees. Our list is just a
   convenient mapping to the information contained in these lists with one
   additional piece of information - whether the build option is enabled or not.
*/
struct tree {
	/*! the root of the tree */
	xmlDoc *root;
	/*! for linking */
	AST_LIST_ENTRY(tree) list;
};

/*! The list of trees from menuselect-tree files */
static AST_LIST_HEAD_NOLOCK_STATIC(trees, tree);

static const char * const tree_files[] = {
	"menuselect-tree"
};

static char *output_makeopts = OUTPUT_MAKEOPTS_DEFAULT;
static char *output_makedeps = OUTPUT_MAKEDEPS_DEFAULT;

/*! This is set to 1 if menuselect.makeopts pre-existed the execution of this app */
static int existing_config = 0;

/*! This is set when the --check-deps argument is provided. */
static int check_deps = 0;

/*! These are set when the --list-options or --list-groups arguments are provided. */
static int list_options = 0, list_groups = 0;

/*! This variable is non-zero when any changes are made */
int changes_made = 0;

/*! Menu name */
const char *menu_name = "Menuselect";

enum dep_file_state {
	DEP_FILE_UNKNOWN = -2,
	DEP_FILE_DISABLED = -1,
	DEP_FILE_UNMET = 0,
	DEP_FILE_MET = 1,
};

/*! Global list of dependencies that are external to the tree */
struct dep_file {
	char name[32];
	enum dep_file_state met;
	enum dep_file_state previously_met;
	AST_LIST_ENTRY(dep_file) list;
};
AST_LIST_HEAD_NOLOCK_STATIC(deps_file, dep_file);

/*! \brief return a pointer to the first non-whitespace character */
static inline char *skip_blanks(char *str)
{
	if (!str)
		return NULL;

	while (*str && *str < 33)
		str++;

	return str;
}

static int open_debug(void)
{
#ifdef MENUSELECT_DEBUG
	if (!(debug = fopen("menuselect_debug.txt", "w"))) {
		fprintf(stderr, "Failed to open menuselect_debug.txt for debug output.\n");
		return -1;
	}
#endif
	return 0;
}

#define print_debug(f, ...) __print_debug(__LINE__, f, ## __VA_ARGS__)
static void __attribute__((format(printf, 2, 3))) __print_debug(int line, const char *format, ...)
{
#ifdef MENUSELECT_DEBUG
	va_list ap;

	fprintf(debug, "%d -", line);

	va_start(ap, format);
	vfprintf(debug, format, ap);
	va_end(ap);

	fflush(debug);
#endif
}

static void close_debug(void)
{
#ifdef MENUSELECT_DEBUG
	if (debug)
		fclose(debug);
#endif
}

/*! \brief Finds a category with the given name or creates it if not found */
static struct category *category_find_or_create(const char *name)
{
	struct category *c;

	AST_LIST_TRAVERSE(&categories, c, list) {
		if (!strcmp(c->name, name)) {
			xmlFree((void *) name);
			return c;
		}
	}

	if (!(c = calloc(1, sizeof(*c)))) {
		return NULL;
	}

	c->name = name;

	AST_LIST_INSERT_TAIL(&categories, c, list);

	return c;
}

static void free_reference(struct reference *ref)
{
	/* If name and displayname point to the same place, only free one of them */
	if (ref->name == ref->displayname) {
		xmlFree((void *) ref->name);
	} else {
		xmlFree((void *) ref->name);
		xmlFree((void *) ref->displayname);
	}

	free(ref);
}

/*! \brief Free a member structure and all of its members */
static void free_member(struct member *mem)
{
	struct reference *ref;

	if (!mem) {
		return;
	}

	while ((ref = AST_LIST_REMOVE_HEAD(&mem->deps, list)))
		free_reference(ref);
	while ((ref = AST_LIST_REMOVE_HEAD(&mem->conflicts, list)))
		free_reference(ref);
	while ((ref = AST_LIST_REMOVE_HEAD(&mem->uses, list)))
		free_reference(ref);

	if (!mem->is_separator) {
		xmlFree((void *) mem->name);
		xmlFree((void *) mem->displayname);
		xmlFree((void *) mem->touch_on_change);
		xmlFree((void *) mem->remove_on_change);
		xmlFree((void *) mem->defaultenabled);
		xmlFree((void *) mem->support_level);
		xmlFree((void *) mem->replacement);
	}

	free(mem);
}

/*! \assigns values to support level strings */
static enum support_level_values string_to_support_level(const char *support_level)
{
	if (!support_level) {
		return SUPPORT_UNSPECIFIED;
	}

	if (!strcasecmp(support_level, "core")) {
		return SUPPORT_CORE;
	}

	if (!strcasecmp(support_level, "extended")) {
		return SUPPORT_EXTENDED;
	}

	if (!strcasecmp(support_level, "deprecated")) {
		return SUPPORT_DEPRECATED;
	}

	if (!strcasecmp(support_level, "external")) {
		return SUPPORT_EXTERNAL;
	}

	if (!strcasecmp(support_level, "option")) {
		return SUPPORT_OPTION;
	}

	return SUPPORT_UNSPECIFIED;
}

/*! \gets const separator strings from support level values */
static const char *support_level_to_string(enum support_level_values support_level)
{
	switch (support_level) {
	case SUPPORT_CORE:
		return "Core";
	case SUPPORT_EXTENDED:
		return "Extended";
	case SUPPORT_DEPRECATED:
		return "Deprecated";
	case SUPPORT_EXTERNAL:
		return "External";
	case SUPPORT_OPTION:
		return "Module Options";
	default:
		return "Unspecified";
	}
}

static void categories_flatten()
{
	int idx;
	struct category *c;
	struct member *m;

	AST_LIST_TRAVERSE(&categories, c, list) {
		for (idx = 0; idx < SUPPORT_COUNT; idx++) {
			struct support_level_bucket bucket = c->buckets[idx];
			while ((m = AST_LIST_REMOVE_HEAD(&bucket, list))) {
				AST_LIST_INSERT_TAIL(&c->members, m, list);
			}
		}
	}
}

/*! \sets default values for a given separator */
static struct member *create_separator(enum support_level_values level)
{
	struct member *separator = calloc(1, sizeof(*separator));
	separator->name = support_level_to_string(level);
	separator->displayname = "";
	separator->is_separator = 1;
	return separator;
}

/*! \adds a member to a category and attaches it to the last element of a particular support level used */
static int add_member_list_order(struct member *mem, struct category *cat)
{
	enum support_level_values support_level = string_to_support_level(mem->support_level);
	struct support_level_bucket *bucket = &cat->buckets[support_level];

	if (AST_LIST_EMPTY(bucket)) {
		struct member *sep = create_separator(support_level);
		AST_LIST_INSERT_TAIL(bucket, sep, list);
	}

	AST_LIST_INSERT_TAIL(bucket, mem, list);

	return 0;
}

static int process_xml_defaultenabled_node(xmlNode *node, struct member *mem)
{
	const char *tmp = (const char *) xmlNodeGetContent(node);

	if (tmp && !strlen_zero(tmp)) {
		mem->defaultenabled = tmp;
	}

	return 0;
}

static int process_xml_supportlevel_node(xmlNode *node, struct member *mem)
{
	const char *tmp = (const char *) xmlNodeGetContent(node);

	if (tmp && !strlen_zero(tmp)) {
		xmlFree((void *) mem->support_level);
		mem->support_level = tmp;
		print_debug("Set support_level for %s to %s\n", mem->name, mem->support_level);
	}

	return 0;
}

static int process_xml_replacement_node(xmlNode *node, struct member *mem)
{
	const char *tmp = (const char *) xmlNodeGetContent(node);

	if (tmp && !strlen_zero(tmp)) {
		mem->replacement = tmp;
	}

	return 0;
}

static int process_xml_ref_node(xmlNode *node, struct member *mem, struct reference_list *refs)
{
	struct reference *ref;
	const char *tmp;

	if (!(ref = calloc(1, sizeof(*ref)))) {
		free_member(mem);
		return -1;
	}

	if ((tmp = (const char *) xmlGetProp(node, BAD_CAST "name"))) {
		if (!strlen_zero(tmp)) {
			ref->name = tmp;
		}
	}

	if ((tmp = (const char *) xmlGetProp(node, BAD_CAST "autoselect"))) {
		ref->autoselect = !strcasecmp(tmp, "yes");
	}

	tmp = (const char *) xmlNodeGetContent(node);

	if (tmp && !strlen_zero(tmp)) {
		ref->displayname = tmp;
		if (!ref->name) {
			ref->name = ref->displayname;
		}

		AST_LIST_INSERT_TAIL(refs, ref, list);
	} else {
		free_reference(ref);
	}

	return 0;
}

static int process_xml_depend_node(xmlNode *node, struct member *mem)
{
	return process_xml_ref_node(node, mem, &mem->deps);
}

static int process_xml_conflict_node(xmlNode *node, struct member *mem)
{
	return process_xml_ref_node(node, mem, &mem->conflicts);
}

static int process_xml_use_node(xmlNode *node, struct member *mem)
{
	return process_xml_ref_node(node, mem, &mem->uses);
}

static int process_xml_member_data_node(xmlNode *node, struct member *mem)
{
	return 0;
}

static int process_xml_unknown_node(xmlNode *node, struct member *mem)
{
	fprintf(stderr, "Encountered unknown node: %s\n", node->name);
	return 0;
}

typedef int (*node_handler)(xmlNode *, struct member *);

static const struct {
	const char *name;
	node_handler func;
} node_handlers[] = {
	{ "defaultenabled", process_xml_defaultenabled_node },
	{ "support_level",  process_xml_supportlevel_node   },
	{ "replacement",    process_xml_replacement_node    },
	{ "depend",         process_xml_depend_node         },
	{ "conflict",       process_xml_conflict_node       },
	{ "use",            process_xml_use_node            },
	{ "member_data",    process_xml_member_data_node    },
};

static node_handler lookup_node_handler(xmlNode *node)
{
	int i;

	for (i = 0; i < ARRAY_LEN(node_handlers); i++) {
		if (!strcmp(node_handlers[i].name, (const char *) node->name)) {
			return node_handlers[i].func;
		}
	}

	return process_xml_unknown_node;
}

static int process_process_xml_category_child_node(xmlNode *node, struct member *mem)
{
	node_handler handler = lookup_node_handler(node);

	return handler(node, mem);
}

static int process_xml_member_node(xmlNode *node, struct category *cat)
{
	xmlNode *cur;
	struct member *mem;
	xmlChar *tmp;

	if (!(mem = calloc(1, sizeof(*mem)))) {
		return -1;
	}

	mem->name = (const char *) xmlGetProp(node, BAD_CAST "name");
	mem->displayname = (const char *) xmlGetProp(node, BAD_CAST "displayname");
	mem->touch_on_change = (const char *) xmlGetProp(node, BAD_CAST "touch_on_change");
	mem->remove_on_change = (const char *) xmlGetProp(node, BAD_CAST "remove_on_change");
	mem->support_level = (const char *) xmlCharStrdup("unspecified");

	if ((tmp = xmlGetProp(node, BAD_CAST "explicitly_enabled_only"))) {
		mem->explicitly_enabled_only = !xmlStrcasecmp(tmp, BAD_CAST "yes");
		xmlFree(tmp);
	}

	for (cur = node->children; cur; cur = cur->next) {
		if (cur->type != XML_ELEMENT_NODE) {
			continue;
		}

		process_process_xml_category_child_node(cur, mem);
	}

	if (!cat->positive_output) {
		mem->enabled = 1;
		if (!mem->defaultenabled || strcasecmp(mem->defaultenabled, "no")) {
			mem->was_enabled = 1;
			print_debug("Enabled %s because the category does not have positive output\n", mem->name);
		}
	}

	if (add_member_list_order(mem, cat)) {
		free_member(mem);
	}

	return 0;
}

static void free_category(struct category *cat)
{
	struct member *mem;

	xmlFree((void *) cat->name);
	xmlFree((void *) cat->displayname);
	xmlFree((void *) cat->remove_on_change);
	xmlFree((void *) cat->touch_on_change);

	while ((mem = AST_LIST_REMOVE_HEAD(&cat->members, list))) {
		free_member(mem);
	}

	free(cat);
}

static int process_xml_category_node(xmlNode *node)
{
	struct category *cat;
	const char *tmp;
	xmlNode *cur;

	if (!(tmp = (const char *) xmlGetProp(node, BAD_CAST "name"))) {
		fprintf(stderr, "Missing 'name' attribute for 'category' element.  Skipping...\n");
		/* Return success here so we don't bail on the whole document */
		return 0;
	}

	cat = category_find_or_create(tmp);

	if ((tmp = (const char *) xmlGetProp(node, BAD_CAST "displayname"))) {
		xmlFree((void *) cat->displayname);
		cat->displayname = tmp;
	}

	if ((tmp = (const char *) xmlGetProp(node, BAD_CAST "remove_on_change"))) {
		xmlFree((void *) cat->remove_on_change);
		cat->remove_on_change = tmp;
	}

	if ((tmp = (const char *) xmlGetProp(node, BAD_CAST "touch_on_change"))) {
		xmlFree((void *) cat->touch_on_change);
		cat->touch_on_change = tmp;
	}

	if ((tmp = (const char *) xmlGetProp(node, BAD_CAST "positive_output"))) {
		cat->positive_output = !strcasecmp(tmp, "yes");
		xmlFree((void *) tmp);
	}

	if ((tmp = (const char *) xmlGetProp(node, BAD_CAST "exclusive"))) {
		cat->exclusive = !strcasecmp(tmp, "yes");
		xmlFree((void *) tmp);
	}

	for (cur = node->children; cur; cur = cur->next) {
		if (cur->type != XML_ELEMENT_NODE) {
			continue;
		}

		if (xmlStrcmp(cur->name, BAD_CAST "member")) {
			fprintf(stderr, "Ignoring unknown element: %s\n", cur->name);
			continue;
		}

		process_xml_member_node(cur, cat);
	}

	return 0;
}

static int process_xml_menu_node(struct tree *tree, xmlNode *node)
{
	xmlNode *cur;
	xmlChar *tmp;

	if (strcmp((const char *) node->name, "menu")) {
		fprintf(stderr, "Invalid document: Expected \"menu\" element.\n");
		return -1;
	}

	AST_LIST_INSERT_HEAD(&trees, tree, list);

	if ((tmp = xmlGetProp(node, BAD_CAST "name"))) {
		menu_name = (const char *) tmp;
	}

	for (cur = node->children; cur; cur = cur->next) {
		if (cur->type != XML_ELEMENT_NODE) {
			continue;
		}

		if (xmlStrcmp(cur->name, BAD_CAST "category")) {
			fprintf(stderr, "Ignoring unknown element: %s\n", cur->name);
			continue;
		}

		if (process_xml_category_node(cur)) {
			return -1;
		}
	}

	categories_flatten();

	return 0;
}

/*! \brief Parse an input makeopts file */
static int parse_tree(const char *tree_file)
{
	struct tree *tree;
	xmlNode *menu;

	if (!(tree = calloc(1, sizeof(*tree)))) {
		return -1;
	}

	if (!(tree->root = xmlParseFile(tree_file))) {
		free(tree);
		return -1;
	}

	if (!(menu = xmlDocGetRootElement(tree->root))) {
		fprintf(stderr, "Invalid document: No root element\n");
		xmlFreeDoc(tree->root);
		free(tree);
		return -1;
	}

	if (process_xml_menu_node(tree, menu)) {
		xmlFreeDoc(tree->root);
		free(tree);
		return -1;
	}

	return 0;
}

/*!
 * \arg interactive Set to non-zero if being called while user is making changes
 */
static unsigned int calc_dep_failures(int interactive, int pre_confload)
{
	unsigned int result = 0;
	struct category *cat;
	struct member *mem;
	struct reference *dep;
	struct dep_file *dep_file;
	unsigned int changed, old_failure;

	AST_LIST_TRAVERSE(&categories, cat, list) {
		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			if (mem->is_separator) {
				continue;
			}
			old_failure = mem->depsfailed;
			AST_LIST_TRAVERSE(&mem->deps, dep, list) {
				if (dep->member)
					continue;

				mem->depsfailed = HARD_FAILURE;
				AST_LIST_TRAVERSE(&deps_file, dep_file, list) {
					if (!strcasecmp(dep_file->name, dep->name)) {
						if (dep_file->met == DEP_FILE_MET) {
							mem->depsfailed = NO_FAILURE;
						}
						break;
					}
				}
				if (mem->depsfailed != NO_FAILURE) {
					break; /* This dependency is not met, so we can stop now */
				}
			}
			if (old_failure == SOFT_FAILURE && mem->depsfailed != HARD_FAILURE)
				mem->depsfailed = SOFT_FAILURE;
		}
	}

	if (pre_confload) {
		return 0;
	}

	do {
		changed = 0;

		AST_LIST_TRAVERSE(&categories, cat, list) {
			AST_LIST_TRAVERSE(&cat->members, mem, list) {
				if (mem->is_separator) {
					continue;
				}

				old_failure = mem->depsfailed;

				if (mem->depsfailed == HARD_FAILURE)
					continue;

				mem->depsfailed = NO_FAILURE;

				AST_LIST_TRAVERSE(&mem->deps, dep, list) {
					if (!dep->member)
						continue;
					if (dep->member->depsfailed == HARD_FAILURE) {
						mem->depsfailed = HARD_FAILURE;
						break;
					} else if (dep->member->depsfailed == SOFT_FAILURE) {
						mem->depsfailed = SOFT_FAILURE;
					} else if (!dep->member->enabled) {
						mem->depsfailed = SOFT_FAILURE;
					}
				}

				if (mem->depsfailed != old_failure) {
					if ((mem->depsfailed == NO_FAILURE) && mem->was_defaulted) {
						mem->enabled = !strcasecmp(mem->defaultenabled, "yes");
						print_debug("Just set %s enabled to %d\n", mem->name, mem->enabled);
					} else {
						mem->enabled = interactive ? 0 : mem->was_enabled;
						print_debug("Just set %s enabled to %d\n", mem->name, mem->enabled);
					}
					changed = 1;
					break; /* This dependency is not met, so we can stop now */
				}
			}
			if (changed)
				break;
		}

		if (changed)
			result = 1;

	} while (changed);

	return result;
}

static unsigned int calc_conflict_failures(int interactive, int pre_confload)
{
	unsigned int result = 0;
	struct category *cat;
	struct member *mem;
	struct reference *cnf;
	struct dep_file *dep_file;
	unsigned int changed, old_failure;

	AST_LIST_TRAVERSE(&categories, cat, list) {
		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			if (mem->is_separator) {
				continue;
			}

			old_failure = mem->conflictsfailed;
			AST_LIST_TRAVERSE(&mem->conflicts, cnf, list) {
				if (cnf->member)
					continue;

				mem->conflictsfailed = NO_FAILURE;
				AST_LIST_TRAVERSE(&deps_file, dep_file, list) {
					if (!strcasecmp(dep_file->name, cnf->name)) {
						if (dep_file->met == DEP_FILE_MET) {
							mem->conflictsfailed = HARD_FAILURE;
							print_debug("Setting %s conflictsfailed to HARD_FAILURE\n", mem->name);
						}
						break;
					}
				}

				if (mem->conflictsfailed != NO_FAILURE)
					break; /* This conflict was found, so we can stop now */
			}
			if (old_failure == SOFT_FAILURE && mem->conflictsfailed != HARD_FAILURE) {
				print_debug("%d - Setting %s conflictsfailed to SOFT_FAILURE\n", __LINE__, mem->name);
				mem->conflictsfailed = SOFT_FAILURE;
			}
		}
	}

	if (pre_confload) {
		return 0;
	}

	do {
		changed = 0;

		AST_LIST_TRAVERSE(&categories, cat, list) {
			AST_LIST_TRAVERSE(&cat->members, mem, list) {
				if (mem->is_separator) {
					continue;
				}

				old_failure = mem->conflictsfailed;

				if (mem->conflictsfailed == HARD_FAILURE)
					continue;

				mem->conflictsfailed = NO_FAILURE;

				AST_LIST_TRAVERSE(&mem->conflicts, cnf, list) {
					if (!cnf->member)
						continue;

					if (cnf->member->enabled) {
						mem->conflictsfailed = SOFT_FAILURE;
						print_debug("%d - Setting %s conflictsfailed to SOFT_FAILURE because %s is enabled\n", __LINE__, mem->name, cnf->member->name);
						break;
					}
				}

				if (mem->conflictsfailed != old_failure && mem->conflictsfailed != NO_FAILURE) {
					mem->enabled = 0;
					print_debug("Just set %s enabled to %d because of conflicts\n", mem->name, mem->enabled);
					changed = 1;
					break; /* This conflict has been found, so we can stop now */
				}
			}
			if (changed)
				break;
		}

		if (changed)
			result = 1;

	} while (changed);

	return result;
}

/*! \brief Process dependencies against the input dependencies file */
static int process_deps(void)
{
	FILE *f;
	char buf[80];
	int res = 0;
	struct dep_file *dep_file;

	if (!(f = fopen(MENUSELECT_DEPS, "r"))) {
		fprintf(stderr, "Unable to open '%s' for reading!  Did you run ./configure ?\n", MENUSELECT_DEPS);
		return -1;
	}

	/* Build a dependency list from the file generated by configure */
	while (memset(buf, 0, sizeof(buf)), fgets(buf, sizeof(buf), f)) {
		char *name, *cur, *prev, *p;
		int val;

		/* Strip trailing CR/NL */
		while ((p = strchr(buf, '\r')) || (p = strchr(buf, '\n'))) {
			*p = '\0';
		}

		p = buf;
		name = strsep(&p, "=");

		if (!p)
			continue;

		cur = strsep(&p, ":");
		prev = strsep(&p, ":");

		if (!(dep_file = calloc(1, sizeof(*dep_file))))
			break;

		strncpy(dep_file->name, name, sizeof(dep_file->name) - 1);
		dep_file->met = DEP_FILE_UNKNOWN;
		dep_file->previously_met = DEP_FILE_UNKNOWN;

		if (sscanf(cur, "%d", &val) != 1) {
			fprintf(stderr, "Unknown value '%s' found in %s for %s\n", cur, MENUSELECT_DEPS, name);
		} else {
			switch (val) {
			case DEP_FILE_MET:
			case DEP_FILE_UNMET:
			case DEP_FILE_DISABLED:
				dep_file->met = val;
				break;
			default:
				fprintf(stderr, "Unknown value '%s' found in %s for %s\n", cur, MENUSELECT_DEPS, name);
				break;
			}
		}

		if (prev) {
			if (sscanf(prev, "%d", &val) != 1) {
				fprintf(stderr, "Unknown value '%s' found in %s for %s\n", prev, MENUSELECT_DEPS, name);
			} else {
				switch (val) {
				case DEP_FILE_MET:
				case DEP_FILE_UNMET:
				case DEP_FILE_DISABLED:
					dep_file->previously_met = val;
					break;
				default:
					fprintf(stderr, "Unknown value '%s' found in %s for %s\n", prev, MENUSELECT_DEPS, name);
					break;
				}
			}
		}

		AST_LIST_INSERT_TAIL(&deps_file, dep_file, list);
	}

	fclose(f);

	return res;
}

static void free_deps_file(void)
{
	struct dep_file *dep_file;

	/* Free the dependency list we built from the file */
	while ((dep_file = AST_LIST_REMOVE_HEAD(&deps_file, list)))
		free(dep_file);
}

static int match_member_relations(void)
{
	struct category *cat, *cat2;
	struct member *mem, *mem2;
	struct reference *dep;
	struct reference *cnf;
	struct reference *use;

	/* Traverse through each module's dependency list and determine whether each is another module */
	AST_LIST_TRAVERSE(&categories, cat, list) {
		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			if (mem->is_separator) {
				continue;
			}

			AST_LIST_TRAVERSE(&mem->deps, dep, list) {
				AST_LIST_TRAVERSE(&cat->members, mem2, list) {
					if (mem->is_separator) {
						continue;
					}

					if (strcasecmp(mem2->name, dep->name))
						continue;

					dep->member = mem2;
					break;
				}
				if (dep->member)
					continue;

				AST_LIST_TRAVERSE(&categories, cat2, list) {
					AST_LIST_TRAVERSE(&cat2->members, mem2, list) {
						if (mem->is_separator) {
							continue;
						}

						if (strcasecmp(mem2->name, dep->name))
							continue;

						dep->member = mem2;
						break;
					}
					if (dep->member)
						break;
				}
			}
		}
	}

	/* Traverse through each module's use list and determine whether each is another module */
	AST_LIST_TRAVERSE(&categories, cat, list) {
		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			if (mem->is_separator) {
				continue;
			}

			AST_LIST_TRAVERSE(&mem->uses, use, list) {
				AST_LIST_TRAVERSE(&cat->members, mem2, list) {
					if (mem->is_separator) {
						continue;
					}

					if (strcasecmp(mem2->name, use->name))
						continue;

					use->member = mem2;
					break;
				}
				if (use->member)
					continue;

				AST_LIST_TRAVERSE(&categories, cat2, list) {
					AST_LIST_TRAVERSE(&cat2->members, mem2, list) {
						if (mem->is_separator) {
							continue;
						}

						if (strcasecmp(mem2->name, use->name))
							continue;

						use->member = mem2;
						break;
					}
					if (use->member)
						break;
				}
			}
		}
	}

/*
 * BUGBUG:
 * This doesn't work, the only way we can fix this is to remove OPTIONAL_API
 * toggle from menuselect and add a command-line argument to ./configure.
 */
#if 0
	AST_LIST_TRAVERSE(&categories, cat, list) {
		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			if (mem->is_separator) {
				continue;
			}

			AST_LIST_TRAVERSE_SAFE_BEGIN(&mem->uses, use, list) {
				if (use->member) {
					AST_LIST_REMOVE_CURRENT(&mem->uses, list);
					AST_LIST_INSERT_TAIL(&mem->deps, use, list);
				}
			}
			AST_LIST_TRAVERSE_SAFE_END;
		}
	}
#endif

	/* Traverse through each category marked as exclusive and mark every member as conflicting with every other member */
	AST_LIST_TRAVERSE(&categories, cat, list) {
		if (!cat->exclusive)
			continue;

		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			if (mem->is_separator) {
				continue;
			}

			AST_LIST_TRAVERSE(&cat->members, mem2, list) {
				if (mem->is_separator) {
					continue;
				}

				if (mem2 == mem)
					continue;

				if (!(cnf = calloc(1, sizeof(*cnf))))
					return -1;

				cnf->name = mem2->name;
				cnf->member = mem2;
				AST_LIST_INSERT_TAIL(&mem->conflicts, cnf, list);
			}
		}
	}

	/* Traverse through each category and determine whether named conflicts for each module are other modules */
	AST_LIST_TRAVERSE(&categories, cat, list) {
		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			if (mem->is_separator) {
				continue;
			}

			AST_LIST_TRAVERSE(&mem->conflicts, cnf, list) {
				AST_LIST_TRAVERSE(&cat->members, mem2, list) {
					if (mem->is_separator) {
						continue;
					}

					if (strcasecmp(mem2->name, cnf->name))
						continue;

					cnf->member = mem2;
					break;
				}
				if (cnf->member)
					continue;

				AST_LIST_TRAVERSE(&categories, cat2, list) {
					AST_LIST_TRAVERSE(&cat2->members, mem2, list) {
						if (mem->is_separator) {
							continue;
						}

						if (strcasecmp(mem2->name, cnf->name))
							continue;

						cnf->member = mem2;
						break;
					}
					if (cnf->member)
						break;
				}
			}
		}
	}

	return 0;
}

/*! \brief Iterate through all of the input tree files and call the parse function on them */
static int build_member_list(void)
{
	int i;
	int res = -1;

	for (i = 0; i < (sizeof(tree_files) / sizeof(tree_files[0])); i++) {
		if ((res = parse_tree(tree_files[i]))) {
			fprintf(stderr, "Error parsing '%s'!\n", tree_files[i]);
			break;
		}
	}

	if (!res)
		res = match_member_relations();

	return res;
}

/*! \brief Given the string representation of a member and category, mark it as present in a given input file */
static void mark_as_present(const char *member, const char *category)
{
	struct category *cat;
	struct member *mem;
	char negate = 0;

	if (*member == '-') {
		member++;
		negate = 1;
	}

	print_debug("Marking %s of %s as present\n", member, category);

	AST_LIST_TRAVERSE(&categories, cat, list) {
		if (strcmp(category, cat->name))
			continue;
		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			if (mem->is_separator) {
				continue;
			}

			if (!strcmp(member, mem->name)) {
				mem->was_enabled = mem->enabled = (negate ? !cat->positive_output : cat->positive_output);
				print_debug("Just set %s enabled to %d\n", mem->name, mem->enabled);
				break;
			}
		}
		if (!mem)
			fprintf(stderr, "member '%s' in category '%s' not found, ignoring.\n", member, category);
		break;
	}

	if (!cat)
		fprintf(stderr, "category '%s' not found! Can't mark '%s' as disabled.\n", category, member);
}

unsigned int enable_member(struct member *mem)
{
	struct reference *dep;
	unsigned int can_enable = 1;

	AST_LIST_TRAVERSE(&mem->deps, dep, list) {
		if (!dep->member)
			continue;

		if (!dep->member->enabled) {
			if (dep->member->conflictsfailed != NO_FAILURE) {
				can_enable = 0;
				break;
			}

			if (dep->member->depsfailed == HARD_FAILURE) {
				can_enable = 0;
				break;
			}

			if (dep->member->explicitly_enabled_only) {
				can_enable = 0;
				break;
			}

			if (!(can_enable = enable_member(dep->member)))
				break;
		}
	}

	if ((mem->enabled = can_enable)) {
		struct reference *use;

		print_debug("Just set %s enabled to %d\n", mem->name, mem->enabled);
		while (calc_dep_failures(1, 0) || calc_conflict_failures(1, 0));

		AST_LIST_TRAVERSE(&mem->uses, use, list) {
			if (use->member && use->autoselect && !use->member->enabled) {
				enable_member(use->member);
			}
		}
	}

	return can_enable;
}

void toggle_enabled(struct member *mem)
{
	if ((mem->depsfailed == HARD_FAILURE) || (mem->conflictsfailed == HARD_FAILURE) || (mem->is_separator))
		return;

	if (!mem->enabled)
		enable_member(mem);
	else
		mem->enabled = 0;

	print_debug("3- changed %s to %d\n", mem->name, mem->enabled);
	mem->was_defaulted = 0;
	changes_made++;

	while (calc_dep_failures(1, 0) || calc_conflict_failures(1, 0));
}

/*! \brief Toggle a member of a category at the specified index to enabled/disabled */
void toggle_enabled_index(struct category *cat, int index)
{
	struct member *mem;
	int i = 0;

	AST_LIST_TRAVERSE(&cat->members, mem, list) {
		if (i++ == index)
			break;
	}

	if (!mem)
		return;

	toggle_enabled(mem);
}

static void set_member_enabled(struct member *mem)
{
	if ((mem->depsfailed == HARD_FAILURE) || (mem->conflictsfailed == HARD_FAILURE))
		return;

	if ((mem->enabled) || (mem->is_separator))
		return;

	enable_member(mem);
	mem->was_defaulted = 0;
	changes_made++;

	while (calc_dep_failures(1, 0) || calc_conflict_failures(1, 0));
}

void set_enabled(struct category *cat, int index)
{
	struct member *mem;
	int i = 0;

	AST_LIST_TRAVERSE(&cat->members, mem, list) {
		if (mem->is_separator) {
			continue;
		}

		if (i++ == index)
			break;
	}

	if (!mem)
		return;

	set_member_enabled(mem);
}

static void clear_member_enabled(struct member *mem)
{
	if (!mem->enabled)
		return;

	mem->enabled = 0;
	mem->was_defaulted = 0;
	changes_made++;

	while (calc_dep_failures(1, 0) || calc_conflict_failures(1, 0));
}

void clear_enabled(struct category *cat, int index)
{
	struct member *mem;
	int i = 0;

	AST_LIST_TRAVERSE(&cat->members, mem, list) {
		if (mem->is_separator) {
			continue;
		}

		if (i++ == index)
			break;
	}

	if (!mem)
		return;

	clear_member_enabled(mem);
}

/*! \brief Process a previously failed dependency
 *
 * If a module was previously disabled because of a failed dependency
 * or a conflict, and not because the user selected it to be that way,
 * then it needs to be re-enabled by default if the problem is no longer present.
 */
static void process_prev_failed_deps(char *buf)
{
	const char *cat_name, *mem_name;
	struct category *cat;
	struct member *mem;

	cat_name = strsep(&buf, "=");
	mem_name = strsep(&buf, "\n");

	if (!cat_name || !mem_name)
		return;

	AST_LIST_TRAVERSE(&categories, cat, list) {
		if (strcasecmp(cat->name, cat_name))
			continue;
		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			if (mem->is_separator) {
				continue;
			}

			if (strcasecmp(mem->name, mem_name))
				continue;

			if (!mem->depsfailed && !mem->conflictsfailed) {
				mem->enabled = 1;
				print_debug("Just set %s enabled to %d in processing of previously failed deps\n", mem->name, mem->enabled);
				mem->was_defaulted = 0;
			}

			break;
		}
		break;
	}

	if (!cat || !mem)
		fprintf(stderr, "Unable to find '%s' in category '%s'\n", mem_name, cat_name);
}

/*! \brief Parse an existing output makeopts file and enable members previously selected */
static int parse_existing_config(const char *infile)
{
	FILE *f;
#define PARSE_BUF_SIZE 8192
	char *buf = NULL;
	char *category, *parse, *member;
	int lineno = 0;

	if (!(f = fopen(infile, "r"))) {
		/* This isn't really an error, so only print the message in debug mode */
		print_debug("Unable to open '%s' for reading existing config.\n", infile);
		return -1;
	}

	buf = malloc(PARSE_BUF_SIZE);
	if (!buf) {
		fprintf(stderr, "Unable to allocate buffer for reading existing config.\n");
		exit(1);
	}

	while (fgets(buf, PARSE_BUF_SIZE, f)) {
		lineno++;

		if (strlen_zero(buf))
			continue;

		/* skip lines that are not for this tool */
		if (strncasecmp(buf, "MENUSELECT_", strlen("MENUSELECT_")))
			continue;

		if (!strncasecmp(buf, "MENUSELECT_DEPENDS_", strlen("MENUSELECT_DEPENDS_")))
			continue;

		if (!strncasecmp(buf, "MENUSELECT_BUILD_DEPS", strlen("MENUSELECT_BUILD_DEPS")))
			continue;

		parse = buf;
		parse = skip_blanks(parse);
		if (strlen_zero(parse))
			continue;

		/* Grab the category name */
		category = strsep(&parse, "=");
		if (!parse) {
			fprintf(stderr, "Invalid string in '%s' at line '%d'!\n", output_makeopts, lineno);
			continue;
		}

		parse = skip_blanks(parse);

		if (!strcasecmp(category, "MENUSELECT_DEPSFAILED")) {
			process_prev_failed_deps(parse);
			continue;
		}

		while ((member = strsep(&parse, " \n"))) {
			member = skip_blanks(member);
			if (strlen_zero(member))
				continue;
			mark_as_present(member, category);
		}
	}

	free(buf);
	fclose(f);

	return 0;
}

/*! \brief Create the output dependencies file */
static int generate_makedeps_file(void)
{
	FILE *f;
	struct category *cat;
	struct member *mem;
	struct reference *dep;
	struct reference *use;
	struct dep_file *dep_file;

	if (!(f = fopen(output_makedeps, "w"))) {
		fprintf(stderr, "Unable to open dependencies file (%s) for writing!\n", output_makedeps);
		return -1;
	}

	/* Traverse all categories and members and mark which used packages were found,
	 * skipping other members
	 */
	AST_LIST_TRAVERSE(&categories, cat, list) {
		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			if (mem->is_separator) {
				continue;
			}

			AST_LIST_TRAVERSE(&mem->uses, use, list) {
				if (use->member) {
					use->met = 0;
					continue;
				}
				AST_LIST_TRAVERSE(&deps_file, dep_file, list) {
					if ((use->met = !strcasecmp(use->name, dep_file->name))) {
						break;
					}
				}
			}
		}
	}

	/* Traverse all categories and members and output dependencies for each member */
	AST_LIST_TRAVERSE(&categories, cat, list) {
		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			if (mem->is_separator) {
				continue;
			}

			unsigned char header_printed = 0;

			if (AST_LIST_EMPTY(&mem->deps) && AST_LIST_EMPTY(&mem->uses))
				continue;

			AST_LIST_TRAVERSE(&mem->deps, dep, list) {
				const char *c;

				if (dep->member) {
					continue;
				}

				if (!header_printed) {
					fprintf(f, "MENUSELECT_DEPENDS_%s=", mem->name);
					header_printed = 1;
				}

				for (c = dep->name; *c; c++)
					fputc(toupper(*c), f);
				fputc(' ', f);
			}
			AST_LIST_TRAVERSE(&mem->uses, use, list) {
				const char *c;

				if (!use->met) {
					continue;
				}

				if (!header_printed) {
					fprintf(f, "MENUSELECT_DEPENDS_%s=", mem->name);
					header_printed = 1;
				}

				for (c = use->name; *c; c++)
					fputc(toupper(*c), f);
				fputc(' ', f);
			}

			if (header_printed) {
				fprintf(f, "\n");
			}
		}
	}

	fclose(f);

	return 0;
}

/*! \brief Create the output makeopts file that results from the user's selections */
static int generate_makeopts_file(void)
{
	FILE *f;
	struct category *cat;
	struct member *mem;
	struct reference *dep;
	struct reference *use;

	if (!(f = fopen(output_makeopts, "w"))) {
		fprintf(stderr, "Unable to open build configuration file (%s) for writing!\n", output_makeopts);
		return -1;
	}

	/* Traverse all categories and members and output them as var/val pairs */
	AST_LIST_TRAVERSE(&categories, cat, list) {
		fprintf(f, "%s=", cat->name);
		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			if (mem->is_separator) {
				continue;
			}

			if ((!cat->positive_output && (!mem->enabled || mem->depsfailed || mem->conflictsfailed)) ||
			    (cat->positive_output && mem->enabled && !mem->depsfailed && !mem->conflictsfailed))
				fprintf(f, "%s ", mem->name);
		}
		fprintf(f, "\n");
	}

	/* Traverse all categories and members, and for every member that is not disabled,
	   if it has internal dependencies (other members), list those members one time only
	   in a special variable */
	fprintf(f, "MENUSELECT_BUILD_DEPS=");
	AST_LIST_TRAVERSE(&categories, cat, list) {
		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			if (mem->is_separator) {
				continue;
			}

			if ((!cat->positive_output && (!mem->enabled || mem->depsfailed || mem->conflictsfailed)) ||
			    (cat->positive_output && mem->enabled && !mem->depsfailed && !mem->conflictsfailed))
				continue;

			AST_LIST_TRAVERSE(&mem->deps, dep, list) {
				/* we only care about dependencies between members (internal, not external) */
				if (!dep->member)
					continue;
				/* if this has already been output, continue */
				if (dep->member->build_deps_output)
					continue;
				fprintf(f, "%s ", dep->member->name);
				dep->member->build_deps_output = 1;
			}
			AST_LIST_TRAVERSE(&mem->uses, use, list) {
				/* we only care about dependencies between members (internal, not external) */
				if (!use->member)
					continue;
				/* if the dependency module is not going to be built, don't list it */
				if (!use->member->enabled)
					continue;
				/* if this has already been output, continue */
				if (use->member->build_deps_output)
					continue;
				fprintf(f, "%s ", use->member->name);
				use->member->build_deps_output = 1;
			}
		}
	}
	fprintf(f, "\n");

	/* Output which members were disabled because of failed dependencies or conflicts */
	AST_LIST_TRAVERSE(&categories, cat, list) {
		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			if (mem->is_separator) {
				continue;
			}

			if (mem->depsfailed != HARD_FAILURE && mem->conflictsfailed != HARD_FAILURE)
				continue;

			if (!mem->defaultenabled || !strcasecmp(mem->defaultenabled, "yes"))
				fprintf(f, "MENUSELECT_DEPSFAILED=%s=%s\n", cat->name, mem->name);
		}
	}

	fclose(f);

	/* there is no need to process remove_on_change rules if we did not have
	   configuration information to start from
	*/
	if (!existing_config)
		return 0;

	/* Traverse all categories and members and remove any files that are supposed
	   to be removed when an item has been changed */
	AST_LIST_TRAVERSE(&categories, cat, list) {
		unsigned int had_changes = 0;
		char rmcommand[256] = "rm -rf ";
		char touchcommand[256] = "touch -c ";
		char *file, *buf;

		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			if (mem->is_separator) {
				continue;
			}

			if ((mem->enabled == mem->was_enabled) && !mem->was_defaulted)
				continue;

			had_changes = 1;

			if (mem->touch_on_change) {
				for (buf = ast_strdupa(mem->touch_on_change), file = strsep(&buf, " ");
				     file;
				     file = strsep(&buf, " ")) {
					strcpy(&touchcommand[9], file);
					system(touchcommand);
				}
			}

			if (mem->remove_on_change) {
				for (buf = ast_strdupa(mem->remove_on_change), file = strsep(&buf, " ");
				     file;
				     file = strsep(&buf, " ")) {
					strcpy(&rmcommand[7], file);
					system(rmcommand);
				}
			}
		}

		if (cat->touch_on_change && had_changes) {
			for (buf = ast_strdupa(cat->touch_on_change), file = strsep(&buf, " ");
			     file;
			     file = strsep(&buf, " ")) {
				strcpy(&touchcommand[9], file);
				system(touchcommand);
			}
		}

		if (cat->remove_on_change && had_changes) {
			for (buf = ast_strdupa(cat->remove_on_change), file = strsep(&buf, " ");
			     file;
			     file = strsep(&buf, " ")) {
				strcpy(&rmcommand[7], file);
				system(rmcommand);
			}
		}
	}

	return 0;
}

/*! \brief Print out all of the information contained in our tree */
static void dump_member_list(void)
{
#ifdef MENUSELECT_DEBUG
	struct category *cat;
	struct member *mem;
	struct reference *dep;
	struct reference *cnf;

	AST_LIST_TRAVERSE(&categories, cat, list) {
		fprintf(stderr, "Category: '%s'\n", cat->name);
		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			if (mem->is_separator) {
				continue;
			}

			fprintf(stderr, "   ==>> Member: '%s'  (%s)", mem->name, mem->enabled ? "Enabled" : "Disabled");
			fprintf(stderr, "        Was %s\n", mem->was_enabled ? "Enabled" : "Disabled");
			if (mem->defaultenabled)
				fprintf(stderr, "        Defaults to %s\n", !strcasecmp(mem->defaultenabled, "yes") ? "Enabled" : "Disabled");
			AST_LIST_TRAVERSE(&mem->deps, dep, list)
				fprintf(stderr, "      --> Depends on: '%s'\n", dep->name);
			if (!AST_LIST_EMPTY(&mem->deps))
				fprintf(stderr, "      --> Dependencies Met: %s\n", mem->depsfailed ? "No" : "Yes");
			AST_LIST_TRAVERSE(&mem->conflicts, cnf, list)
				fprintf(stderr, "      --> Conflicts with: '%s'\n", cnf->name);
			if (!AST_LIST_EMPTY(&mem->conflicts))
				fprintf(stderr, "      --> Conflicts Found: %s\n", mem->conflictsfailed ? "Yes" : "No");
		}
	}
#endif
}

/*! \brief Free all categories and their members */
static void free_member_list(void)
{
	struct category *cat;

	while ((cat = AST_LIST_REMOVE_HEAD(&categories, list))) {
		free_category(cat);
	}
}

/*! \brief Free all of the XML trees */
static void free_trees(void)
{
	struct tree *tree;

	while ((tree = AST_LIST_REMOVE_HEAD(&trees, list))) {
		xmlFreeDoc(tree->root);
		free(tree);
	}
}

/*! \brief Enable/Disable all members of a category as long as dependencies have been met and no conflicts are found */
void set_all(struct category *cat, int val)
{
	struct member *mem;

	AST_LIST_TRAVERSE(&cat->members, mem, list) {
		if (mem->enabled == val)
			continue;

		if (mem->is_separator)
			continue;

		if ((mem->depsfailed == HARD_FAILURE) || (mem->conflictsfailed == HARD_FAILURE))
			continue;

		if (val) {
			enable_member(mem);
		} else {
			mem->enabled = 0;
		}

		mem->was_defaulted = 0;
		changes_made++;
	}

	while (calc_dep_failures(1, 0) || calc_conflict_failures(1, 0));
}

int count_categories(void)
{
	struct category *cat;
	int count = 0;

	AST_LIST_TRAVERSE(&categories, cat, list)
		count++;

	return count;
}

int count_members(struct category *cat)
{
	struct member *mem;
	int count = 0;

	AST_LIST_TRAVERSE(&cat->members, mem, list)
		count++;

	return count;
}

static void print_sanity_dep_header(struct dep_file *dep_file, unsigned int *flag)
{
	fprintf(stderr, "\n"
		"***********************************************************\n"
		"  The '%s' dependency was previously satisfied but         \n"
		"  is now unsatisfied.                                      \n",
		dep_file->name);
	*flag = 1;
}

/*! \brief Make sure an existing menuselect.makeopts disabled everything it should have */
static int sanity_check(void)
{
	unsigned int insane = 0;
	struct category *cat;
	struct member *mem;
	struct reference *dep;
	struct reference *use;
	struct dep_file *dep_file;
	unsigned int dep_header_printed;
	unsigned int group_header_printed;

	AST_LIST_TRAVERSE(&deps_file, dep_file, list) {
		if (!((dep_file->previously_met == DEP_FILE_MET) &&
		      (dep_file->met == DEP_FILE_UNMET))) {
			continue;
		}

		/* this dependency was previously met, but now is not, so
		   warn the user about members that could be affected by it
		*/

		dep_header_printed = 0;

		group_header_printed = 0;
		AST_LIST_TRAVERSE(&categories, cat, list) {
			AST_LIST_TRAVERSE(&cat->members, mem, list) {
				if (mem->is_separator) {
					continue;
				}

				if (!mem->enabled) {
					continue;
				}
				AST_LIST_TRAVERSE(&mem->deps, dep, list) {
					if (strcasecmp(dep->name, dep_file->name)) {
						continue;
					}
					if (!group_header_printed) {
						if (!dep_header_printed) {
							print_sanity_dep_header(dep_file, &dep_header_printed);
						}
						fprintf(stderr, "\n"
							"  The following modules will no longer be available:\n");
						group_header_printed = 1;
					}
					fprintf(stderr, "          %s\n", mem->name);
					insane = 1;
				}
			}
		}

		group_header_printed = 0;
		AST_LIST_TRAVERSE(&categories, cat, list) {
			AST_LIST_TRAVERSE(&cat->members, mem, list) {
				if (mem->is_separator) {
					continue;
				}

				if (!mem->enabled) {
					continue;
				}
				AST_LIST_TRAVERSE(&mem->uses, use, list) {
					if (strcasecmp(use->name, dep_file->name)) {
						continue;
					}
					if (!group_header_printed) {
						if (!dep_header_printed) {
							print_sanity_dep_header(dep_file, &dep_header_printed);
						}
						fprintf(stderr, "\n"
							"  The functionality of the following modules will\n"
							"  be affected:\n");
						group_header_printed = 1;
					}
					fprintf(stderr, "          %s\n", mem->name);
					insane = 1;
				}
			}
		}

		if (dep_header_printed) {
			fprintf(stderr,
				"***********************************************************\n");
		}
	}

	AST_LIST_TRAVERSE(&categories, cat, list) {
		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			if (mem->is_separator) {
				continue;
			}

			if ((mem->depsfailed || mem->conflictsfailed) && mem->enabled) {
				fprintf(stderr, "\n"
					"***********************************************************\n"
					"  The existing menuselect.makeopts file did not specify    \n"
					"  that '%s' should not be included.  However, either some  \n"
					"  dependencies for this module were not found or a         \n"
					"  conflict exists.                                         \n"
					"                                                           \n"
					"  Either run 'make menuselect' or remove the existing      \n"
					"  menuselect.makeopts file to resolve this issue.          \n"
					"***********************************************************\n"
					"\n", mem->name);
				insane = 1;
			}
		}
	}

	return insane ? -1 : 0;
}

/* \brief Set the forced default values if they exist */
static void process_defaults(void)
{
	struct category *cat;
	struct member *mem;

	print_debug("Processing default values since config was not present\n");

	AST_LIST_TRAVERSE(&categories, cat, list) {
		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			if (mem->is_separator) {
				continue;
			}

			if (!mem->defaultenabled)
				continue;

			if (mem->depsfailed == HARD_FAILURE)
				continue;

			if (mem->conflictsfailed == HARD_FAILURE)
				continue;

			if (!strcasecmp(mem->defaultenabled, "yes")) {
				mem->enabled = 1;
				mem->was_defaulted = 1;
			} else if (!strcasecmp(mem->defaultenabled, "no")) {
				mem->enabled = 0;
				mem->was_defaulted = 1;
			} else
				fprintf(stderr, "Invalid defaultenabled value for '%s' in category '%s'\n", mem->name, cat->name);
		}
	}

}

struct member *find_member(const char *name)
{
	struct category *cat;
	struct member *mem;

	AST_LIST_TRAVERSE(&categories, cat, list) {
		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			if (mem->is_separator) {
				continue;
			}

			if (!strcasecmp(name, mem->name)) {
				return mem;
			}
		}
	}

	return NULL;
}

struct category *find_category(const char *name)
{
	struct category *cat;

	AST_LIST_TRAVERSE(&categories, cat, list) {
		if (!strcasecmp(name, cat->name)) {
			return cat;
		}
	}

	return NULL;
}

static int usage(const char *argv0)
{
	fprintf(stderr, "Usage: %s [--enable <option>] [--disable <option>]\n", argv0);
	fprintf(stderr, "   [--enable-category <category>] [--enable-all]\n");
	fprintf(stderr, "   [--disable-category <category>] [--disable-all] [...]\n");
	fprintf(stderr, "   [<config-file> [...]]\n");
	fprintf(stderr, "Usage: %s { --check-deps | --list-options\n", argv0);
	fprintf(stderr, "   | --list-category <category> | --category-list | --help }\n");
	fprintf(stderr, "   [<config-file> [...]]\n");
	return 0;
}

int main(int argc, char *argv[])
{
	int res = 0;
	const char *list_group = NULL;
	unsigned int x;
	static struct option long_options[] = {
		/*
		 * The --check-deps option is used to ask this application to check to
		 * see if that an existing menuselect.makeopts file contains all of the
		 * modules that have dependencies that have not been met.  If this
		 * is not the case, an informative message will be printed to the
		 * user and the build will fail.
		 */
		{ "check-deps",       no_argument,       &check_deps,   1  },
		{ "enable",           required_argument, 0,            'e' },
		{ "enable-category",  required_argument, 0,            'E' },
		{ "enable-all",       no_argument,       0,            'a' },
		{ "disable",          required_argument, 0,            'd' },
		{ "disable-category", required_argument, 0,            'D' },
		{ "disable-all",      no_argument,       0,            'A' },
		{ "list-options",     no_argument,       &list_options, 1  },
		{ "list-category",    required_argument, 0,            'L' },
		{ "category-list",    no_argument,       &list_groups,  1  },
		{ "help",             no_argument,       0,            'h' },

		{ 0, 0, 0, 0 },
	};
	int do_menu = 1, do_settings = 1;
	int c, option_index = 0;

	if (open_debug()) {
		exit(1);
	}

	LIBXML_TEST_VERSION;

	/* Parse the input XML files to build the list of available options */
	if ((res = build_member_list()))
		exit(res);

	/* Load module dependencies */
	if ((res = process_deps()))
		exit(res);

	while (calc_dep_failures(0, 1) || calc_conflict_failures(0, 1));

	while ((c = getopt_long(argc, argv, "", long_options, &option_index)) != -1) {
		switch (c) {
		case 'L':
			list_group = optarg;
			do_settings = 0;
			/* Fall-through */
		case 'a':
		case 'A':
		case 'e':
		case 'E':
		case 'd':
		case 'D':
			do_menu = 0;
			break;
		case 'h':
			return usage(argv[0]);
			break;
		default:
			break;
		}
	}

	if (check_deps || list_options || list_groups) {
		do_menu = 0;
		do_settings = 0;
	}

	if (optind < argc) {
		for (x = optind; x < argc; x++) {
			res = parse_existing_config(argv[x]);
			if (!res && !strcasecmp(argv[x], OUTPUT_MAKEOPTS_DEFAULT))
				existing_config = 1;
			res = 0;
		}
	}

	/* Dump the list produced by parsing the various input files */
	dump_member_list();

	while (calc_dep_failures(0, 0) || calc_conflict_failures(0, 0));

	if (!existing_config)
		process_defaults();
	else if (check_deps)
		res = sanity_check();

	while (calc_dep_failures(0, 0) || calc_conflict_failures(0, 0));

	print_debug("do_menu=%d, do_settings=%d\n", do_menu, do_settings);

	if (do_menu && !res) {
		res = run_menu();
	} else if (!do_settings) {
		if (list_groups) {
			struct category *cat;
			AST_LIST_TRAVERSE(&categories, cat, list) {
				fprintf(stdout, "%s\n", cat->name);
			}
		} else if (list_options) {
			struct category *cat;
			struct member *mem;
			AST_LIST_TRAVERSE(&categories, cat, list) {
				AST_LIST_TRAVERSE(&cat->members, mem, list) {
					if (mem->is_separator) {
						continue;
					}

					fprintf(stdout, "%c %-30.30s %s\n", mem->enabled ? '+' : '-', mem->name, cat->name);
				}
			}
		} else if (!strlen_zero(list_group)) {
			struct category *cat;
			struct member *mem;
			if ((cat = find_category(list_group))) {
				AST_LIST_TRAVERSE(&cat->members, mem, list) {
					if (mem->is_separator) {
						continue;
					}

					fprintf(stdout, "%c %s\n", mem->enabled ? '+' : '-', mem->name);
				}
			}
		}
	} else if (!do_menu && do_settings) {
		struct member *mem;
		struct category *cat;

		print_debug("Doing settings with argc=%d\n", argc);

		/* Reset options processing */
		option_index = 0;
		optind = 1;

		while ((c = getopt_long(argc, argv, "", long_options, &option_index)) != -1) {
			print_debug("Got option %c\n", c);
			switch (c) {
			case 'e':
				if (!strlen_zero(optarg)) {
					if ((mem = find_member(optarg))) {
						set_member_enabled(mem);
					} else {
						fprintf(stderr, "'%s' not found\n", optarg);
					}
				}
				break;
			case 'E':
				if (!strlen_zero(optarg)) {
					if ((cat = find_category(optarg))) {
						set_all(cat, 1);
					} else {
						fprintf(stderr, "'%s' not found\n", optarg);
					}
				}
				break;
			case 'a': /* enable-all */
				AST_LIST_TRAVERSE(&categories, cat, list) {
					set_all(cat, 1);
				}
				break;
			case 'd':
				if (!strlen_zero(optarg)) {
					if ((mem = find_member(optarg))) {
						clear_member_enabled(mem);
					} else {
						fprintf(stderr, "'%s' not found\n", optarg);
					}
				}
				break;
			case 'D':
				if (!strlen_zero(optarg)) {
					if ((cat = find_category(optarg))) {
						set_all(cat, 0);
					} else {
						fprintf(stderr, "'%s' not found\n", optarg);
					}
				}
				break;
			case 'A': /* disable-all */
				AST_LIST_TRAVERSE(&categories, cat, list) {
					set_all(cat, 0);
				}
				break;
			case '?':
				break;
			default:
				break;
			}
		}
		res = 0;
	}

	if (!res) {
		res = generate_makeopts_file();
	}

	/* Always generate the dependencies file */
	if (!res) {
		generate_makedeps_file();
	}

	/* free everything we allocated */
	free_deps_file();
	free_trees();
	free_member_list();

	close_debug();

	xmlCleanupParser();

	exit(res);
}
