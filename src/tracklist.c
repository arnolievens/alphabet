/**
 * @author      Arno Lievens (arnolievens@gmail.com)
 * @date        08/09/2021
 * @file        tracklist.c
 * @brief       track storage and file-manager-like widget
 * @copyright   Copyright (c) 2021 Arno Lievens
 */

#include <assert.h>
#include <errno.h>
#include <gtk/gtk.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../include/config.h"
#include "../include/player.h"
#include "../include/track.h"
#include "../include/tracklist.h"

/* static gboolean (*GtkTreeViewSearchEqualFunc) (GtkTreeModel *model, gint column, const gchar *key, GtkTreeIter *iter, gpointer search_data) */

/**
* Play the selected song in player
*
* @param this
* @param selection the selected track (max 1)
*/
static void tracklist_on_selection_changed(Tracklist* this, GtkTreeSelection* selection);

/**
 * function used by load_thread threadpool to async load tracks
 *
 * @param data file closure from thread_pool_push call
 * @param user_data tracklist object closure from thread_pool_new
 */
static void tracklist_load_async(gpointer data, gpointer user_data);


/*******************************************************************************
 * extern functions
 */

typedef enum TRACKLIST_TABLE_ENTRY {
    TRACKLIST_TABLE_ENTRY_STRING,
    TRACKLIST_TABLE_ENTRY_GTK_TREE_MODEL_ROW,
    TRACKLIST_TABLE_ENTRY_WAV,
    TRACKLIST_TABLE_ENTRY_COUNT
} TRACKLIST_TABLE_ENTRY;


GtkTargetEntry entries[] = {
    {"STRING", 0, TRACKLIST_TABLE_ENTRY_STRING},
    {"GTK_TREE_MODEL_ROW", GTK_TARGET_SAME_WIDGET, TRACKLIST_TABLE_ENTRY_GTK_TREE_MODEL_ROW},
    {"audio/x-wav", 0, TRACKLIST_TABLE_ENTRY_WAV},
    /* {"text/uri-list", 0, 3} */
};

Tracklist* tracklist_new(Player* player)
{
    GError* err = NULL;
    Tracklist* this = malloc(sizeof(Tracklist));
    this->player = player;

    this->list = gtk_list_store_new(
            TRACKLIST_COLUMNS,
            G_TYPE_STRING,
            G_TYPE_STRING,
            G_TYPE_STRING,
            G_TYPE_STRING,
            G_TYPE_POINTER);

    /* create threadpool for async loading of files */
    /* files can be added: g_thread_pool_push(this->load_thread, file, &err); */
    /* FIXME only works for MAX THREADS = -1 - segfult for limited threads*/
    this->load_thread = g_thread_pool_new(
            tracklist_load_async, this, -1, FALSE, &err);

    if (err) {
        g_printerr("%s\n", err->message);
        return NULL;
    }
    return this;
}


void
drag_data_get (GtkWidget        *widget,
               GdkDragContext   *context,
               GtkSelectionData *selection_data,
               guint             info,
               guint             time,
               gpointer          data)
{
    printf("get\n");
}

static void drag_begin (GtkTreeView *tree, GdkDragContext *context, Tracklist* this)
{
    printf("begin\n");
}

static void drag_data_received(
        GtkWidget *tree, GdkDragContext *ctx, gint x, gint y,
        GtkSelectionData *selection, guint info, guint32 time, Tracklist* this)
{
    /* tfw why \r ?? */
    gint wx, wy;
    const gchar* delim = "\r\n";
    const guchar* uris = gtk_selection_data_get_data(selection);

    gchar* type = gdk_atom_name(gtk_selection_data_get_target(selection));

    GtkTreePath* path = gtk_tree_path_new();

    printf("x: %d, y: %d\n", x, y);

    gtk_tree_view_convert_widget_to_bin_window_coords(this->tree, x, y, &wx, &wy);
    gtk_tree_view_get_path_at_pos(this->tree, wx, wy, &path, NULL, NULL, NULL);

    if (g_strcmp0(type, "STRING") == 0) {
        char* str = g_strdup((const char*)uris);
        char* uri;
        uri = strtok(str, delim);
        do {
            GFile* file = g_file_new_for_uri(uri);
            tracklist_insert_file(this, file, path);
        } while ((uri = strtok(NULL, delim)));

        g_free(str);
    }

    gtk_drag_finish(ctx, TRUE, FALSE, time);
    g_free(type);
}

void tracklist_init(Tracklist* this)
{
    GtkTreeSelection* selection;
    GtkTreeViewColumn* column;
    GtkCellRenderer* cellrenderer;
    int id;

    this->tree = GTK_TREE_VIEW(
            gtk_tree_view_new_with_model(GTK_TREE_MODEL(this->list)));

    gtk_tree_view_set_reorderable(this->tree, TRUE);
    gtk_tree_view_set_enable_search(this->tree, FALSE);

    selection = gtk_tree_view_get_selection(this->tree);
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_BROWSE);

    g_signal_connect_swapped(
            selection,
            "changed",
            G_CALLBACK(tracklist_on_selection_changed),
            this);

    id = TRACKLIST_COLUMN_NAME;
    column = gtk_tree_view_column_new();
    gtk_tree_view_append_column(this->tree, column);
    cellrenderer = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(column, cellrenderer, FALSE);
    gtk_tree_view_column_set_resizable(column, FALSE);
    gtk_tree_view_column_set_clickable(column, TRUE);
    gtk_tree_view_column_add_attribute(column, cellrenderer, "text", id);
    gtk_tree_view_column_set_title(column, "Track");
    gtk_tree_view_column_set_expand(column, TRUE);
    g_object_set(G_OBJECT(cellrenderer), "ellipsize", PANGO_ELLIPSIZE_END, NULL);
    /* gtk_tree_view_column_set_sort_column_id(column, id); */
    gtk_tree_view_set_search_column(this->tree, TRACKLIST_COLUMN_NAME);

    id = TRACKLIST_COLUMN_LUFS;
    column = gtk_tree_view_column_new();
    gtk_tree_view_append_column(this->tree, column);
    gtk_tree_view_column_set_alignment(column, 0.5);
    cellrenderer = gtk_cell_renderer_text_new();
    gtk_cell_renderer_set_alignment(cellrenderer, 0.5, 0.0);
    gtk_tree_view_column_pack_start(column, cellrenderer, FALSE);
    gtk_tree_view_column_set_resizable(column, FALSE);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_clickable(column, TRUE);
    gtk_tree_view_column_add_attribute(column, cellrenderer, "text", id);
    gtk_tree_view_column_set_title(column, "LUFs");
    gtk_tree_view_column_set_expand(column, FALSE);
    /* gtk_tree_view_column_set_sort_column_id(column, id); */

    id = TRACKLIST_COLUMN_PEAK;
    column = gtk_tree_view_column_new();
    gtk_tree_view_append_column(this->tree, column);
    gtk_tree_view_column_set_alignment(column, 0.5);
    cellrenderer = gtk_cell_renderer_text_new();
    gtk_cell_renderer_set_alignment(cellrenderer, 0.5, 0.0);
    gtk_tree_view_column_pack_start(column, cellrenderer, FALSE);
    gtk_tree_view_column_set_resizable(column, FALSE);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_clickable(column, TRUE);
    gtk_tree_view_column_add_attribute(column, cellrenderer, "text", id);
    gtk_tree_view_column_set_title(column, "Peak");
    gtk_tree_view_column_set_expand(column, FALSE);
    /* gtk_tree_view_column_set_sort_column_id(column, id); */

    id = TRACKLIST_COLUMN_DURATION;
    column = gtk_tree_view_column_new();
    gtk_tree_view_append_column(this->tree, column);
    gtk_tree_view_column_set_alignment(column, 0.5);
    cellrenderer = gtk_cell_renderer_text_new();
    gtk_cell_renderer_set_alignment(cellrenderer, 0.5, 0.0);
    gtk_tree_view_column_pack_start(column, cellrenderer, FALSE);
    gtk_tree_view_column_set_resizable(column, FALSE);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_clickable(column, TRUE);
    gtk_tree_view_column_add_attribute(column, cellrenderer, "text", id);
    gtk_tree_view_column_set_title(column, "Duration");
    gtk_tree_view_column_set_expand(column, FALSE);
    /* gtk_tree_view_column_set_sort_column_id(column, id); */

    gtk_drag_source_set(GTK_WIDGET(this->tree), GDK_BUTTON1_MASK, entries, 1, GDK_ACTION_MOVE);
    g_signal_connect(GTK_WIDGET(this->tree), "drag-begin", G_CALLBACK (drag_begin), this);

    gtk_drag_dest_set(GTK_WIDGET(this->tree), GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_HIGHLIGHT, NULL, 1, GDK_ACTION_MOVE);
    g_signal_connect(this->tree, "drag-data-received", G_CALLBACK(drag_data_received), this);

    GtkTargetList* target_list = gtk_target_list_new(NULL, 0);
    gdk_atom_intern(entries[TRACKLIST_TABLE_ENTRY_STRING].target, TRUE);
    gdk_atom_intern(entries[TRACKLIST_TABLE_ENTRY_GTK_TREE_MODEL_ROW].target, TRUE);
    gtk_target_list_add_table(target_list, entries, TRACKLIST_TABLE_ENTRY_COUNT);
    gtk_drag_dest_set_target_list(GTK_WIDGET(this->tree), target_list);

    /* g_signal_connect(this->tree, "drag-motion", G_CALLBACK(drag_motion), NULL); */
    /* g_signal_connect(this->tree, "drag-drop", G_CALLBACK(drag_data_received), NULL); */

    gtk_widget_show_all(GTK_WIDGET(this->tree));
}

void tracklist_add_track(Tracklist* this, Track* track, GtkTreePath* after)
{
    if (!track) return;
    GtkTreeIter iter, prev;

    gchar lufs[7];
    g_snprintf(lufs, G_N_ELEMENTS(lufs), "%.2f", track->lufs);

    gchar peak[7];
    g_snprintf(peak, G_N_ELEMENTS(peak), "%.2f", track->peak);

    gchar duration[10];
    dtoduration(duration, track->length);

    if (after && gtk_tree_model_get_iter(GTK_TREE_MODEL(this->list), &prev, after)) {
        gtk_list_store_insert_after(this->list, &iter, &prev);
    } else {
        gtk_list_store_append(this->list, &iter);
    }
    gtk_list_store_set(
            this->list, &iter,
            TRACKLIST_COLUMN_NAME, track->name,
            TRACKLIST_COLUMN_LUFS, lufs,
            TRACKLIST_COLUMN_PEAK, peak,
            TRACKLIST_COLUMN_DURATION, duration,
            TRACKLIST_COLUMN_DATA, track,
            -1);

    this->min_lufs = MIN(this->min_lufs, track->lufs);
    this->player->min_lufs = this->min_lufs;

    gtk_tree_path_free(after);
}

Track* tracklist_file_to_track(UNUSED Tracklist* this, GFile* file)
{
    if (!G_IS_FILE(file)) return NULL;
    const char* type;
    char* name, * path;
    GError *err = NULL;
    GFileInfo* info;

        printf("path: %s\n", g_file_get_path(file));
        printf("name: %s\n", g_file_get_basename(file));
    info = g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
            G_FILE_QUERY_INFO_NONE, NULL, &err);

    /* file-open errors - report and cancel */
    if (err) {
        g_printerr("%s\n", err->message);
        g_error_free(err);
        return NULL;
    }

    name = g_file_get_basename(file);
    path = g_file_get_path(file);

    /* mime type not readable ?? */
    if (!(type = g_file_info_get_content_type(info))) {
        g_printerr("Error getting mimetype for file \"%s\"\n", path);
        goto fail;
    }

    /* invalid file*/
    if (!g_strstr_len(type, -1, "audio")) {
        g_printerr("Error loading file \"%s\": Not and audio file\n", path);
        goto fail;
    }

    Track* track = track_new(name, path);
    g_object_unref(info);
    g_free(name);
    g_free(path);
    return track;

fail:
    g_object_unref(info);
    g_free(name);
    g_free(path);
    return NULL;
}

LoadFileData* loadfiledata_new(GFile* file, GtkTreePath* path)
{
    LoadFileData* this = malloc(sizeof(LoadFileData));
    this->file = file;
    this->position = path;
    return this;
}

void loadfiledata_free(LoadFileData* this)
{
    free(this);
}

void tracklist_insert_file(Tracklist* this, GFile* file, GtkTreePath* path)
{
    GError* err = NULL;
    LoadFileData* data = loadfiledata_new(file, path);
    g_thread_pool_push(this->load_thread, data, &err);
    if (err) {
        g_printerr("%s\n", err->message);
    }
}

void tracklist_append_file(Tracklist* this, GFile* file)
{
    tracklist_insert_file(this, file, NULL);
}

void tracklist_remove_selected(Tracklist* this)
{
    GtkTreeSelection* selection = gtk_tree_view_get_selection(this->tree);
    Track* track;
    GtkTreeIter iter;
    GtkTreeModel* model = GTK_TREE_MODEL(this->list);
    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) return;
    gtk_tree_model_get(model, &iter, TRACKLIST_COLUMN_DATA, &track, -1);
    gtk_list_store_remove(this->list, &iter);
    track_free(track);
}

void tracklist_free(Tracklist* this)
{
    if (!this) return;

    GtkTreeModel* model = GTK_TREE_MODEL(this->list);
    Track* track;
    GtkTreeIter iter;
    GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(this->tree));

    g_signal_handlers_disconnect_by_data(selection, this);

    gtk_tree_selection_get_selected(selection, &model, &iter);

    while (gtk_list_store_iter_is_valid(this->list, &iter)) {
        gtk_tree_model_get(model, &iter, TRACKLIST_COLUMN_DATA, &track, -1);
        track_free(track);
        gtk_list_store_remove(this->list, &iter);
    }

    this->player->current = NULL;

    gtk_widget_destroy(GTK_WIDGET(this->tree));
    g_object_unref(this->list);
    g_free(this);
}


/*******************************************************************************
 * static functions
 *
 */

void tracklist_on_selection_changed(Tracklist* this, GtkTreeSelection* selection)
{
    Track* track;
    GtkTreeIter iter;
    /* TODO don't need to set model, its set in ..._get_selection */
    GtkTreeModel* model = GTK_TREE_MODEL(this->list);

    gtk_tree_selection_get_selected(selection, &model, &iter);
    if (!gtk_list_store_iter_is_valid(this->list, &iter)) return;
    gtk_tree_model_get(model, &iter, TRACKLIST_COLUMN_DATA, &track, -1);

    gdouble position = 0.0;
    if (this->player->marker) position = this->player->marker;
    else if (!this->player->rtn) position = player_update(this->player);
    player_load_track(this->player, track, position);
}

void tracklist_load_async(gpointer data, gpointer user_data)
{
    Tracklist* this = user_data;
    Track* track = tracklist_file_to_track(this, ((LoadFileData*)data)->file);
    /* this shouldn't be wrapped in g_idle_ad ?? */
    if (track) tracklist_add_track(this, track, ((LoadFileData*)data)->position);
    loadfiledata_free(data);
}
