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
 * \brief curses frontend for Asterisk module selection
 */

#include "autoconfig.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <curses.h>

#include "menuselect.h"

#define MENU_TITLE1	"*************************************"
#define MENU_TITLE2	"*     Asterisk Module Selection     *"
#define MENU_TITLE3	"*************************************"

#define TITLE_HEIGHT	5

#define MIN_X		80
#define MIN_Y		20

#define PAGE_OFFSET	10


/*! Maximum number of characters horizontally */
int max_x = 0;
/*! Maximum number of characters vertically */
int max_y = 0;

const char * const help_info[] = {
	"scroll        => up/down arrows",
	"(de)select    => Enter",
	"select all    => F8",
	"deselect all  => F7",
	"back          => left arrow",
	"quit          => q",
	"save and quit => x",
	"",
	"XXX means dependencies have not been met"
};

void winch_handler(int sig);
void show_help(WINDOW *win);
void draw_main_menu(WINDOW *menu, int curopt);
void draw_category_menu(WINDOW *menu, struct category *cat, int start, int end, int curopt);
int run_category_menu(WINDOW *menu, int cat_num);
int run_category_menu(WINDOW *menu, int cat_num);
void draw_title_window(WINDOW *title);

/*! \brief Handle a window resize in xterm */
void winch_handler(int sig)
{
	getmaxyx(stdscr, max_y, max_x);

	if (max_x < MIN_X - 1 || max_y < MIN_Y - 1) {
		fprintf(stderr, "Terminal must be at least 80 x 25.\n");
		max_x = MIN_X - 1;
		max_y = MIN_Y - 1;
	}
}

/*! \brief Display help information */
void show_help(WINDOW *win)
{
	int i;

	wclear(win);
	for (i = 0; i < (sizeof(help_info) / sizeof(help_info[0])); i++) {
		wmove(win, i, max_x / 2 - 15);
		waddstr(win, help_info[i]);
	}
	wrefresh(win);
	getch(); /* display the help until the user hits a key */
}

void draw_main_menu(WINDOW *menu, int curopt)
{
	struct category *cat;
	char buf[64];
	int i = 0;

	wclear(menu);

	AST_LIST_TRAVERSE(&categories, cat, list) {
		wmove(menu, i++, max_x / 2 - 10);
		if (!strlen_zero(cat->displayname))
			snprintf(buf, sizeof(buf), "%d.%s %s", i, i < 10 ? " " : "", cat->displayname);
		else
			snprintf(buf, sizeof(buf), "%d.%s %s", i, i < 10 ? " " : "", cat->name);
		waddstr(menu, buf);
	}

	wmove(menu, curopt, (max_x / 2) - 15);
	waddstr(menu, "--->");
	wmove(menu, 0, 0);

	wrefresh(menu);
}

void draw_category_menu(WINDOW *menu, struct category *cat, int start, int end, int curopt)
{
	int i = 0;
	int j = 0;
	struct member *mem;
	char buf[64];

	wclear(menu);

	AST_LIST_TRAVERSE(&cat->members, mem, list) {
		if (i < start) {
			i++;
			continue;
		}
		wmove(menu, j++, max_x / 2 - 10);
		i++;
		if (mem->depsfailed)
			snprintf(buf, sizeof(buf), "XXX %d.%s %s", i, i < 10 ? " " : "", mem->name);
		else
			snprintf(buf, sizeof(buf), "[%s] %d.%s %s", mem->enabled ? "*" : " ", i, i < 10 ? " " : "", mem->name);
		waddstr(menu, buf);
		if (i == end)
			break;
	}

	wmove(menu, curopt - start, max_x / 2 - 9);

	wrefresh(menu);
}

int run_category_menu(WINDOW *menu, int cat_num)
{
	struct category *cat;
	int i = 0;
	int start = 0;
	int end = max_y - TITLE_HEIGHT - 2;
	int c;
	int curopt = 0;
	int maxopt;

	AST_LIST_TRAVERSE(&categories, cat, list) {
		if (i++ == cat_num)
			break;
	}
	if (!cat)
		return -1;

	maxopt = count_members(cat) - 1;

	draw_category_menu(menu, cat, start, end, curopt);

	while ((c = getch())) {
		switch (c) {
		case KEY_UP:
			if (curopt > 0) {
				curopt--;
				if (curopt < start) {
					start--;
					end--;
				}
			}
			break;
		case KEY_DOWN:
			if (curopt < maxopt) {
				curopt++;
				if (curopt > end - 1) {
					start++;
					end++;
				}
			}
			break;
		case KEY_NPAGE:
			/* XXX Move down the list by PAGE_OFFSET */
			break;
		case KEY_PPAGE:
			/* XXX Move up the list by PAGE_OFFSET */
			break;
		case KEY_LEFT:
			return 0;
		case KEY_RIGHT:
		case KEY_ENTER:
		case '\n':
		case ' ':
			toggle_enabled(cat, curopt);
			break;
		case 'h':
		case 'H':
			show_help(menu);
			break;
		case KEY_F(7):
			set_all(cat, 0);
			break;
		case KEY_F(8):
			set_all(cat, 1);
		default:
			break;	
		}
		if (c == 'x' || c == 'q')
			break;	
		draw_category_menu(menu, cat, start, end, curopt);
	}

	wrefresh(menu);

	return c;
}

void draw_title_window(WINDOW *title)
{
	wmove(title, 1, (max_x / 2) - (strlen(MENU_TITLE1) / 2));
	waddstr(title, MENU_TITLE1);
	wmove(title, 2, (max_x / 2) - (strlen(MENU_TITLE2) / 2));
	waddstr(title, MENU_TITLE2);
	wmove(title, 3, (max_x / 2) - (strlen(MENU_TITLE3) / 2));
	waddstr(title, MENU_TITLE3);
	wmove(title, 0, 0);
	waddstr(title, "Press 'h' for help");
	wrefresh(title);
}



int run_menu(void)
{
	WINDOW *title;
	WINDOW *menu;
	int maxopt;
	int curopt = 0;
	int c;
	int res = 0;

	initscr();
	getmaxyx(stdscr, max_y, max_x);
	signal(SIGWINCH, winch_handler); /* handle window resizing in xterm */

	if (max_x < MIN_X - 1 || max_y < MIN_Y - 1) {
		fprintf(stderr, "Terminal must be at least %d x %d.\n", MIN_X, MIN_Y);
		endwin();
		return -1;
	}

	cbreak(); /* don't buffer input until the enter key is pressed */
	noecho(); /* don't echo user input to the screen */
	keypad(stdscr, TRUE); /* allow the use of arrow keys */
	clear();
	refresh();

	maxopt = count_categories() - 1;
	
	/* We have two windows - the title window at the top, and the menu window gets the rest */
	title = newwin(TITLE_HEIGHT, max_x, 0, 0);
	menu = newwin(max_y - TITLE_HEIGHT, max_x, TITLE_HEIGHT, 0);
	draw_title_window(title);	
	draw_main_menu(menu, curopt);
	
	while ((c = getch())) {
		switch (c) {
		case KEY_UP:
			if (curopt > 0)
				curopt--;
			break;
		case KEY_DOWN:
			if (curopt < maxopt)
				curopt++;
			break;
		case KEY_RIGHT:
		case KEY_ENTER:
		case '\n':
		case ' ':
			c = run_category_menu(menu, curopt);
			break;
		case 'h':
		case 'H':
			show_help(menu);
		default:
			break;	
		}
		if (c == 'q') {
			res = -1;
			break;
		}
		if (c == 'x')
			break;	
		draw_main_menu(menu, curopt);
	}

	endwin();

	return res;
}
