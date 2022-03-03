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

/*!
 * \file
 *
 * \author Russell Bryant <russell@digium.com>
 *
 * \brief curses frontend for selection maintenance
 */

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#ifdef HAVE_NCURSES
#ifdef HAVE_NCURSES_SUBDIR
#include <ncurses/ncurses.h>
#else
#include <ncurses.h>
#endif
#else
#include <curses.h>
#endif

#include "menuselect.h"

#define MENU_HELP	"Press 'h' for help."

#define TITLE_HEIGHT	7

#define MIN_X		80
#define MIN_Y		27

#define PAGE_OFFSET	10

#define SCROLL_NONE     0
#define SCROLL_DOWN     1

#define SCROLL_DOWN_INDICATOR "... More ..."

#define MIN(a, b) ({ typeof(a) __a = (a); typeof(b) __b = (b); ((__a > __b) ? __b : __a);})
#define MAX(a, b) ({ typeof(a) __a = (a); typeof(b) __b = (b); ((__a < __b) ? __b : __a);})

extern int changes_made;

/*! Maximum number of characters horizontally */
static int max_x = 0;
/*! Maximum number of characters vertically */
static int max_y = 0;

static const char * const help_info[] = {
	"scroll              => up/down arrows",
	"toggle selection    => Enter",
	"select              => y",
	"deselect            => n",
	"select all          => F8",
	"deselect all        => F7",
	"back                => left arrow",
	"quit                => q",
	"save and quit       => x",
	"",
	"XXX means dependencies have not been met",
	"    or a conflict exists",
	"",
	"< > means a dependency has been deselected",
	"    and will be automatically re-selected",
	"    if this item is selected",
	"",
	"( ) means a conflicting item has been",
	"    selected",
};

/*! \brief Handle a window resize in xterm */
static void _winch_handler(int sig)
{
	getmaxyx(stdscr, max_y, max_x);

	if (max_x < MIN_X || max_y < MIN_Y) {
		fprintf(stderr, "Terminal must be at least %d x %d.\n", MIN_X, MIN_Y);
		max_x = MIN_X - 1;
		max_y = MIN_Y - 1;
	}
}

static struct sigaction winch_handler = {
	.sa_handler = _winch_handler,
};

/*! \brief Handle a SIGQUIT */
static void _sigint_handler(int sig)
{

}

static struct sigaction sigint_handler = {
	.sa_handler = _sigint_handler,
};

/*! \brief Display help information */
static void show_help(WINDOW *win)
{
	int i;

	wclear(win);
	for (i = 0; i < (sizeof(help_info) / sizeof(help_info[0])); i++) {
		wmove(win, i, max_x / 2 - 15);
		waddstr(win, (char *) help_info[i]);
	}
	wrefresh(win);
	getch(); /* display the help until the user hits a key */
}

static int really_quit(WINDOW *win)
{
	int c;
	wclear(win);
	wmove(win, 2, max_x / 2 - 15);
	waddstr(win, "ARE YOU SURE?");
        wmove(win, 3, max_x / 2 - 12);
	waddstr(win, "--- It appears you have made some changes, and");
	wmove(win, 4, max_x / 2 - 12);
	waddstr(win, "you have opted to Quit without saving these changes!");
	wmove(win, 6, max_x / 2 - 12);
	waddstr(win, "  Please Enter Y to exit without saving;");
	wmove(win, 7, max_x / 2 - 12);
	waddstr(win, "  Enter N to cancel your decision to quit,");
	wmove(win, 8, max_x / 2 - 12);
	waddstr(win, "     and keep working in menuselect, or");
	wmove(win, 9, max_x / 2 - 12);
	waddstr(win, "  Enter S to save your changes, and exit");
	wmove(win, 10, max_x / 2 - 12);
	wrefresh(win);
	while ((c=getch())) {
		if (c == 'Y' || c == 'y') {
			c = 'q';
			break;
		}
		if (c == 'S' || c == 's') {
			c = 'S';
			break;
		}
		if (c == 'N' || c == 'n') {
			c = '%';
			break;
		}
	}
	return c;
}

#define MENU_HELP_LEFT_ADJ 16
#define MAIN_MENU_LEFT_ADJ 20
#define CAT_MENU_LEFT_ADJ 20
#define SCROLL_DOWN_LEFT_ADJ 15
#define MEMBER_INFO_LEFT_ADJ 25

static void draw_main_menu(WINDOW *menu, int curopt)
{
	struct category *cat;
	char buf[64];
	int i = 0;

	wclear(menu);

	AST_LIST_TRAVERSE(&categories, cat, list) {
		wmove(menu, i++, max_x / 2 - MAIN_MENU_LEFT_ADJ);
		snprintf(buf, sizeof(buf), "%s", strlen_zero(cat->displayname) ? cat->name : cat->displayname);
		waddstr(menu, buf);
	}

	wmove(menu, curopt, (max_x / 2) - MAIN_MENU_LEFT_ADJ - 5);
	waddstr(menu, "--->");
	wmove(menu, curopt, (max_x / 2) - MAIN_MENU_LEFT_ADJ);

	wrefresh(menu);
}

static void display_mem_info(WINDOW *menu, struct member *mem, int start_y, int end)
{
	char buf[64];
	struct reference *dep;
	struct reference *con;
	struct reference *use;
	int start_x = (max_x / 2 - MEMBER_INFO_LEFT_ADJ);
	int maxlen = (max_x - start_x);

	wmove(menu, end - start_y + 1, 0);
	wclrtoeol(menu);
	wmove(menu, end - start_y + 2, 0);
	wclrtoeol(menu);
	wmove(menu, end - start_y + 3, 0);
	wclrtoeol(menu);
	wmove(menu, end - start_y + 4, 0);
	wclrtoeol(menu);
	wmove(menu, end - start_y + 5, 0);
	wclrtoeol(menu);
	wmove(menu, end - start_y + 6, 0);
	wclrtoeol(menu);
	wmove(menu, end - start_y + 7, 0);
	wclrtoeol(menu);

	if (mem->displayname) {
		char buf[maxlen + 1];
		char *displayname = ast_strdupa(mem->displayname);
		char *word;
		int current_line = 1;
		int new_line = 1;

		buf[0] = '\0';
		wmove(menu, end - start_y + 1, start_x);

		while ((word = strsep(&displayname, " "))) {
			if ((strlen(buf) + strlen(word) + 1) > maxlen) {
				waddstr(menu, buf);
				current_line++;
				wmove(menu, end - start_y + current_line, start_x);
				buf[0] = '\0';
				new_line = 1;
			}
			sprintf(buf + strlen(buf), "%*.*s%s", new_line ? 0 : 1, new_line ? 0 : 1, " ", word);
			new_line = 0;
		}
		if (strlen(buf)) {
			waddstr(menu, buf);
		}
	}

	if (!AST_LIST_EMPTY(&mem->deps)) {
		wmove(menu, end - start_y + 4, start_x);
		strcpy(buf, "Depends on: ");
		AST_LIST_TRAVERSE(&mem->deps, dep, list) {
			strncat(buf, dep->displayname, sizeof(buf) - strlen(buf) - 1);
			strncat(buf, dep->member ? "(M)" : "(E)", sizeof(buf) - strlen(buf) - 1);
			if (AST_LIST_NEXT(dep, list))
				strncat(buf, ", ", sizeof(buf) - strlen(buf) - 1);
		}
		waddstr(menu, buf);
	}
	if (!AST_LIST_EMPTY(&mem->uses)) {
		wmove(menu, end - start_y + 5, start_x);
		strcpy(buf, "Can use: ");
		AST_LIST_TRAVERSE(&mem->uses, use, list) {
			strncat(buf, use->displayname, sizeof(buf) - strlen(buf) - 1);
			strncat(buf, use->member ? "(M)" : "(E)", sizeof(buf) - strlen(buf) - 1);
			if (AST_LIST_NEXT(use, list))
				strncat(buf, ", ", sizeof(buf) - strlen(buf) - 1);
		}
		waddstr(menu, buf);
	}
	if (!AST_LIST_EMPTY(&mem->conflicts)) {
		wmove(menu, end - start_y + 6, start_x);
		strcpy(buf, "Conflicts with: ");
		AST_LIST_TRAVERSE(&mem->conflicts, con, list) {
			strncat(buf, con->displayname, sizeof(buf) - strlen(buf) - 1);
			strncat(buf, con->member ? "(M)" : "(E)", sizeof(buf) - strlen(buf) - 1);
			if (AST_LIST_NEXT(con, list))
				strncat(buf, ", ", sizeof(buf) - strlen(buf) - 1);
		}
		waddstr(menu, buf);
	}

	if (!mem->is_separator) { /* Separators lack support levels */
		{ /* support level */
			char buf2[64];
			wmove(menu, end - start_y + 7, start_x);
			snprintf(buf, sizeof(buf), "Support Level: %s", mem->support_level);
			if (mem->replacement && *mem->replacement) {
				snprintf(buf2, sizeof(buf2), ", Replaced by: %s", mem->replacement);
				strncat(buf, buf2, sizeof(buf) - strlen(buf) - 1);
			}
			if (mem->deprecated_in && *mem->deprecated_in) {
				snprintf(buf2, sizeof(buf2), ", Deprecated in: %s", mem->deprecated_in);
				strncat(buf, buf2, sizeof(buf) - strlen(buf) - 1);
			}
			if (mem->removed_in && *mem->removed_in) {
				snprintf(buf2, sizeof(buf2), ", Removed in: %s", mem->removed_in);
				strncat(buf, buf2, sizeof(buf) - strlen(buf) - 1);
			}
			waddstr(menu, buf);
		}
	}
}

static void draw_category_menu(WINDOW *menu, struct category *cat, int start, int end, int curopt, int changed, int flags)
{
	int i = 0;
	int j = 0;
	struct member *mem;
	char buf[64];

	if (!changed) {
		/* If all we have to do is move the cursor,
		 * then don't clear the screen and start over */
		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			i++;
			if (curopt + 1 == i) {
				display_mem_info(menu, mem, start, end);
				break;
			}
		}
		wmove(menu, curopt - start, (max_x / 2) - (CAT_MENU_LEFT_ADJ - 1));
		wrefresh(menu);
		return;
	}

	wclear(menu);

	i = 0;
	AST_LIST_TRAVERSE(&cat->members, mem, list) {
		if (i < start) {
			i++;
			continue;
		}
		wmove(menu, j++, max_x / 2 - CAT_MENU_LEFT_ADJ);
		i++;
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
		waddstr(menu, buf);

		if (curopt + 1 == i)
			display_mem_info(menu, mem, start, end);

		if (i == end - (flags & SCROLL_DOWN ? 1 : 0))
			break;
	}

	if (flags & SCROLL_DOWN) {
		wmove(menu, j, max_x / 2 - SCROLL_DOWN_LEFT_ADJ);
		waddstr(menu, SCROLL_DOWN_INDICATOR);
	}

	wmove(menu, curopt - start, (max_x / 2) - (CAT_MENU_LEFT_ADJ - 1));
	wrefresh(menu);
}

static void play_space(void);

static int move_up(int *current, int itemcount, int delta, int *start, int *end, int scroll)
{
	if (*current > 0) {
		*current = MAX(*current - delta, 0);
		if (*current < *start) {
			int diff = *start - MAX(*start - delta, 0);
			*start  -= diff;
			*end    -= diff;
			return 1;
		}
	}
	return 0;
}

static int move_down(int *current, int itemcount, int delta, int *start, int *end, int scroll)
{
	if (*current < itemcount) {
		*current = MIN(*current + delta, itemcount);
		if (*current > *end - 1 - (scroll & SCROLL_DOWN ? 1 : 0)) {
			int diff = MIN(*end + delta - 1, itemcount) - *end + 1;
			*start  += diff;
			*end    += diff;
			return 1;
		}
	}
	return 0;
}

static int run_category_menu(WINDOW *menu, int cat_num)
{
	struct category *cat;
	int i = 0;
	int start = 0;
	int end = max_y - TITLE_HEIGHT - 8;
	int c;
	int curopt = 0;
	int maxopt;
	int changed = 1;
	int scroll = SCROLL_NONE;

	AST_LIST_TRAVERSE(&categories, cat, list) {
		if (i++ == cat_num)
			break;
	}
	if (!cat)
		return -1;

	maxopt = count_members(cat) - 1;

	if (maxopt > end) {
		scroll = SCROLL_DOWN;
	}

	draw_category_menu(menu, cat, start, end, curopt, changed, scroll);

	while ((c = getch())) {
		changed = 0;
		switch (c) {
		case KEY_UP:
			changed = move_up(&curopt, maxopt, 1, &start, &end, scroll);
			break;
		case KEY_DOWN:
			changed = move_down(&curopt, maxopt, 1, &start, &end, scroll);
			break;
		case KEY_PPAGE:
			changed = move_up(
				&curopt,
				maxopt,
				MIN(PAGE_OFFSET, max_y - TITLE_HEIGHT - 6 - (scroll & SCROLL_DOWN ? 1 : 0)),
				&start,
				&end,
				scroll);
			break;
		case KEY_NPAGE:
			changed = move_down(
				&curopt,
				maxopt,
				MIN(PAGE_OFFSET, max_y - TITLE_HEIGHT - 6 - (scroll & SCROLL_DOWN ? 1 : 0)),
				&start,
				&end,
				scroll);
			break;
		case KEY_HOME:
			changed = move_up(&curopt, maxopt, curopt, &start, &end, scroll);
			break;
		case KEY_END:
			changed = move_down(&curopt, maxopt, maxopt - curopt, &start, &end, scroll);
			break;
		case KEY_LEFT:
		case 27:	/* Esc key */
			return 0;
		case KEY_RIGHT:
		case KEY_ENTER:
		case '\n':
		case ' ':
			toggle_enabled_index(cat, curopt);
			changed = 1;
			break;
		case 'y':
		case 'Y':
			set_enabled(cat, curopt);
			changed = 1;
			break;
		case 'n':
		case 'N':
			clear_enabled(cat, curopt);
			changed = 1;
			break;
		case 'h':
		case 'H':
			show_help(menu);
			changed = 1;
			break;
		case KEY_F(7):
			set_all(cat, 0);
			changed = 1;
			break;
		case KEY_F(8):
			set_all(cat, 1);
			changed = 1;
		default:
			break;
		}
		if (c == 'x' || c == 'X' || c == 'Q' || c == 'q')
			break;

		if (end <= maxopt) {
			scroll |= SCROLL_DOWN;
		} else {
			scroll &= ~SCROLL_DOWN;
		}

		draw_category_menu(menu, cat, start, end, curopt, changed, scroll);
	}

	wrefresh(menu);

	return c;
}

static void draw_title_window(WINDOW *title)
{
	char titlebar[strlen(menu_name) + 9];

	memset(titlebar, '*', sizeof(titlebar) - 1);
	titlebar[sizeof(titlebar) - 1] = '\0';
	wclear(title);
	wmove(title, 1, (max_x / 2) - (strlen(titlebar) / 2));
	waddstr(title, titlebar);
	wmove(title, 2, (max_x / 2) - (strlen(menu_name) / 2));
	waddstr(title, (char *) menu_name);
	wmove(title, 3, (max_x / 2) - (strlen(titlebar) / 2));
	waddstr(title, titlebar);
	wmove(title, 5, (max_x / 2) - MENU_HELP_LEFT_ADJ);
	waddstr(title, MENU_HELP);
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

	setenv("ESCDELAY", "0", 1); /* So that ESC is processed immediately */

	initscr();
	getmaxyx(stdscr, max_y, max_x);
	sigaction(SIGWINCH, &winch_handler, NULL); /* handle window resizing in xterm */
	sigaction(SIGINT, &sigint_handler, NULL); /* handle window resizing in xterm */

	if (max_x < MIN_X || max_y < MIN_Y) {
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
		case KEY_HOME:
			curopt = 0;
			break;
		case KEY_END:
			curopt = maxopt;
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
			break;
		case 'i':
		case 'I':
			play_space();
			draw_title_window(title);
		default:
			break;
		}
		if (c == 'q' || c == 'Q' || c == 27 || c == 3) {
			if (changes_made) {
				c = really_quit(menu);
				if (c == 'q') {
					res = -1;
					break;
				}
			} else {
				res = -1;
				break;
			}
		}
		if (c == 'x' || c == 'X' || c == 's' || c == 'S')
			break;
		draw_main_menu(menu, curopt);
	}

	endwin();

	return res;
}

enum blip_type {
	BLIP_TANK = 0,
	BLIP_SHOT,
	BLIP_BOMB,
	BLIP_ALIEN,
	BLIP_BARRIER,
	BLIP_UFO
};

struct blip {
	enum blip_type type;
	int x;
	int y;
	int ox;
	int oy;
	int goingleft;
	int health;
	AST_LIST_ENTRY(blip) entry;
};

static AST_LIST_HEAD_NOLOCK(, blip) blips;

static int respawn = 0;
static int score = 0;
static int num_aliens = 0;
static int alien_sleeptime = 0;
struct blip *ufo = NULL;
struct blip *tank = NULL;

/*! Probability of a bomb, out of 100 */
#define BOMB_PROB   1

static int add_barrier(int x, int y)
{
	struct blip *cur = NULL;

	cur = calloc(1,sizeof(struct blip));
	if(!cur) {
		return -1;
	}
	cur->type=BLIP_BARRIER;
	cur->x = x;
	cur->y=max_y - y;
	cur->health = 1;
	AST_LIST_INSERT_HEAD(&blips, cur,entry);
	return 0;
}

static int init_blips(void)
{
	int i, j;
	struct blip *cur;
	int offset = 4;

	srandom(time(NULL) + getpid());

	/* make tank */
	cur = calloc(1, sizeof(struct blip));
	if (!cur)
		return -1;
	cur->type = BLIP_TANK;
	cur->x = max_x / 2;
	cur->y = max_y - 1;
	AST_LIST_INSERT_HEAD(&blips, cur, entry);
	tank = cur;

	/* 3 rows of 10 aliens */
	num_aliens = 0;
	for (i = 0; i < 3; i++) {
		for (j = 0; j < 10; j++) {
			cur = calloc(1, sizeof(struct blip));
			if (!cur)
				return -1;
			cur->type = BLIP_ALIEN;
			cur->x = (j * 2) + 1;
			cur->y = (i * 2) + 2;
			AST_LIST_INSERT_HEAD(&blips, cur, entry);
			num_aliens++;
		}
	}
	for(i=0; i < 4; i++) {
		if (i > 0)
			offset += 5 + ((max_x) -28) / 3;
		add_barrier(offset + 1, 6);
		add_barrier(offset + 2, 6);
		add_barrier(offset + 3, 6);

		add_barrier(offset, 5);
		add_barrier(offset + 1, 5);
		add_barrier(offset + 2, 5);
		add_barrier(offset + 3, 5);
		add_barrier(offset + 4, 5);

		add_barrier(offset, 4);
		add_barrier(offset + 1, 4);
		add_barrier(offset + 3, 4);
		add_barrier(offset + 4, 4);
	}
	return 0;
}

static inline chtype type2chtype(enum blip_type type)
{
	switch (type) {
	case BLIP_TANK:
		return 'A';
	case BLIP_ALIEN:
		return 'X';
	case BLIP_SHOT:
		return '|';
	case BLIP_BOMB:
		return 'o';
	case BLIP_BARRIER:
		return '*';
	case BLIP_UFO:
		return '@';
	default:
		break;
	}
	return '?';
}

static int repaint_screen(void)
{
	struct blip *cur;

	wmove(stdscr, 0, 0);
	wprintw(stdscr, "Score: %d", score);

	AST_LIST_TRAVERSE(&blips, cur, entry) {
		if (cur->x != cur->ox || cur->y != cur->oy) {
			wmove(stdscr, cur->oy, cur->ox);
			waddch(stdscr, ' ');
			wmove(stdscr, cur->y, cur->x);
			waddch(stdscr, type2chtype(cur->type));
			cur->ox = cur->x;
			cur->oy = cur->y;
		}
	}

	wmove(stdscr, 0, max_x - 1);

	wrefresh(stdscr);

	return 0;
}

static int tank_move_left(void)
{
	if (tank->x > 0)
		tank->x--;

	return 0;
}

static int tank_move_right(void)
{
	if (tank->x < (max_x - 1))
		tank->x++;

	return 0;
}

static int count_shots(void)
{
	struct blip *cur;
	int count = 0;

	AST_LIST_TRAVERSE(&blips, cur, entry) {
		if (cur->type == BLIP_SHOT)
			count++;
	}

	return count;
}

static int tank_shoot(void)
{
	struct blip *shot;

	if (count_shots() == 3)
		return 0;

	score--;

	shot = calloc(1, sizeof(struct blip));
	if (!shot)
		return -1;
	shot->type = BLIP_SHOT;
	shot->x = tank->x;
	shot->y = max_y - 2;
	AST_LIST_INSERT_HEAD(&blips, shot, entry);

	return 0;
}

static int remove_blip(struct blip *blip)
{
	if (!blip) {
		return -1;
	}

	AST_LIST_REMOVE(&blips, blip, entry);

	if (blip->type == BLIP_ALIEN) {
		num_aliens--;
	}
	wmove(stdscr, blip->oy, blip->ox);
	waddch(stdscr, ' ');
	free(blip);

	return 0;
}

static int move_aliens(void)
{
	struct blip *cur;
	struct blip *current_barrier;

	AST_LIST_TRAVERSE(&blips, cur, entry) {
		if (cur->type != BLIP_ALIEN) {
			/* do nothing if it's not an alien */
			continue;
		}
		if (cur->goingleft && (cur->x == 0)) {
			cur->y++;
			cur->goingleft = 0;
		} else if (!cur->goingleft && cur->x == (max_x - 1)) {
			cur->y++;
			cur->goingleft = 1;
		} else if (cur->goingleft) {
			cur->x--;
		} else {
			cur->x++;
		}
		/* Alien into the tank == game over */
		if (cur->x == tank->x && cur->y == tank->y)
			return 1;
		AST_LIST_TRAVERSE(&blips, current_barrier, entry){
			if(current_barrier->type!=BLIP_BARRIER)
				continue;
			if(cur->y == current_barrier->y && cur->x == current_barrier -> x)
				remove_blip(current_barrier);
		}
		if (random() % 100 < BOMB_PROB && cur->y != max_y) {
			struct blip *bomb = calloc(1, sizeof(struct blip));
			if (!bomb)
				continue;
			bomb->type = BLIP_BOMB;
			bomb->x = cur->x;
			bomb->y = cur->y + 1;
			AST_LIST_INSERT_HEAD(&blips, bomb, entry);
		}
	}

	return 0;
}

static int move_bombs(void)
{
	struct blip *cur;
	struct blip *current_barrier;

	AST_LIST_TRAVERSE(&blips, cur, entry) {
		int mark = 0;
		if (cur->type != BLIP_BOMB)
			continue;
		cur->y++;
		if (cur->x == tank->x && cur->y == tank->y) {
			return 1;
		}

		AST_LIST_TRAVERSE(&blips, current_barrier, entry) {
			if (current_barrier->type != BLIP_BARRIER)
				continue;
			if (cur->x == current_barrier->x && cur->y == current_barrier->y) {
				mark = 1;
				current_barrier->health--;
				if (current_barrier->health == 0)
					remove_blip(current_barrier);
			}
		}
		if (mark){
			remove_blip(cur);}
	}

	return 0;
}

static void move_shots(void)
{
	struct blip *cur;

	AST_LIST_TRAVERSE(&blips, cur, entry) {
		if (cur->type != BLIP_SHOT)
			continue;
		cur->y--;
	}
}


static int ufo_action()
{
	struct blip *cur;

	AST_LIST_TRAVERSE(&blips, cur, entry) {
		if (cur->type != BLIP_UFO) {
			continue;
		}

		cur->x--;

		if (cur->x < 0) {
			remove_blip(cur);
			respawn += 1;
		}

	}

	if (respawn == 7) {
		respawn = 0;
		/* make new mothership*/
		cur = calloc(1, sizeof(struct blip));
		if(!cur)
			return -1;
		cur->type = BLIP_UFO;
		cur->x = max_x - 1;
		cur->y = 1;
		AST_LIST_INSERT_HEAD(&blips, cur, entry);
	}

	return 0;
}

static void game_over(int win)
{
	clear();

	wmove(stdscr, max_y / 2, max_x / 2 - 10);
	wprintw(stdscr, "Game over!  You %s!", win ? "win" : "lose");

	wmove(stdscr, 0, max_x - 1);

	wrefresh(stdscr);

	sleep(1);

	while (getch() != ' ');

	return;
}

static int check_shot(struct blip *shot)
{
	struct blip *cur;

	AST_LIST_TRAVERSE(&blips, cur, entry) {
		if ((cur->type == BLIP_ALIEN || cur->type == BLIP_UFO) && cur->x == shot->x && cur->y == shot->y){
			if (cur->type == BLIP_UFO) {
				score += 80;
			}
			score += 20;
			remove_blip(cur);
			remove_blip(shot);
			respawn += 1;
			if (!num_aliens) {
				if(alien_sleeptime < 101) {
					game_over(1);
					return 1;
				} else {
					alien_sleeptime = alien_sleeptime - 100;
					return 1;
				}
			}
			break;
		}
		if (cur->type == BLIP_BARRIER) {
			if (shot->x == cur->x && shot->y == cur->y) {
				remove_blip(cur);
				remove_blip(shot);
				break;
			}
		}
	}

	return 0;
}

static int check_placement(void)
{
	struct blip *cur;

	AST_LIST_TRAVERSE_SAFE_BEGIN(&blips, cur, entry) {
		if (cur->y <= 0 || cur->y >= max_y) {
			AST_LIST_REMOVE_CURRENT(&blips, entry);
			remove_blip(cur);
		} else if (cur->type == BLIP_SHOT && check_shot(cur))
			return 1;
	}
	AST_LIST_TRAVERSE_SAFE_END

	return 0;
}

static void play_space(void)
{
	int c;
	unsigned int jiffies = 1;
	int quit = 0;
	struct blip *blip;
	alien_sleeptime = 1000;
	score = 0;

	while(alien_sleeptime > 100) {

		jiffies = 1;
		clear();
		nodelay(stdscr, TRUE);
		init_blips();
		repaint_screen();

		for (;;) {
			c = getch();
			switch (c) {
			case ' ':
				tank_shoot();
				break;
			case KEY_LEFT:
				tank_move_left();
				break;
			case KEY_RIGHT:
				tank_move_right();
				break;
			case 'x':
			case 'X':
			case 'q':
			case 'Q':
				quit = 1;
			default:
				/* ignore unknown input */
				break;
			}
			if (quit) {
				alien_sleeptime = 1;
				break;
			}
			if (!(jiffies % 25)) {
				if (move_aliens() || move_bombs() || ufo_action()) {
					alien_sleeptime = 1;
					game_over(0);
					break;
				}
				if (check_placement())
					break;
			}
			if (!(jiffies % 10)) {
				move_shots();
				if (check_placement())
					break;
			}
			repaint_screen();
			jiffies++;
			usleep(alien_sleeptime);
		}

		while ((blip = AST_LIST_REMOVE_HEAD(&blips, entry)))
			free(blip);
	}

	nodelay(stdscr, FALSE);
}
