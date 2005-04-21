/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * GTK Console monitor -- very kludgy right now
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

/* 
 * I know this might seem somewhat pointless in its current phase, but one
 * of the most important parts of this module is demonstrate that modules
 * can require other external libraries and still be loaded (in this
 * case, a host of libraries involving gtk), so long as they are properly
 * linked (see the Makefile)
 */

#include <sys/types.h>
#include "asterisk/pbx.h"
#include "asterisk/config.h"
#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk/options.h"
#include "asterisk/cli.h"
#include "asterisk/utils.h"
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <signal.h>
#include <sys/time.h>

#include <gtk/gtk.h>
#include <glib.h>
/* For where to put dynamic tables */
#include "asterisk.h"
#include "astconf.h"

AST_MUTEX_DEFINE_STATIC(verb_lock);

static pthread_t console_thread;

static int inuse=0;
static int clipipe[2];
static int cleanupid = -1;

static char *dtext = "Asterisk PBX Console (GTK Version)";

static GtkWidget *window;
static GtkWidget *quit;
static GtkWidget *closew;
static GtkWidget *verb;
static GtkWidget *modules;
static GtkWidget *statusbar;
static GtkWidget *cli;

static struct timeval last;

static void update_statusbar(char *msg)
{
	gtk_statusbar_pop(GTK_STATUSBAR(statusbar), 1);
	gtk_statusbar_push(GTK_STATUSBAR(statusbar), 1, msg);
}

int unload_module(void)
{
	if (inuse) {
		/* Kill off the main thread */
		pthread_cancel(console_thread);
		gdk_threads_enter();
		gtk_widget_destroy(window);
		gdk_threads_leave();
		close(clipipe[0]);
		close(clipipe[1]);
	}
	return 0;
}

static int cleanup(void *useless)
{
	gdk_threads_enter();
	gtk_clist_thaw(GTK_CLIST(verb));
	gtk_widget_queue_resize(verb->parent);
	gtk_clist_moveto(GTK_CLIST(verb), GTK_CLIST(verb)->rows - 1, 0, 0, 0);
	cleanupid = -1;
	gdk_threads_leave();
	return 0;
}


static void __verboser(const char *stuff, int opos, int replacelast, int complete)
{
	char *s2[2];
	struct timeval tv;
	int ms;
	s2[0] = (char *)stuff;
	s2[1] = NULL;
	gtk_clist_freeze(GTK_CLIST(verb));
	if (replacelast) 
		gtk_clist_remove(GTK_CLIST(verb), GTK_CLIST(verb)->rows - 1);
	gtk_clist_append(GTK_CLIST(verb), s2);
	if (last.tv_sec || last.tv_usec) {
		gdk_threads_leave();
		gettimeofday(&tv, NULL);
		if (cleanupid > -1)
			gtk_timeout_remove(cleanupid);
		ms = (tv.tv_sec - last.tv_sec) * 1000 + (tv.tv_usec - last.tv_usec) / 1000;
		if (ms < 100) {
			/* We just got a message within 100ms, so just schedule an update
			   in the near future */
			cleanupid = gtk_timeout_add(200, cleanup, NULL);
		} else {
			cleanup(&cleanupid);
		}
		last = tv;
	} else {
		gettimeofday(&last, NULL);
	}
}

static void verboser(const char *stuff, int opos, int replacelast, int complete) 
{
	ast_mutex_lock(&verb_lock);
	/* Lock appropriately if we're really being called in verbose mode */
	__verboser(stuff, opos, replacelast, complete);
	ast_mutex_unlock(&verb_lock);
}

static void cliinput(void *data, int source, GdkInputCondition ic)
{
	static char buf[256];
	static int offset = 0;
	int res;
	char *c;
	char *l;
	char n;
	/* Read as much stuff is there */
	res = read(source, buf + offset, sizeof(buf) - 1 - offset);
	if (res > -1)
		buf[res + offset] = '\0';
	/* make sure we've null terminated whatever we have so far */
	c = buf;
	l = buf;
	while(*c) {
		if (*c == '\n') {
			/* Keep the trailing \n */
			c++;
			n = *c;
			*c = '\0';
			__verboser(l, 0, 0, 1);
			*(c - 1) = '\0';
			*c = n;
			l = c;
		} else
		c++;
	}
	if (strlen(l)) {
		/* We have some left over */
		memmove(buf, l, strlen(l) + 1);
		offset = strlen(buf);
	} else {
		offset = 0;
	}

}


static void remove_module(void)
{
	int res;
	char *module;
	char buf[256];
	if (GTK_CLIST(modules)->selection) {
		module= (char *)gtk_clist_get_row_data(GTK_CLIST(modules), (int) GTK_CLIST(modules)->selection->data);
		gdk_threads_leave();
		res = ast_unload_resource(module, 0);
		gdk_threads_enter();
		if (res) {
			snprintf(buf, sizeof(buf), "Module '%s' is in use", module);
			update_statusbar(buf);
		} else {
			snprintf(buf, sizeof(buf), "Module '%s' removed", module);
			update_statusbar(buf);
		}
	}
}
static void reload_module(void)
{
	int res, x;
	char *module;
	char buf[256];
	if (GTK_CLIST(modules)->selection) {
		module= (char *)gtk_clist_get_row_data(GTK_CLIST(modules), (int) GTK_CLIST(modules)->selection->data);
		module = strdup(module);
		if (module) {
			gdk_threads_leave();
			res = ast_unload_resource(module, 0);
			gdk_threads_enter();
			if (res) {
				snprintf(buf, sizeof(buf), "Module '%s' is in use", module);
				update_statusbar(buf);
			} else {
				gdk_threads_leave();
				res = ast_load_resource(module);
				gdk_threads_enter();
				if (res) {
					snprintf(buf, sizeof(buf), "Error reloading module '%s'", module);
				} else {
					snprintf(buf, sizeof(buf), "Module '%s' reloaded", module);
				}
				for (x=0; x < GTK_CLIST(modules)->rows; x++) {
					if (!strcmp((char *)gtk_clist_get_row_data(GTK_CLIST(modules), x), module)) {
						gtk_clist_select_row(GTK_CLIST(modules), x, -1);
						break;
					}
				}
				update_statusbar(buf);
				
			}
			free(module);
		}
	}
}

static void file_ok_sel(GtkWidget *w, GtkFileSelection *fs)
{
	char tmp[AST_CONFIG_MAX_PATH];
	char *module = gtk_file_selection_get_filename(fs);
	char buf[256];
	snprintf(tmp, sizeof(tmp), "%s/", ast_config_AST_MODULE_DIR);
	if (!strncmp(module, (char *)tmp, strlen(tmp))) 
		module += strlen(tmp);
	gdk_threads_leave();
	if (ast_load_resource(module)) {
		snprintf(buf, sizeof(buf), "Error loading module '%s'.", module);
		update_statusbar(buf);
	} else {
		snprintf(buf, sizeof(buf), "Module '%s' loaded", module);
		update_statusbar(buf);
	}
	gdk_threads_enter();
	gtk_widget_destroy(GTK_WIDGET(fs));
}

static void add_module(void)
{
	char tmp[AST_CONFIG_MAX_PATH];
	GtkWidget *filew;
	snprintf(tmp, sizeof(tmp), "%s/*.so", ast_config_AST_MODULE_DIR);
	filew = gtk_file_selection_new("Load Module");
	gtk_signal_connect(GTK_OBJECT (GTK_FILE_SELECTION(filew)->ok_button),
					"clicked", GTK_SIGNAL_FUNC(file_ok_sel), filew);
	gtk_signal_connect_object(GTK_OBJECT (GTK_FILE_SELECTION(filew)->cancel_button),
					"clicked", GTK_SIGNAL_FUNC(gtk_widget_destroy), GTK_OBJECT(filew));
	gtk_file_selection_set_filename(GTK_FILE_SELECTION(filew), (char *)tmp);
	gtk_widget_show(filew);
}

static int add_mod(char *module, char *description, int usecount)
{
	char use[10];
	char *pass[4];
	int row;
	snprintf(use, sizeof(use), "%d", usecount);
	pass[0] = module;
	pass[1] = description;
	pass[2] = use;
	pass[3] = NULL;
	row = gtk_clist_append(GTK_CLIST(modules), pass);
	gtk_clist_set_row_data(GTK_CLIST(modules), row, module);
	return 0;	
}

static int mod_update(void)
{
	char *module= NULL;
	/* Update the mod stuff */
	if (GTK_CLIST(modules)->selection) {
		module= (char *)gtk_clist_get_row_data(GTK_CLIST(modules), (int) GTK_CLIST(modules)->selection->data);
	}
	gtk_clist_freeze(GTK_CLIST(modules));
	gtk_clist_clear(GTK_CLIST(modules));
	ast_update_module_list(add_mod);
	if (module)
		gtk_clist_select_row(GTK_CLIST(modules), gtk_clist_find_row_from_data(GTK_CLIST(modules), module), -1);
	gtk_clist_thaw(GTK_CLIST(modules));
	return 1;
}

static void exit_now(GtkWidget *widget, gpointer data)
{
	ast_loader_unregister(mod_update);
	gtk_main_quit();
	inuse--;
	ast_update_use_count();
	ast_unregister_verbose(verboser);
	ast_unload_resource("pbx_gtkconsole", 0);
	if (option_verbose > 1)
		ast_verbose(VERBOSE_PREFIX_2 "GTK Console Monitor Exiting\n");
	/* XXX Trying to quit after calling this makes asterisk segfault XXX */
}

static void exit_completely(GtkWidget *widget, gpointer data)
{
#if 0
	/* Clever... */
	ast_cli_command(clipipe[1], "quit");
#else
	kill(getpid(), SIGTERM);
#endif
}

static void exit_nicely(GtkWidget *widget, gpointer data)
{
	fflush(stdout);
	gtk_widget_destroy(window);
}

static void *consolethread(void *data)
{
	gtk_widget_show(window);
	gdk_threads_enter();
	gtk_main();
	gdk_threads_leave();
	return NULL;
}

static int cli_activate(void)
{
	char buf[256] = "";
	strncpy(buf, gtk_entry_get_text(GTK_ENTRY(cli)), sizeof(buf) - 1);
	gtk_entry_set_text(GTK_ENTRY(cli), "");
	if (strlen(buf)) {
		ast_cli_command(clipipe[1], buf);
	}
	return TRUE;
}

static int show_console(void)
{
	GtkWidget *hbox;
	GtkWidget *wbox;
	GtkWidget *notebook;
	GtkWidget *sw;
	GtkWidget *bbox, *hbbox, *add, *removew, *reloadw;
	char *modtitles[3] = { "Module", "Description", "Use Count" };
	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	
	statusbar = gtk_statusbar_new();
	gtk_widget_show(statusbar);
	
	gtk_signal_connect(GTK_OBJECT(window), "delete_event",
			GTK_SIGNAL_FUNC (exit_nicely), window);
	gtk_signal_connect(GTK_OBJECT(window), "destroy",
			GTK_SIGNAL_FUNC (exit_now), window);
	gtk_container_set_border_width(GTK_CONTAINER(window), 10);

	quit = gtk_button_new_with_label("Quit Asterisk");
	gtk_signal_connect(GTK_OBJECT(quit), "clicked",
			GTK_SIGNAL_FUNC (exit_completely), window);
	gtk_widget_show(quit);

	closew = gtk_button_new_with_label("Close Window");
	gtk_signal_connect(GTK_OBJECT(closew), "clicked",
			GTK_SIGNAL_FUNC (exit_nicely), window);
	gtk_widget_show(closew);

	notebook = gtk_notebook_new();
	verb = gtk_clist_new(1);
	gtk_clist_columns_autosize(GTK_CLIST(verb));
	sw = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
	gtk_container_add(GTK_CONTAINER(sw), verb);
	gtk_widget_show(verb);
	gtk_widget_show(sw);
	gtk_widget_set_usize(verb, 640, 400);
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), sw, gtk_label_new("Verbose Status"));

	
	modules = gtk_clist_new_with_titles(3, modtitles);
	gtk_clist_columns_autosize(GTK_CLIST(modules));
	gtk_clist_set_column_auto_resize(GTK_CLIST(modules), 0, TRUE);
	gtk_clist_set_column_auto_resize(GTK_CLIST(modules), 1, TRUE);
	gtk_clist_set_column_auto_resize(GTK_CLIST(modules), 2, TRUE);
	gtk_clist_set_sort_column(GTK_CLIST(modules), 0);
	gtk_clist_set_auto_sort(GTK_CLIST(modules), TRUE);
	gtk_clist_column_titles_passive(GTK_CLIST(modules));
	sw = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
	gtk_container_add(GTK_CONTAINER(sw), modules);
	gtk_clist_set_selection_mode(GTK_CLIST(modules), GTK_SELECTION_BROWSE);
	gtk_widget_show(modules);
	gtk_widget_show(sw);

	add = gtk_button_new_with_label("Load...");
	gtk_widget_show(add);
	removew = gtk_button_new_with_label("Unload");
	gtk_widget_show(removew);
	reloadw = gtk_button_new_with_label("Reload");
	gtk_widget_show(reloadw);
	gtk_signal_connect(GTK_OBJECT(removew), "clicked",
			GTK_SIGNAL_FUNC (remove_module), window);
	gtk_signal_connect(GTK_OBJECT(add), "clicked",
			GTK_SIGNAL_FUNC (add_module), window);
	gtk_signal_connect(GTK_OBJECT(reloadw), "clicked",
			GTK_SIGNAL_FUNC (reload_module), window);
		
	bbox = gtk_vbox_new(FALSE, 5);
	gtk_widget_show(bbox);

	gtk_widget_set_usize(bbox, 100, -1);
	gtk_box_pack_start(GTK_BOX(bbox), add, FALSE, FALSE, 5);
	gtk_box_pack_start(GTK_BOX(bbox), removew, FALSE, FALSE, 5);
	gtk_box_pack_start(GTK_BOX(bbox), reloadw, FALSE, FALSE, 5);

	hbbox = gtk_hbox_new(FALSE, 5);
	gtk_widget_show(hbbox);
	
	gtk_box_pack_start(GTK_BOX(hbbox), sw, TRUE, TRUE, 5);
	gtk_box_pack_start(GTK_BOX(hbbox), bbox, FALSE, FALSE, 5);

	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), hbbox, gtk_label_new("Module Information"));

	gtk_widget_show(notebook);

	wbox = gtk_hbox_new(FALSE, 5);
	gtk_widget_show(wbox);
	gtk_box_pack_end(GTK_BOX(wbox), quit, FALSE, FALSE, 5);
	gtk_box_pack_end(GTK_BOX(wbox), closew, FALSE, FALSE, 5);

	hbox = gtk_vbox_new(FALSE, 0);
	gtk_widget_show(hbox);
	
	/* Command line */
	cli = gtk_entry_new();
	gtk_widget_show(cli);

	gtk_signal_connect(GTK_OBJECT(cli), "activate",
			GTK_SIGNAL_FUNC (cli_activate), NULL);

	gtk_box_pack_start(GTK_BOX(hbox), notebook, TRUE, TRUE, 5);
	gtk_box_pack_start(GTK_BOX(hbox), wbox, FALSE, FALSE, 5);
	gtk_box_pack_start(GTK_BOX(hbox), cli, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), statusbar, FALSE, FALSE, 0);
	gtk_container_add(GTK_CONTAINER(window), hbox);
	gtk_window_set_title(GTK_WINDOW(window), "Asterisk Console");
	gtk_widget_grab_focus(cli);
	ast_pthread_create(&console_thread, NULL, consolethread, NULL);
	/* XXX Okay, seriously fix me! XXX */
	usleep(100000);
	ast_register_verbose(verboser);
	gtk_clist_freeze(GTK_CLIST(verb));
	ast_loader_register(mod_update);
	gtk_clist_thaw(GTK_CLIST(verb));
	gdk_input_add(clipipe[0], GDK_INPUT_READ, cliinput, NULL);
	mod_update();
	update_statusbar("Asterisk Console Ready");
	return 0;
}


int load_module(void)
{
	if (pipe(clipipe)) {
		ast_log(LOG_WARNING, "Unable to create CLI pipe\n");
		return -1;
	}
	g_thread_init(NULL);
	if (gtk_init_check(NULL, NULL))  {
		if (!show_console()) {
			inuse++;
			ast_update_use_count();
			if (option_verbose > 1)
				ast_verbose( VERBOSE_PREFIX_2 "Launched GTK Console monitor\n");		
		} else
			ast_log(LOG_WARNING, "Unable to start GTK console\n");
	} else {
		if (option_debug)
			ast_log(LOG_DEBUG, "Unable to start GTK console monitor -- ignoring\n");
		else if (option_verbose > 1)
			ast_verbose( VERBOSE_PREFIX_2 "GTK is not available -- skipping monitor\n");
	}
	return 0;
}

int usecount(void)
{
	return inuse;
}

char *description(void)
{
	return dtext;
}

char *key(void)
{
	return ASTERISK_GPL_KEY;
}
