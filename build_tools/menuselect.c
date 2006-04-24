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
AST_LIST_HEAD_NOLOCK_STATIC(trees, tree);

const char * const makeopts_files[] = {
	"makeopts.xml"
};

char *output_makeopts = OUTPUT_MAKEOPTS_DEFAULT;

/*! This is set to 1 if menuselect.makeopts pre-existed the execution of this app */
int existing_config = 0;

/*! This is set when the --check-deps argument is provided. */
int check_deps = 0;

/*! Force a clean of the source tree */
int force_clean = 0;

int add_category(struct category *cat);
int add_member(struct member *mem, struct category *cat);
int parse_makeopts_xml(const char *makeopts_xml);
int process_deps(void);
int build_member_list(void);
void mark_as_present(const char *member, const char *category);
int parse_existing_config(const char *infile);
int generate_makeopts_file(void);
void free_member_list(void);
void free_trees(void);

/*! \brief a wrapper for calloc() that generates an error message if the allocation fails */
static inline void *my_calloc(size_t num, size_t len)
{
	void *tmp;

	tmp = calloc(num, len);
	
	if (!tmp)
		fprintf(stderr, "Memory allocation error!\n");

	return tmp;
}

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
int add_category(struct category *cat)
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
int add_member(struct member *mem, struct category *cat)
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
int parse_makeopts_xml(const char *makeopts_xml)
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

	if (!(tree = my_calloc(1, sizeof(*tree)))) {
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
		if (!(cat = my_calloc(1, sizeof(*cat))))
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
			if (!(mem = my_calloc(1, sizeof(*mem))))
				return -1;
			
			if (!cat->positive_output)
				mem->enabled = 1; /* Enabled by default */

			mem->name = mxmlElementGetAttr(cur2, "name");
			
			cur3 = mxmlFindElement(cur2, cur2, "defaultenabled", NULL, NULL, MXML_DESCEND);
			if (cur3 && cur3->child) {
				if (!strcasecmp("no", cur3->child->value.opaque))
					mem->enabled = 0;
				else if (!strcasecmp("yes", cur3->child->value.opaque))
					mem->enabled = 1;
				else
					fprintf(stderr, "Invalid value '%s' for <defaultenabled> !\n", cur3->child->value.opaque);
			}
			
			for (cur3 = mxmlFindElement(cur2, cur2, "depend", NULL, NULL, MXML_DESCEND);
			     cur3 && cur3->child;
			     cur3 = mxmlFindElement(cur3, cur2, "depend", NULL, NULL, MXML_DESCEND))
			{
				if (!(dep = my_calloc(1, sizeof(*dep))))
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
				if (!(cnf = my_calloc(1, sizeof(*cnf))))
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
int process_deps(void)
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
		if (!(dep_file = my_calloc(1, sizeof(*dep_file))))
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
			if (mem->depsfailed) {
				if (check_deps && existing_config && mem->enabled) {
					/* Config already existed, but this module was not disabled.
					 * However, according to our current list of dependencies that
					 * have been met, this can not be built. */
					res = -1;
					fprintf(stderr, "\nThe existing menuselect.makeopts did not specify that %s should not be built\n", mem->name);
					fprintf(stderr, "However, menuselect-deps indicates that dependencies for this module have not\n");
					fprintf(stderr, "been met.  So, either remove the existing menuselect.makeopts file, or run\n");
					fprintf(stderr, "'make menuselect' to generate a file that is correct.\n\n");
					goto deps_file_free;
				}
				mem->enabled = 0; /* Automatically disable it if dependencies not met */
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
			if (mem->conflictsfailed) {
				if (check_deps && existing_config && mem->enabled) {
					/* Config already existed, but this module was not disabled.
					 * However, according to our current list of conflicts that
					 * exist, this can not be built. */
					res = -1;
					fprintf(stderr, "\nThe existing menuselect.makeopts did not specify that %s should not be built\n", mem->name);
					fprintf(stderr, "However, menuselect-deps indicates that conflicts for this module exist.\n");
					fprintf(stderr, "So, either remove the existing menuselect.makeopts file, or run\n");
					fprintf(stderr, "'make menuselect' to generate a file that is correct.\n\n");
					goto deps_file_free;
				}
				mem->enabled = 0; /* Automatically disable it if conflicts exist */
			}
		}
	}

deps_file_free:

	/* Free the dependency list we built from the file */
	while ((dep_file = AST_LIST_REMOVE_HEAD(&deps_file, list)))
		free(dep_file);

	return res;
}

/*! \brief Iterate through all of the input makeopts files and call the parse function on them */
int build_member_list(void)
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
void mark_as_present(const char *member, const char *category)
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

/*! \brief Parse an existing output makeopts file and enable members previously selected */
int parse_existing_config(const char *infile)
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
int generate_makeopts_file(void)
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

	fclose(f);

	return 0;
}

#ifdef MENUSELECT_DEBUG
/*! \brief Print out all of the information contained in our tree */
void dump_member_list(void)
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
void free_member_list(void)
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
void free_trees(void)
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

int main(int argc, char *argv[])
{
	int res = 0;
	unsigned int x;

	/* Parse the input XML files to build the list of available options */
	if ((res = build_member_list()))
		exit(res);

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
		}
	}

	/* Process module dependencies */
	res = process_deps();

#ifdef MENUSELECT_DEBUG
	/* Dump the list produced by parsing the various input files */
	dump_member_list();
#endif

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

	if (force_clean)
		unlink(".lastclean");

	exit(res);
}
