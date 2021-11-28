/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2008 Sean Bright
 *
 * Sean Bright <sean.bright@gmail.com>
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
 * \author Sean Bright <sean.bright@gmail.com>
 *
 * \brief newt frontend for selection maintenance
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <newt.h>

#include "menuselect.h"

#define MIN_X 80
#define MIN_Y 21

#define MIN(a, b) ({ typeof(a) __a = (a); typeof(b) __b = (b); ((__a > __b) ? __b : __a);})
#define MAX(a, b) ({ typeof(a) __a = (a); typeof(b) __b = (b); ((__a < __b) ? __b : __a);})

extern int changes_made;

static newtComponent rootOptions;
static newtComponent subOptions;

static newtComponent memberNameTextbox;
static newtComponent dependsLabel;
static newtComponent usesLabel;
static newtComponent conflictsLabel;
static newtComponent supportLevelLabel;
static newtComponent dependsDataTextbox;
static newtComponent usesDataTextbox;
static newtComponent conflictsDataTextbox;
static newtComponent supportLevelDataTextbox;

static newtComponent exitButton;
static newtComponent saveAndExitButton;

static void build_members_menu(int overlay);
static void root_menu_callback(newtComponent component, void *data);

static void toggle_all_options(int select)
{
	struct category *cat = newtListboxGetCurrent(rootOptions);

	set_all(cat, select);

	/* Redraw */
	build_members_menu(1);

	return;
}

static void toggle_selected_option()
{
	int i;
	struct member *mem = newtListboxGetCurrent(subOptions);

	toggle_enabled(mem);

	/* Redraw */
	build_members_menu(1);

	/* Select the next item in the list */
	for (i = 0; i < newtListboxItemCount(subOptions); i++) {
		struct member *cur;

		newtListboxGetEntry(subOptions, i, NULL, (void **) &cur);

		if (cur == mem) {
			i = MIN(i + 1, newtListboxItemCount(subOptions) - 1);
			break;
		}
	}

	newtListboxSetCurrent(subOptions, i);

	return;
}

static void reset_display()
{
	newtTextboxSetText(memberNameTextbox, "");
	newtTextboxSetText(dependsDataTextbox, "");
	newtTextboxSetText(usesDataTextbox, "");
	newtTextboxSetText(conflictsDataTextbox, "");
	newtTextboxSetText(supportLevelDataTextbox, "");
	newtRefresh();
}

static void display_member_info(struct member *mem)
{
	char buffer[128] = { 0 };
	char buf2[64];

	struct reference *dep;
	struct reference *con;
	struct reference *uses;

	reset_display();

	if (mem->displayname) {
		newtTextboxSetText(memberNameTextbox, mem->displayname);
	}

	if (AST_LIST_EMPTY(&mem->deps)) {
		if (mem->is_separator) {
			newtTextboxSetText(dependsDataTextbox, "");
		} else {
			newtTextboxSetText(dependsDataTextbox, "N/A");
		}
	} else {
		strcpy(buffer, "");
		AST_LIST_TRAVERSE(&mem->deps, dep, list) {
			strncat(buffer, dep->displayname, sizeof(buffer) - strlen(buffer) - 1);
			strncat(buffer, dep->member ? "(M)" : "(E)", sizeof(buffer) - strlen(buffer) - 1);
			if (AST_LIST_NEXT(dep, list))
				strncat(buffer, ", ", sizeof(buffer) - strlen(buffer) - 1);
		}
		newtTextboxSetText(dependsDataTextbox, buffer);
	}

	if (AST_LIST_EMPTY(&mem->uses)) {
		if (mem->is_separator) {
			newtTextboxSetText(usesDataTextbox, "");
		} else {
			newtTextboxSetText(usesDataTextbox, "N/A");
		}
	} else {
		strcpy(buffer, "");
		AST_LIST_TRAVERSE(&mem->uses, uses, list) {
			strncat(buffer, uses->displayname, sizeof(buffer) - strlen(buffer) - 1);
			strncat(buffer, uses->member ? "(M)" : "(E)", sizeof(buffer) - strlen(buffer) - 1);
			if (AST_LIST_NEXT(uses, list))
				strncat(buffer, ", ", sizeof(buffer) - strlen(buffer) - 1);
		}
		newtTextboxSetText(usesDataTextbox, buffer);
	}

	if (AST_LIST_EMPTY(&mem->conflicts)) {
		if (!mem->is_separator) {
			newtTextboxSetText(conflictsDataTextbox, "N/A");
		} else {
			newtTextboxSetText(conflictsDataTextbox, "");
		}
	} else {
		strcpy(buffer, "");
		AST_LIST_TRAVERSE(&mem->conflicts, con, list) {
			strncat(buffer, con->displayname, sizeof(buffer) - strlen(buffer) - 1);
			strncat(buffer, con->member ? "(M)" : "(E)", sizeof(buffer) - strlen(buffer) - 1);
			if (AST_LIST_NEXT(con, list))
				strncat(buffer, ", ", sizeof(buffer) - strlen(buffer) - 1);
		}
		newtTextboxSetText(conflictsDataTextbox, buffer);
	}

	{ /* Support Level */
		snprintf(buffer, sizeof(buffer), "%s", mem->support_level);
		if (mem->replacement && *mem->replacement) {
			snprintf(buf2, sizeof(buf2), ", Replaced by: %s", mem->replacement);
			strncat(buffer, buf2, sizeof(buffer) - strlen(buffer) - 1);
		}
		if (mem->deprecated_in && *mem->deprecated_in) {
			snprintf(buf2, sizeof(buf2), ", Deprecated in: %s", mem->deprecated_in);
			strncat(buffer, buf2, sizeof(buffer) - strlen(buffer) - 1);
		}
		if (mem->removed_in && *mem->removed_in) {
			snprintf(buf2, sizeof(buf2), ", Removed in: %s", mem->removed_in);
			strncat(buffer, buf2, sizeof(buffer) - strlen(buffer) - 1);
		}
		if (mem->is_separator) {
			newtTextboxSetText(supportLevelDataTextbox, "");
		} else {
			newtTextboxSetText(supportLevelDataTextbox, buffer);
		}
	}
}

static void build_members_menu(int overlay)
{
	struct category *cat;
	struct member *mem;
	char buf[64];
	int i = 0;

	if (!overlay) {
		reset_display();
		newtListboxClear(subOptions);
	}

	cat = newtListboxGetCurrent(rootOptions);

	AST_LIST_TRAVERSE(&cat->members, mem, list) {

		if ((mem->depsfailed == HARD_FAILURE) || (mem->conflictsfailed == HARD_FAILURE)) {
			snprintf(buf, sizeof(buf), "XXX %s", mem->name);
		} else if (mem->is_separator) {
			snprintf(buf, sizeof(buf), "    --- %s ---", mem->name);
		} else if (mem->depsfailed == SOFT_FAILURE) {
			snprintf(buf, sizeof(buf), "<%s> %s", mem->enabled ? "*" : " ", mem->name);
		} else if (mem->conflictsfailed == SOFT_FAILURE) {
			snprintf(buf, sizeof(buf), "(%s) %s", mem->enabled ? "*" : " ", mem->name);
		} else {
			snprintf(buf, sizeof(buf), "[%s] %s", mem->enabled ? "*" : " ", mem->name);
		}

		if (overlay) {
			newtListboxSetEntry(subOptions, i, buf);
		} else {
			newtListboxAppendEntry(subOptions, buf, mem);
		}

		i++;
	}

	if (!overlay) {
		display_member_info(AST_LIST_FIRST(&cat->members));
	}

	return;
}

static void build_main_menu()
{
	struct category *cat;
	char buf[64];
	int i = 1;

	newtListboxClear(rootOptions);

	AST_LIST_TRAVERSE(&categories, cat, list) {
		if (!strlen_zero(cat->displayname))
			snprintf(buf, sizeof(buf), " %s ", cat->displayname);
		else
			snprintf(buf, sizeof(buf), " %s ", cat->name);

		newtListboxAppendEntry(rootOptions, buf, cat);

		i++;
	}
}

static void category_menu_callback(newtComponent component, void *data)
{
	display_member_info(newtListboxGetCurrent(subOptions));
}

static void root_menu_callback(newtComponent component, void *data)
{
	build_members_menu(0);
}

int run_confirmation_dialog(int *result)
{
	int res = newtWinTernary("Are You Sure?", "Discard changes & Exit", "Save & Exit", "Cancel",
				   "It appears you have made some changes, and you have opted to Quit "
				   "without saving these changes.  Please choose \"Discard changes & Exit\" to exit "
				   "without saving; Choose \"Cancel\" to cancel your decision to quit, and keep "
				   "working in menuselect, or choose \"Save & Exit\" to save your changes, and exit.");

	switch (res) {
	case 1:
		/* Discard and exit */
		*result = -1;
		return 1;
	case 2:
		/* Save and exit */
		*result = 0;
		return 1;
	case 3:
		/* They either chose "No" or they hit F12 */
	default:
		*result = -1;
		return 0;
	}
}

int run_menu(void)
{
	struct newtExitStruct es;
	newtComponent form;
	int x = 0, y = 0, res = 0;

	newtInit();
	newtCls();
	newtGetScreenSize(&x, &y);

	if (x < MIN_X || y < MIN_Y) {
		newtFinished();
		fprintf(stderr, "Terminal must be at least %d x %d.\n", MIN_X, MIN_Y);
		return -1;
	}

	newtPushHelpLine("  <ENTER> toggles selection | <F12> saves & exits | <ESC> exits without save");
	newtRefresh();

	newtCenteredWindow(x - 8, y - 7, menu_name);
	form = newtForm(NULL, NULL, 0);

	/* F8 for select all */
	newtFormAddHotKey(form, NEWT_KEY_F8);

	/* F7 for deselect all */
	newtFormAddHotKey(form, NEWT_KEY_F7);

	newtFormSetTimer(form, 200);

	rootOptions = newtListbox(2, 1, y - 15, 0);
	newtListboxSetWidth(rootOptions, 34);
	newtFormAddComponent(form, rootOptions);
	newtComponentAddCallback(rootOptions, root_menu_callback, NULL);

	subOptions = newtListbox(38, 1, y - 15, NEWT_FLAG_SCROLL | NEWT_FLAG_RETURNEXIT);
	newtListboxSetWidth(subOptions, x - 47);
	newtFormAddComponent(form, subOptions);
	newtComponentAddCallback(subOptions, category_menu_callback, NULL);

	memberNameTextbox       = newtTextbox(2, y - 13, x - 10, 2, NEWT_FLAG_WRAP);
	dependsLabel            = newtLabel(2, y - 11, "    Depends on:");
	usesLabel               = newtLabel(2, y - 10, "       Can use:");
	conflictsLabel          = newtLabel(2, y - 9,  "Conflicts with:");
	supportLevelLabel       = newtLabel(2, y - 8,  " Support Level:");
	dependsDataTextbox      = newtTextbox(18, y - 11, x - 27, 1, 0);
	usesDataTextbox         = newtTextbox(18, y - 10, x - 27, 1, 0);
	conflictsDataTextbox    = newtTextbox(18, y - 9, x - 27, 1, 0);
	supportLevelDataTextbox = newtTextbox(18, y - 8, x - 27, 1, 0);

	exitButton = newtButton(x - 23, y - 11, "  Exit  ");
	saveAndExitButton = newtButton(x - 43, y - 11, " Save & Exit ");

	newtFormAddComponents(
		form,
		memberNameTextbox,
		dependsLabel,
		dependsDataTextbox,
		usesLabel,
		usesDataTextbox,
		conflictsLabel,
		conflictsDataTextbox,
		supportLevelLabel,
		supportLevelDataTextbox,
		saveAndExitButton,
		exitButton,
		NULL);

	build_main_menu();

	root_menu_callback(rootOptions, AST_LIST_FIRST(&categories));

	for (;;) {
		do {
			newtFormRun(form, &es);
		} while (es.reason == NEWT_EXIT_TIMER);

		if (es.reason == NEWT_EXIT_HOTKEY) {
			int done = 1;

			switch (es.u.key) {
			case NEWT_KEY_F12:
				res = 0;
				break;
			case NEWT_KEY_F7:
				toggle_all_options(0);
				done = 0;
				break;
			case NEWT_KEY_F8:
				toggle_all_options(1);
				done = 0;
				break;
			case NEWT_KEY_ESCAPE:
				if (changes_made) {
					done = run_confirmation_dialog(&res);
				} else {
					res = -1;
				}
				break;
			default:
				done = 0;
				break;
			}

			if (done) {
				break;
			}
		} else if (es.reason == NEWT_EXIT_COMPONENT) {
			if (es.u.co == saveAndExitButton) {
				res = 0;
				break;
			} else if (es.u.co == exitButton) {
				int done = 1;

				if (changes_made) {
					done = run_confirmation_dialog(&res);
				} else {
					res = -1;
				}

				if (done) {
					break;
				}
			} else if (es.u.co == subOptions) {
				toggle_selected_option();
			}
		}
	}

	/* Cleanup */
	reset_display();
	newtFormDestroy(form);
	newtPopWindow();
	newtPopHelpLine();
	newtCls();
	newtFinished();

	return res;
}
