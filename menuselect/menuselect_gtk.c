#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <gtk/gtk.h>

#include "menuselect.h"

enum {
	/*! The row name */
	COLUMN_NAME,
	/*! Whether this row is enabled */
	COLUMN_SELECTED,
	/*! Dependencies */
	COLUMN_DEPS,
	/*! Optional dependencies */
	COLUMN_USES,
	/*! Conflicts */
	COLUMN_CNFS,
	/*! Number of columns, must be the last element in the enum */
	NUM_COLUMNS,
};

static void handle_save(GtkWidget *w, gpointer data);
static void handle_about(GtkWidget *w, gpointer data);
static void handle_quit(GtkWidget *w, gpointer data);

static GtkItemFactoryEntry menu_items[] = {
  { "/_File",               NULL,         NULL,           0, "<Branch>" },
  { "/File/_Save And Quit", "<control>S", handle_save, 0, "<StockItem>", GTK_STOCK_SAVE },
  { "/File/sep1",           NULL,         NULL,           0, "<Separator>" },
  { "/File/_Quit",          "<CTRL>Q",    handle_quit,  0, "<StockItem>", GTK_STOCK_QUIT },
  { "/_Help",               NULL,         NULL,           0, "<LastBranch>" },
  { "/_Help/About",         NULL,         handle_about,   0, "<Item>" },
};

static gint nmenu_items = sizeof(menu_items) / sizeof(menu_items[0]);

static GtkTreeView *tree;
static GtkWidget *window;

/* 0, save ... non-zero, don't save */
static int main_res = 1;
static int change_made = 0;

static void handle_save(GtkWidget *w, gpointer data)
{
	main_res = 0;
	gtk_main_quit();
}

static void handle_about(GtkWidget *w, gpointer data)
{
	GtkWidget *dialog;

	dialog = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL,
				GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
				"GMenuselect - http://www.asterisk.org/\n"
				"Russell Bryant <russell@digium.com>\n"
				"Copyright (C) 2007\n");

	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
}

static gboolean delete_event(GtkWidget *widget, GdkEvent *event, gpointer data)
{
	return FALSE;
}

static void handle_quit(GtkWidget *widget, gpointer data)
{
	gtk_main_quit();
}

static void destroy(GtkWidget *widget, gpointer data)
{
	GtkWidget *dialog;
	gint response;

	if (!main_res || !change_made) {
		gtk_main_quit();
		return;
	}

	dialog = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL,
			GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO, "Save before quit?");
	response = gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);

	if (response == GTK_RESPONSE_YES)
		main_res = 0;

	gtk_main_quit();
}

static void toggled_handler(GtkCellRendererToggle *renderer, gchar *path, gpointer data)
{
	gchar *cat_num_str, *mem_num_str;
	int cat_num, mem_num;
	int i = 0;
	struct category *cat;
	struct member *mem;
	GtkTreeStore *store = data;
	GtkTreeModel *model;
	GtkTreeIter cat_iter, mem_iter;

	mem_num_str = alloca(strlen(path)) + 1;
	strcpy(mem_num_str, path);
	cat_num_str = strsep(&mem_num_str, ":");

	if (!mem_num_str || !*mem_num_str)
		return;

	cat_num = atoi(cat_num_str);
	mem_num = atoi(mem_num_str);

	AST_LIST_TRAVERSE(&categories, cat, list) {
		if (i == cat_num)
			break;
		i++;
	}
	if (!cat)
		return;

	i = 0;
	AST_LIST_TRAVERSE(&cat->members, mem, list) {
		if (i == mem_num)
			break;
		i++;
	}
	if (!mem)
		return;

	toggle_enabled(mem);

	model = gtk_tree_view_get_model(tree);

	gtk_tree_model_get_iter_first(model, &cat_iter);
	for (i = 0; i < cat_num; i++) {
		if (!gtk_tree_model_iter_next(model, &cat_iter))
			break;
	}
	if (i != cat_num)
		return;

	if (!gtk_tree_model_iter_children(model, &mem_iter, &cat_iter))
		return;

	for (i = 0; i < mem_num; i++) {
		if (!gtk_tree_model_iter_next(model, &mem_iter))
			break;
	}
	if (i != mem_num)
		return;

	gtk_tree_store_set(store, &mem_iter, COLUMN_SELECTED, mem->enabled, -1);

	change_made = 1;
}

static void row_activated_handler(GtkTreeView *treeview, GtkTreePath *path,
	GtkTreeViewColumn *col, gpointer data)
{
	GtkTreeIter iter;
	GtkTreeModel *model;
	GtkTreeStore *store = data;
	gchar *name;
	struct category *cat;
	struct member *mem;

	model = gtk_tree_view_get_model(treeview);

	if (!gtk_tree_model_get_iter(model, &iter, path))
		return;

	gtk_tree_model_get(model, &iter, COLUMN_NAME, &name, -1);

	AST_LIST_TRAVERSE(&categories, cat, list) {
		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			if (strcmp(name, mem->name))
				continue;

			toggle_enabled(mem);
			gtk_tree_store_set(store, &iter, COLUMN_SELECTED, mem->enabled, -1);
			change_made = 1;
			break;
		}
		if (mem)
			break;
	}

	g_free(name);
}

static GtkWidget *get_menubar_menu(GtkWidget *window)
{
	GtkItemFactory *item_factory;
	GtkAccelGroup *accel_group;

	/* Make an accelerator group (shortcut keys) */
	accel_group = gtk_accel_group_new();

	/* Make an ItemFactory (that makes a menubar) */
	item_factory = gtk_item_factory_new(GTK_TYPE_MENU_BAR, "<main>",
					accel_group);

	/* This function generates the menu items. Pass the item factory,
	   the number of items in the array, the array itself, and any
	   callback data for the the menu items. */
	gtk_item_factory_create_items(item_factory, nmenu_items, menu_items, NULL);

	/* Attach the new accelerator group to the window. */
	gtk_window_add_accel_group(GTK_WINDOW(window), accel_group);

	/* Finally, return the actual menu bar created by the item factory. */
	return gtk_item_factory_get_widget(item_factory, "<main>");
}

int run_menu(void)
{
	int argc = 0;
	char **argv = NULL;
	GtkWidget *s_window;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkTreeStore *store;
	struct category *cat;
	struct member *mem;
	GtkWidget *main_vbox;
	GtkWidget *menubar;

	gtk_init(&argc, &argv);
   
   	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_widget_set_size_request(window, 640, 480);
	gtk_window_set_title(GTK_WINDOW(window), "GMenuselect");

	main_vbox = gtk_vbox_new(FALSE, 1);
	gtk_container_set_border_width(GTK_CONTAINER(main_vbox), 1); 
	gtk_container_add(GTK_CONTAINER(window), main_vbox);

	menubar = get_menubar_menu(window);
	gtk_box_pack_start(GTK_BOX(main_vbox), menubar, FALSE, FALSE, 0);

	s_window = gtk_scrolled_window_new(NULL, NULL);

	g_signal_connect(G_OBJECT(window), "delete_event",
	                 G_CALLBACK(delete_event), NULL);
	g_signal_connect(G_OBJECT(window), "destroy",
	                 G_CALLBACK(destroy), NULL);

	store = gtk_tree_store_new(NUM_COLUMNS,
		G_TYPE_STRING,   /* COLUMN_NAME */
		G_TYPE_BOOLEAN,  /* COLUMN_SELECTED */
		G_TYPE_STRING,   /* COLUMN_DEPS */
		G_TYPE_STRING,   /* COLUMN_USES */
		G_TYPE_STRING);  /* COLUMN_CNFS */

	AST_LIST_TRAVERSE(&categories, cat, list) {
		GtkTreeIter iter, iter2;
		gtk_tree_store_append(store, &iter, NULL);
		gtk_tree_store_set(store, &iter,
			COLUMN_NAME, cat->displayname,
			COLUMN_SELECTED, TRUE,
			-1);
		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			char name_buf[64];
			char dep_buf[64] = "";
			char use_buf[64] = "";
			char cnf_buf[64] = "";
			struct reference *dep;
			struct reference *use;
			struct reference *cnf;

			AST_LIST_TRAVERSE(&mem->deps, dep, list) {
				strncat(dep_buf, dep->displayname, sizeof(dep_buf) - strlen(dep_buf) - 1);
				strncat(dep_buf, dep->member ? "(M)" : "(E)", sizeof(dep_buf) - strlen(dep_buf) - 1);
				if (AST_LIST_NEXT(dep, list))
					strncat(dep_buf, ", ", sizeof(dep_buf) - strlen(dep_buf) - 1);
			}
			AST_LIST_TRAVERSE(&mem->uses, use, list) {
				strncat(use_buf, use->displayname, sizeof(use_buf) - strlen(use_buf) - 1);
				if (AST_LIST_NEXT(use, list))
					strncat(use_buf, ", ", sizeof(use_buf) - strlen(use_buf) - 1);
			}
			AST_LIST_TRAVERSE(&mem->conflicts, cnf, list) {
				strncat(cnf_buf, cnf->displayname, sizeof(cnf_buf) - strlen(cnf_buf) - 1);
				strncat(cnf_buf, cnf->member ? "(M)" : "(E)", sizeof(cnf_buf) - strlen(cnf_buf) - 1);
				if (AST_LIST_NEXT(cnf, list))
					strncat(cnf_buf, ", ", sizeof(cnf_buf) - strlen(cnf_buf) - 1);
			}

			if (mem->is_separator) {
				snprintf(name_buf, sizeof(name_buf), "--- %s ---", mem->name);
			} else {
				snprintf(name_buf, sizeof(name_buf), "%s", mem->name);
			}
			if (mem->depsfailed == HARD_FAILURE)
				strncat(name_buf, " (Failed Deps.)", sizeof(name_buf) - strlen(name_buf) - 1);
			if (mem->conflictsfailed == HARD_FAILURE)
				strncat(name_buf, " (In Conflict)", sizeof(name_buf) - strlen(name_buf) - 1);

			gtk_tree_store_append(store, &iter2, &iter);
			gtk_tree_store_set(store, &iter2,
				COLUMN_NAME, name_buf,
				COLUMN_SELECTED, mem->enabled,
				COLUMN_DEPS, dep_buf,
				COLUMN_USES, use_buf,
				COLUMN_CNFS, cnf_buf,
				-1);
		}
	}

	tree = (GtkTreeView *) gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));

#if GTK_CHECK_VERSION(2,10,0)
	gtk_tree_view_set_enable_tree_lines(tree, TRUE);
	gtk_tree_view_set_grid_lines(tree, GTK_TREE_VIEW_GRID_LINES_BOTH);
#endif

	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes("Name",
				renderer, "text", COLUMN_NAME, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

	renderer = gtk_cell_renderer_toggle_new();
	column = gtk_tree_view_column_new_with_attributes("Selected",
				renderer, "active", COLUMN_SELECTED, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);
	g_signal_connect(renderer, "toggled", (GCallback) toggled_handler, store);

	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes("Depends On",
				renderer, "text", COLUMN_DEPS, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes("Can Use",
				renderer, "text", COLUMN_USES, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes("Conflicts With",
				renderer, "text", COLUMN_CNFS, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);
	
	g_signal_connect(tree, "row-activated", (GCallback) row_activated_handler, store);

	gtk_container_add(GTK_CONTAINER(s_window), GTK_WIDGET(tree));

	gtk_box_pack_end(GTK_BOX(main_vbox), s_window, TRUE, TRUE, 0);

	gtk_widget_show_all(window);
   
	gtk_main();

	return main_res;
}
