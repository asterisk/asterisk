/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2005 - 2006, Russell Bryant
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

/*
 * \file
 *
 * \author Russell Bryant <russell@digium.com>
 * 
 * \brief A menu-driven system for Asterisk module selection
 */

#include "autoconfig.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "mxml/mxml.h"
#include "menuselect.h"

#include "asterisk.h"

#include "asterisk/linkedlists.h"

#undef MENUSELECT_DEBUG

struct depend {
	/*! the name of the dependency */
	const char *name;
	/*! for linking */
	AST_LIST_ENTRY(depend) list;
};

struct conflict {
	/*! the name of the conflict */
	const char *name;
	/*! for linking */
	AST_LIST_ENTRY(conflict) list;
};

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
	mxml_node_t *root;
	/*! for linking */
	AST_LIST_ENTRY(tree) list;
};

/*! The list of trees from makeopts.xml files */
static AST_LIST_HEAD_NOLOCK_STATIC(trees, tree);

static const char * const makeopts_files[] = {
	"makeopts.xml"
};

static char *output_makeopts = OUTPUT_MAKEOPTS_DEFAULT;

/*! This is set to 1 if menuselect.makeopts pre-existed the execution of this app */
static int existing_config = 0;

/*! This is set when the --check-deps argument is provided. */
static int check_deps = 0;

/*! Force a clean of the source tree */
static int force_clean = 0;

static int add_category(struct category *cat);
static int add_member(struct member *mem, struct category *cat);
static int parse_makeopts_xml(const char *makeopts_xml);
static int process_deps(void);
static int build_member_list(void);
static void mark_as_present(const char *member, const char *category);
static void process_prev_failed_deps(char *buf);
static int parse_existing_config(const char *infile);
static int generate_makeopts_file(void);
static void free_member_list(void);
static void free_trees(void);

/*! \brief return a pointer to the first non-whitespace character */
static inline char *skip_blanks(char *str)
{
	if (!str)
		return NULL;

	while (*str && *str < 33)
		str++;

	return str;
}

/*! \brief Add a category to the category list, ensuring that there are no duplicates */
static int add_category(struct category *cat)
{
	struct category *tmp;

	AST_LIST_TRAVERSE(&categories, tmp, list) {
		if (!strcmp(tmp->name, cat->name)) {
			fprintf(stderr, "Category '%s' specified more than once!\n", cat->name);
			return -1;
		}
	}
	AST_LIST_INSERT_TAIL(&categories, cat, list);

	return 0;
}

/*! \brief Add a member to the member list of a category, ensuring that there are no duplicates */
static int add_member(struct member *mem, struct category *cat)
{
	struct member *tmp;

	AST_LIST_TRAVERSE(&cat->members, tmp, list) {
		if (!strcmp(tmp->name, mem->name)) {
			fprintf(stderr, "Member '%s' already exists in category '%s', ignoring.\n", mem->name, cat->name);
			return -1;
		}
	}
	AST_LIST_INSERT_TAIL(&cat->members, mem, list);

	return 0;
}

/*! \brief Parse an input makeopts file */
static int parse_makeopts_xml(const char *makeopts_xml)
{
	FILE *f;
	struct category *cat;
	struct tree *tree;
	struct member *mem;
	struct depend *dep;
	struct conflict *cnf;
	mxml_node_t *cur;
	mxml_node_t *cur2;
	mxml_node_t *cur3;
	mxml_node_t *menu;
	const char *tmp;

	if (!(f = fopen(makeopts_xml, "r"))) {
		fprintf(stderr, "Unable to open '%s' for reading!\n", makeopts_xml);
		return -1;
	}

	if (!(tree = calloc(1, sizeof(*tree)))) {
		fclose(f);
		return -1;
	}

	if (!(tree->root = mxmlLoadFile(NULL, f, MXML_OPAQUE_CALLBACK))) {
		fclose(f);
		free(tree);
		return -1;
	}

	AST_LIST_INSERT_HEAD(&trees, tree, list);

	menu = mxmlFindElement(tree->root, tree->root, "menu", NULL, NULL, MXML_DESCEND);
	for (cur = mxmlFindElement(menu, menu, "category", NULL, NULL, MXML_DESCEND);
	     cur;
	     cur = mxmlFindElement(cur, menu, "category", NULL, NULL, MXML_DESCEND))
	{
		if (!(cat = calloc(1, sizeof(*cat))))
			return -1;

		cat->name = mxmlElementGetAttr(cur, "name");
		cat->displayname = mxmlElementGetAttr(cur, "displayname");
		if ((tmp = mxmlElementGetAttr(cur, "positive_output")))
			cat->positive_output = !strcasecmp(tmp, "yes");
		if ((tmp = mxmlElementGetAttr(cur, "force_clean_on_change")))
			cat->force_clean_on_change = !strcasecmp(tmp, "yes");

		if (add_category(cat)) {
			free(cat);
			continue;
		}

		for (cur2 = mxmlFindElement(cur, cur, "member", NULL, NULL, MXML_DESCEND);
		     cur2;
		     cur2 = mxmlFindElement(cur2, cur, "member", NULL, NULL, MXML_DESCEND))
		{
			if (!(mem = calloc(1, sizeof(*mem))))
				return -1;
			
			mem->name = mxmlElementGetAttr(cur2, "name");
		
			if (!cat->positive_output)
				mem->enabled = 1;
	
			cur3 = mxmlFindElement(cur2, cur2, "defaultenabled", NULL, NULL, MXML_DESCEND);
			if (cur3 && cur3->child)
				mem->defaultenabled = cur3->child->value.opaque;
			
			for (cur3 = mxmlFindElement(cur2, cur2, "depend", NULL, NULL, MXML_DESCEND);
			     cur3 && cur3->child;
			     cur3 = mxmlFindElement(cur3, cur2, "depend", NULL, NULL, MXML_DESCEND))
			{
				if (!(dep = calloc(1, sizeof(*dep))))
					return -1;
				if (!strlen_zero(cur3->child->value.opaque)) {
					dep->name = cur3->child->value.opaque;
					AST_LIST_INSERT_HEAD(&mem->deps, dep, list);
				} else
					free(dep);
			}

			for (cur3 = mxmlFindElement(cur2, cur2, "conflict", NULL, NULL, MXML_DESCEND);
			     cur3 && cur3->child;
			     cur3 = mxmlFindElement(cur3, cur2, "conflict", NULL, NULL, MXML_DESCEND))
			{
				if (!(cnf = calloc(1, sizeof(*cnf))))
					return -1;
				if (!strlen_zero(cur3->child->value.opaque)) {
					cnf->name = cur3->child->value.opaque;
					AST_LIST_INSERT_HEAD(&mem->conflicts, cnf, list);
				} else
					free(cnf);
			}

			if (add_member(mem, cat))
				free(mem);
		}
	}

	fclose(f);

	return 0;
}

/*! \brief Process dependencies against the input dependencies file */
static int process_deps(void)
{
	struct category *cat;
	struct member *mem;
	struct depend *dep;
	struct conflict *cnf;
	FILE *f;
	struct dep_file {
		char name[32];
		int met;
		AST_LIST_ENTRY(dep_file) list;
	} *dep_file;
	AST_LIST_HEAD_NOLOCK_STATIC(deps_file, dep_file);
	char buf[80];
	char *p;
	int res = 0;

	if (!(f = fopen(MENUSELECT_DEPS, "r"))) {
		fprintf(stderr, "Unable to open '%s' for reading!  Did you run ./configure ?\n", MENUSELECT_DEPS);
		return -1;
	}

	/* Build a dependency list from the file generated by configure */	
	while (memset(buf, 0, sizeof(buf)), fgets(buf, sizeof(buf), f)) {
		p = buf;
		strsep(&p, "=");
		if (!p)
			continue;
		if (!(dep_file = calloc(1, sizeof(*dep_file))))
			break;
		strncpy(dep_file->name, buf, sizeof(dep_file->name) - 1);
		dep_file->met = atoi(p);
		AST_LIST_INSERT_TAIL(&deps_file, dep_file, list);
	}

	fclose(f);

	/* Process dependencies of all modules */
	AST_LIST_TRAVERSE(&categories, cat, list) {
		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			AST_LIST_TRAVERSE(&mem->deps, dep, list) {
				mem->depsfailed = 1;
				AST_LIST_TRAVERSE(&deps_file, dep_file, list) {
					if (!strcasecmp(dep_file->name, dep->name)) {
						if (dep_file->met)
							mem->depsfailed = 0;
						break;
					}
				}
				if (mem->depsfailed)
					break; /* This dependency is not met, so we can stop now */
			}
		}
	}

	/* Process conflicts of all modules */
	AST_LIST_TRAVERSE(&categories, cat, list) {
		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			AST_LIST_TRAVERSE(&mem->conflicts, cnf, list) {
				mem->conflictsfailed = 0;
				AST_LIST_TRAVERSE(&deps_file, dep_file, list) {
					if (!strcasecmp(dep_file->name, cnf->name)) {
						if (dep_file->met)
							mem->conflictsfailed = 1;
						break;
					}
				}
				if (mem->conflictsfailed)
					break; /* This conflict was found, so we can stop now */
			}
		}
	}

	/* Free the dependency list we built from the file */
	while ((dep_file = AST_LIST_REMOVE_HEAD(&deps_file, list)))
		free(dep_file);

	return res;
}

/*! \brief Iterate through all of the input makeopts files and call the parse function on them */
static int build_member_list(void)
{
	int i;
	int res = -1;

	for (i = 0; i < (sizeof(makeopts_files) / sizeof(makeopts_files[0])); i++) {
		if ((res = parse_makeopts_xml(makeopts_files[i]))) {
			fprintf(stderr, "Error parsing '%s'!\n", makeopts_files[i]);
			break;
		}
	}

	return res;
}

/*! \brief Given the string representation of a member and category, mark it as present in a given input file */
static void mark_as_present(const char *member, const char *category)
{
	struct category *cat;
	struct member *mem;

	AST_LIST_TRAVERSE(&categories, cat, list) {
		if (strcmp(category, cat->name))
			continue;
		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			if (!strcmp(member, mem->name)) {
				mem->enabled = cat->positive_output;
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

/*! \brief Toggle a member of a category at the specified index to enabled/disabled */
void toggle_enabled(struct category *cat, int index)
{
	struct member *mem;
	int i = 0;

	AST_LIST_TRAVERSE(&cat->members, mem, list) {
		if (i++ == index)
			break;
	}

	if (mem && !(mem->depsfailed || mem->conflictsfailed)) {
		mem->enabled = !mem->enabled;
		if (cat->force_clean_on_change)
			force_clean = 1;
	}
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
			if (strcasecmp(mem->name, mem_name))
				continue;

			if (!mem->depsfailed && !mem->conflictsfailed)
				mem->enabled = 1;			
	
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
	char buf[2048];
	char *category, *parse, *member;
	int lineno = 0;

	if (!(f = fopen(infile, "r"))) {
#ifdef MENUSELECT_DEBUG
		/* This isn't really an error, so only print the message in debug mode */
		fprintf(stderr, "Unable to open '%s' for reading existing config.\n", infile);
#endif	
		return -1;
	}

	while (fgets(buf, sizeof(buf), f)) {
		lineno++;

		if (strlen_zero(buf))
			continue;

		/* skip lines that are not for this tool */
		if (strncasecmp(buf, "MENUSELECT_", strlen("MENUSELECT_")))
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

	fclose(f);

	return 0;
}

/*! \brief Create the output makeopts file that results from the user's selections */
static int generate_makeopts_file(void)
{
	FILE *f;
	struct category *cat;
	struct member *mem;

	if (!(f = fopen(output_makeopts, "w"))) {
		fprintf(stderr, "Unable to open build configuration file (%s) for writing!\n", output_makeopts);
		return -1;
	}

	/* Traverse all categories and members and output them as var/val pairs */
	AST_LIST_TRAVERSE(&categories, cat, list) {
		fprintf(f, "%s=", cat->name);
		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			if ((!cat->positive_output && (!mem->enabled || mem->depsfailed || mem->conflictsfailed)) ||
			    (cat->positive_output && mem->enabled && !mem->depsfailed && !mem->conflictsfailed))
				fprintf(f, "%s ", mem->name);
		}
		fprintf(f, "\n");
	}

	/* Output which members were disabled because of failed dependencies or conflicts */
	AST_LIST_TRAVERSE(&categories, cat, list) {
		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			if (mem->depsfailed || mem->conflictsfailed)
				fprintf(f, "MENUSELECT_DEPSFAILED=%s=%s\n", cat->name, mem->name);
		}
	}

	fclose(f);

	return 0;
}

#ifdef MENUSELECT_DEBUG
/*! \brief Print out all of the information contained in our tree */
static void dump_member_list(void)
{
	struct category *cat;
	struct member *mem;
	struct depend *dep;
	struct conflict *cnf;

	AST_LIST_TRAVERSE(&categories, cat, list) {
		fprintf(stderr, "Category: '%s'\n", cat->name);
		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			fprintf(stderr, "   ==>> Member: '%s'  (%s)\n", mem->name, mem->enabled ? "Enabled" : "Disabled");
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
}
#endif

/*! \brief Free all categories and their members */
static void free_member_list(void)
{
	struct category *cat;
	struct member *mem;
	struct depend *dep;
	struct conflict *cnf;

	while ((cat = AST_LIST_REMOVE_HEAD(&categories, list))) {
		while ((mem = AST_LIST_REMOVE_HEAD(&cat->members, list))) {
			while ((dep = AST_LIST_REMOVE_HEAD(&mem->deps, list)))
				free(dep);
			while ((cnf = AST_LIST_REMOVE_HEAD(&mem->conflicts, list)))
				free(cnf);
			free(mem);
		}
		free(cat);
	}
}

/*! \brief Free all of the XML trees */
static void free_trees(void)
{
	struct tree *tree;

	while ((tree = AST_LIST_REMOVE_HEAD(&trees, list))) {
		mxmlDelete(tree->root);
		free(tree);
	}
}

/*! \brief Enable/Disable all members of a category as long as dependencies have been met and no conflicts are found */
void set_all(struct category *cat, int val)
{
	struct member *mem;

	AST_LIST_TRAVERSE(&cat->members, mem, list) {
		if (!(mem->depsfailed || mem->conflictsfailed))
			mem->enabled = val;
	}
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

/*! \brief Make sure an existing menuselect.makeopts disabled everything it should have */
static int sanity_check(void)
{
	struct category *cat;
	struct member *mem;

	AST_LIST_TRAVERSE(&categories, cat, list) {
		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			if ((mem->depsfailed || mem->conflictsfailed) && mem->enabled) {
				fprintf(stderr, "\n***********************************************************\n"
				                "  The existing menuselect.makeopts file did not specify    \n"
				                "  that '%s' should not be included.  However, either some  \n"
				                "  dependencies for this module were not found or a         \n"
				                "  conflict exists.                                         \n"
				                "                                                           \n"
				                "  Either run 'make menuselect' or remove the existing      \n"
				                "  menuselect.makeopts file to resolve this issue.          \n"
						"***********************************************************\n\n", mem->name);
				return -1;
			}
		}
	}
}

/* \brief Set the forced default values if they exist */
static void process_defaults(void)
{
	struct category *cat;
	struct member *mem;

	AST_LIST_TRAVERSE(&categories, cat, list) {
		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			if (!mem->defaultenabled)
				continue;
			
			if (!strcasecmp(mem->defaultenabled, "yes"))
				mem->enabled = 1;
			else if (!strcasecmp(mem->defaultenabled, "no"))
				mem->enabled = 0;
			else
				fprintf(stderr, "Invalid defaultenabled value for '%s' in category '%s'\n", mem->name, cat->name);	
		}
	}

}

int main(int argc, char *argv[])
{
	int res = 0;
	unsigned int x;

	/* Parse the input XML files to build the list of available options */
	if ((res = build_member_list()))
		exit(res);
	
	/* Process module dependencies */
	res = process_deps();
	
	/* The --check-deps option is used to ask this application to check to
	 * see if that an existing menuselect.makeopts file contails all of the
	 * modules that have dependencies that have not been met.  If this
	 * is not the case, an informative message will be printed to the
	 * user and the build will fail. */
	for (x = 1; x < argc; x++) {
		if (!strcmp(argv[x], "--check-deps"))
			check_deps = 1;
		else {
			res = parse_existing_config(argv[x]);
			if (!res && !strcasecmp(argv[x], OUTPUT_MAKEOPTS_DEFAULT))
				existing_config = 1;
			res = 0;
		}
	}

#ifdef MENUSELECT_DEBUG
	/* Dump the list produced by parsing the various input files */
	dump_member_list();
#endif

	if (!existing_config)
		process_defaults();
	else if (check_deps)
		res = sanity_check();

	/* Run the menu to let the user enable/disable options */
	if (!check_deps && !res)
		res = run_menu();

	/* Write out the menuselect.makeopts file if
	 * 1) menuselect was not executed with --check-deps
	 * 2) menuselect was executed with --check-deps but menuselect.makeopts
	 *    did not already exist.
	 */
	if ((!check_deps || !existing_config) && !res)
		res = generate_makeopts_file();
	
	/* free everything we allocated */
	free_trees();
	free_member_list();

	if (check_deps && !existing_config && !res) {
		fprintf(stderr, "\n***********************************************************\n");
		fprintf(stderr, "* menuselect.makeopts file generated with default values! *\n");
		fprintf(stderr, "* Please rerun make to build Asterisk.                    *\n");
		fprintf(stderr, "***********************************************************\n\n");
		res = -1;
	}

	/* In some cases, such as modifying the CFLAGS for the build,
	 * a "make clean" needs to be forced.  Removing the .lastclean 
	 * file does this. */
	if (force_clean)
		unlink(".lastclean");

	exit(res);
}
