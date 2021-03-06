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

/**
* Play the selected song in player
*
* @param this
* @param selection the selected track (max 1)
*/
static void selection_changed(Tracklist* this, GtkTreeSelection* selection);

/**
 * function used by load_thread threadpool to async load tracks
 *
 * @param data file closure from thread_pool_push call
 * @param user_data tracklist object closure from thread_pool_new
 */
static void load_async(gpointer data, gpointer user_data);

/*
 * Drag-and-Drop signal handlers
 */
static void drag_begin(GtkTreeView *tree, GdkDragContext *ctx, Tracklist* this);

static gboolean drag_motion(GtkTreeView* tree, GdkDragContext* ctx, gint x,
gint y, guint time, gpointer user_data);

static gboolean drag_drop(GtkTreeView* tree, GdkDragContext* ctx, gint x,
gint y, guint time, Tracklist* this);

static void drag_data_get (GtkTreeView* tree, GdkDragContext* ctx,
GtkSelectionData* selection, guint info, guint time, gpointer data);

static void drag_data_received(GtkTreeView* tree, GdkDragContext *ctx, gint x,
gint y, GtkSelectionData *selection, guint info, guint32 time, Tracklist* this);

static void drag_leave(GtkWidget* tree, GdkDragContext* ctx, guint time,
gpointer user_data);

static void drag_data_delete(GtkWidget* tree, GdkDragContext* ctx, gpointer);

static gboolean drag_data_failed(GtkWidget *tree, GdkDragContext *ctx,
GtkDragResult result, gpointer user_data);

/**
 * Target entries
 *
 * these are used to set the gdk atoms
 *
 * ignore const qual warning
 * GtkTargetEntry does not change strings
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdiscarded-qualifiers"
    static GtkTargetEntry entries[TRACKLIST_ENTRY_TOT] = {
        [TRACKLIST_ENTRY_ROW] = {
            "GTK_TREE_MODEL_ROW",
            GTK_TARGET_SAME_WIDGET,
            TRACKLIST_ENTRY_ROW
        },
        [TRACKLIST_ENTRY_STR] = {
            "STRING",
            GTK_TARGET_OTHER_APP,
            TRACKLIST_ENTRY_STR
        },
        [TRACKLIST_ENTRY_WAV] = {
            "audio/x-wav",
            GTK_TARGET_OTHER_APP,
            TRACKLIST_ENTRY_WAV
        },
        /* {"text/uri-list", 0, 3} */
    };
#pragma GCC diagnostic pop


/*******************************************************************************
 * extern functions
 */

Tracklist* tracklist_new(Player* player)
{
    GError* err = NULL;
    Tracklist* this = malloc(sizeof(Tracklist));
    this->player = player;
    this->min_lufs = 0.0;
    this->tree = NULL;

    this->list = gtk_list_store_new(TRACKLIST_COLUMNS,
                                    G_TYPE_STRING,      /* NAME */
                                    G_TYPE_STRING,      /* LUFS */
                                    G_TYPE_STRING,      /* PEAK */
                                    G_TYPE_STRING,      /* DURATION */
                                    G_TYPE_POINTER);    /* DATA */

    /* create threadpool for async loading of files
     * files can be added: g_thread_pool_push(this->load_thread, file, &err);
     * functions to add track from file asynchronously
     *  - tracklist_append_file
     *  - tracklist_inset_file
     * FIXME: only works for MAX THREADS = -1 - segfault for limited threads
     */

    this->load_thread = g_thread_pool_new(
            load_async, this, -1, FALSE, &err);

    if (err) {
        g_printerr("%s\n", err->message);
        tracklist_free(this);
        return NULL;
    }
    return this;
}

void tracklist_init(Tracklist* this)
{
    GtkTreeSelection* selection;
    GtkTreeViewColumn* column;
    GtkCellRenderer* cellrender;
    TracklistColum id;
    GtkTreeModel* model = GTK_TREE_MODEL(this->list);

    assert(this->tree == NULL && "tracklist already initialized)");

    this->tree = GTK_TREE_VIEW(gtk_tree_view_new_with_model(model));

    gtk_tree_view_set_enable_search(this->tree, TRUE);

    selection = gtk_tree_view_get_selection(this->tree);
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_BROWSE);

    g_signal_connect_swapped( selection, "changed",
            G_CALLBACK(selection_changed), this);

    id = TRACKLIST_COLUMN_NAME;
    column = gtk_tree_view_column_new();
    gtk_tree_view_append_column(this->tree, column);
    cellrender = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(column, cellrender, FALSE);
    gtk_tree_view_column_set_resizable(column, FALSE);
    gtk_tree_view_column_set_clickable(column, TRUE);
    gtk_tree_view_column_add_attribute(column, cellrender, "text", (gint)id);
    gtk_tree_view_column_set_title(column, "Track");
    gtk_tree_view_column_set_expand(column, TRUE);
    g_object_set(G_OBJECT(cellrender), "ellipsize", PANGO_ELLIPSIZE_END, NULL);
    gtk_tree_view_column_set_sort_column_id(column, (gint)id);
    gtk_tree_view_set_search_column(this->tree, TRACKLIST_COLUMN_NAME);

    id = TRACKLIST_COLUMN_LUFS;
    column = gtk_tree_view_column_new();
    gtk_tree_view_append_column(this->tree, column);
    gtk_tree_view_column_set_alignment(column, 0.5);
    cellrender = gtk_cell_renderer_text_new();
    gtk_cell_renderer_set_alignment(cellrender, 0.5, 0.0);
    gtk_tree_view_column_pack_start(column, cellrender, FALSE);
    gtk_tree_view_column_set_resizable(column, FALSE);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_clickable(column, TRUE);
    gtk_tree_view_column_add_attribute(column, cellrender, "text", (gint)id);
    gtk_tree_view_column_set_title(column, "LUFs");
    gtk_tree_view_column_set_expand(column, FALSE);
    gtk_tree_view_column_set_sort_column_id(column, (gint)id);

    id = TRACKLIST_COLUMN_PEAK;
    column = gtk_tree_view_column_new();
    gtk_tree_view_append_column(this->tree, column);
    gtk_tree_view_column_set_alignment(column, 0.5);
    cellrender = gtk_cell_renderer_text_new();
    gtk_cell_renderer_set_alignment(cellrender, 0.5, 0.0);
    gtk_tree_view_column_pack_start(column, cellrender, FALSE);
    gtk_tree_view_column_set_resizable(column, FALSE);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_clickable(column, TRUE);
    gtk_tree_view_column_add_attribute(column, cellrender, "text", (gint)id);
    gtk_tree_view_column_set_title(column, "Peak");
    gtk_tree_view_column_set_expand(column, FALSE);
    gtk_tree_view_column_set_sort_column_id(column, (gint)id);

    id = TRACKLIST_COLUMN_DURATION;
    column = gtk_tree_view_column_new();
    gtk_tree_view_append_column(this->tree, column);
    gtk_tree_view_column_set_alignment(column, 0.5);
    cellrender = gtk_cell_renderer_text_new();
    gtk_cell_renderer_set_alignment(cellrender, 0.5, 0.0);
    gtk_tree_view_column_pack_start(column, cellrender, FALSE);
    gtk_tree_view_column_set_resizable(column, FALSE);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_clickable(column, TRUE);
    gtk_tree_view_column_add_attribute(column, cellrender, "text", (gint)id);
    gtk_tree_view_column_set_title(column, "Duration");
    gtk_tree_view_column_set_expand(column, FALSE);
    gtk_tree_view_column_set_sort_column_id(column, (gint)id);

    /* Drag-and-Drop setup
     *
     * the tree is dnd source and destination
     * dnd can be initiated from within the tree to reorder tracks
     * as well as from a file manager to import new files
     *
     * different handling methods are based on the gdk atoms
     */

    GtkTargetList* target_list = gtk_target_list_new(NULL, 0);
    gdk_atom_intern(entries[TRACKLIST_ENTRY_STR].target, TRUE);
    gdk_atom_intern(entries[TRACKLIST_ENTRY_ROW].target, TRUE);
    gdk_atom_intern(entries[TRACKLIST_ENTRY_WAV].target, TRUE);
    gtk_target_list_add_table(target_list, entries, TRACKLIST_ENTRY_TOT);

    gtk_tree_view_enable_model_drag_source(this->tree,
            GDK_BUTTON1_MASK,
            entries,
            TRACKLIST_ENTRY_ROW+1,
            GDK_ACTION_MOVE);

    gtk_tree_view_enable_model_drag_dest(this->tree,
            entries,
            TRACKLIST_ENTRY_TOT,
            GDK_ACTION_MOVE);

    g_signal_connect(this->tree, "drag-begin",
            G_CALLBACK(drag_begin), this);

    g_signal_connect(this->tree, "drag-motion",
            G_CALLBACK(drag_motion), this);

    g_signal_connect(this->tree, "drag-drop",
            G_CALLBACK(drag_drop), this);

    g_signal_connect(this->tree, "drag-data-get",
            G_CALLBACK(drag_data_get), this);

    g_signal_connect(this->tree, "drag-data-received",
            G_CALLBACK(drag_data_received), this);

    g_signal_connect(this->tree, "drag-failed",
            G_CALLBACK(drag_data_failed), this);

    g_signal_connect(this->tree, "drag-data-delete",
            G_CALLBACK(drag_data_delete), this);

    g_signal_connect(this->tree, "drag-leave",
            G_CALLBACK(drag_leave), this);

    gtk_widget_show_all(GTK_WIDGET(this->tree));
}

void tracklist_add_track(Tracklist* this, Track* track, GtkTreePath* path,
GtkTreeViewDropPosition pos)
{
    if (!track) return;
    GtkTreeIter iter, prev;
    GtkTreeModel* model = GTK_TREE_MODEL(this->list);

    gchar lufs[7];
    g_snprintf(lufs, G_N_ELEMENTS(lufs), "%.2f", track->lufs);

    gchar peak[7];
    g_snprintf(peak, G_N_ELEMENTS(peak), "%.2f", track->peak);

    gchar duration[10];
    dtoduration(duration, track->length);

    if (path && gtk_tree_model_get_iter(model, &prev, path)) {
        switch (pos) {
            case GTK_TREE_VIEW_DROP_BEFORE:
            case GTK_TREE_VIEW_DROP_INTO_OR_BEFORE:
                gtk_list_store_insert_before(this->list, &iter, &prev);
                break;

            case GTK_TREE_VIEW_DROP_AFTER:
            case GTK_TREE_VIEW_DROP_INTO_OR_AFTER:
            default:
                gtk_list_store_insert_after(this->list, &iter, &prev);
        }
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

    /* set the min_lufs value to the lowest loudness
     * min_should accurately contain the lowest value even after deleting tracks
     * so it's not neccessary to run through all tracks on each add
     * min_lufs is stored in tracklist but must be sit in player to take effect
     */

    this->min_lufs = MIN(this->min_lufs, track->lufs);
    this->player->min_lufs = this->min_lufs;
}

Track* tracklist_file_to_track(UNUSED Tracklist* this, GFile* file)
{
    if (!G_IS_FILE(file)) return NULL;
    Track* track = NULL;
    const gchar* type;
    const gchar* name = NULL;
    gchar* path = NULL;
    GError *err = NULL;
    GFileInfo* info;

    path = g_file_get_path(file);

    /* get mimetype */
    info = g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
            G_FILE_QUERY_INFO_NONE, NULL, &err);
    if (err) {
        g_printerr("%s\n", err->message);
        g_error_free(err);
        goto fail;
    }
    if (!(type = g_file_info_get_content_type(info))) {
        g_printerr("Error getting mimetype for file \"%s\"\n", path);
        goto fail;
    }

    /* TODO: find a better way (lib?) to determine file == audio file??? */
    if (    !g_strstr_len(type, -1, "audio") &&
            !g_strstr_len(type, -1, "org.xiph.flac"))
    {
        g_printerr("Error loading file \"%s\": Not and audio file\n", path);
        goto fail;
    }

    /* get filename */
    info = g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME,
            G_FILE_QUERY_INFO_NONE, NULL, &err);
    if (err) {
        g_printerr("%s\n", err->message);
        g_error_free(err);
        goto fail;
    }
    if (!(name = g_file_info_get_display_name(info))) {
        g_printerr("Error getting display name for file \"%s\"\n", path);
        goto fail;
    }

    track = track_new(name, path);

fail:
    /* delete file here if creating track failed
     * because it cannot be unreffed in load_async */
    if (!track) g_object_unref(file);
    if (info) g_object_unref(info);
    g_free(path);
    return track;
}

void tracklist_insert_file(Tracklist* this, GFile* file, GtkTreePath* path,
GtkTreeViewDropPosition pos)
{
    GError* err = NULL;

    /* allocate position to allow storing in GObject, free-ed by async loader */
    GtkTreeViewDropPosition* position = malloc(sizeof(GtkTreeViewDropPosition));
    *position = pos;

    g_object_set_data(G_OBJECT(file), "path", path);
    g_object_set_data(G_OBJECT(file), "position", position);

    /* send data to load thread pool */
    g_thread_pool_push(this->load_thread, file, &err);
    if (err) {
        g_printerr("%s\n", err->message);
    }
}

void tracklist_append_file(Tracklist* this, GFile* file)
{
    /* use the functionality of insert_file but set path to NULL to append */

    tracklist_insert_file(this, file, NULL, 0);
}

void tracklist_update_min_lufs(Tracklist* this)
{
    GtkTreeIter iter;
    GtkTreeModel* model;
    gdouble min = 0.0;

    model = gtk_tree_view_get_model(this->tree);

    /* get the first track, when no tracks left, min_lufs must be set to 0.0
     * to make sure adding new tracks get proper min_lufs value in add_track
     */

    if (!(gtk_tree_model_get_iter_first(model, &iter))) {
        this->min_lufs = 0.0;
        return;
    }

    do {
        Track* track;
        gtk_tree_model_get(model, &iter, TRACKLIST_COLUMN_DATA, &track, -1);
        min = MIN(track->lufs, min);

    } while (gtk_tree_model_iter_next(model, &iter));

     /* min_lufs is stored in tracklist but must be updated in player to take
      * effect
      */

    this->min_lufs = min;
    this->player->min_lufs = this->min_lufs;
}

void tracklist_remove_selected(Tracklist* this)
{
    GtkTreeSelection* selection = gtk_tree_view_get_selection(this->tree);
    Track* track;
    GtkTreeIter iter;
    GtkTreeModel* model = GTK_TREE_MODEL(this->list);

    /* when removing a track, the selection_changed handler will be called as
     * the next track in line will be selected automatically
     */

    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) return;
    gtk_tree_model_get(model, &iter, TRACKLIST_COLUMN_DATA, &track, -1);
    gtk_list_store_remove(this->list, &iter);
    track_free(track);

    /* it's possible the removed track had the lowest loudness so we must
     * update min_lufs now
     */

    tracklist_update_min_lufs(this);
}

void tracklist_free(Tracklist* this)
{
    if (!this) return;

    Track* track;
    GtkTreeIter iter;
    GtkTreeSelection* selection;
    GtkTreeModel* model = GTK_TREE_MODEL(this->list);

    /* free each track contained in tracklist before destroying ourselves
     * we must first disconnect the selction signal handler or tracklist will
     * trigger play after each trag that gets deleted and selection moves to
     * the next track
     */

    if (this->tree) {
        selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(this->tree));
        g_signal_handlers_disconnect_by_data(selection, this);
    }

    gtk_tree_model_get_iter_first(model, &iter);

    while (gtk_list_store_iter_is_valid(this->list, &iter)) {
        gtk_tree_model_get(model, &iter, TRACKLIST_COLUMN_DATA, &track, -1);
        track_free(track);
        gtk_list_store_remove(this->list, &iter);
    }

    if (this->player) this->player->current = NULL;

    g_thread_pool_free(this->load_thread, FALSE, FALSE);
    g_object_unref(this->list);
    if (this->tree) {

        gtk_widget_destroy(GTK_WIDGET(this->tree));
    }
    g_free(this);
}


/*******************************************************************************
 * static functions
 *
 */

void selection_changed(Tracklist* this, GtkTreeSelection* selection)
{
    Track* track;
    GtkTreeIter iter;
    GtkTreeModel* model;

    /* get the currently selected track and ask the player to load it
     * if there's no track selected (eg the last one was removed), the player
     * is asked to stop and the current track is reset
     */

    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
        player_stop(this->player);
        this->player->current = NULL;
        return;
    }

    gtk_tree_model_get(model, &iter, TRACKLIST_COLUMN_DATA, &track, -1);
    player_load_track(this->player, track);
}

void load_async(gpointer file_data, gpointer tracklist_data)
{
    /* the load-async handler is invoked when the thread_pool receives new data
     * (called by g_thread_pool_push() in insert_file() or append_file()
     * the file GObject in "file_data" has the row (path) as well as the
     * before/after (pos) information embedded as data
     * it is extracted here to call add_track
     * file is unreffed here as it needs to stay alive until file_to_track
     * has finished
     */

    Tracklist* this = tracklist_data;
    GFile* file = file_data;
    GtkTreePath* path = g_object_get_data(G_OBJECT(file), "path");
    GtkTreeViewDropPosition* pos = g_object_get_data(G_OBJECT(file), "position");
    Track* track = tracklist_file_to_track(this, file);
    if (track) tracklist_add_track(this, track, path, *pos);

    gtk_tree_path_free(path);
    free(pos);
    g_object_unref(file);
}

void drag_begin(UNUSED GtkTreeView *tree, UNUSED GdkDragContext *ctx,
UNUSED Tracklist* this)
{
    /* printf("drag-begin\n"); */
}

gboolean drag_motion(GtkTreeView* tree, GdkDragContext* ctx, gint x, gint y,
guint time, UNUSED gpointer user_data)
{
    /* drag-motion handler is connected to the drag destination and therefore
     * relevant to drops from within the tree as well as external files
     * this hanler is responsible for notifying the tree which row to highlight
     */

    GdkAtom target = gtk_drag_dest_find_target(GTK_WIDGET(tree), ctx, NULL);

    if (target != GDK_NONE) {
        GtkTreePath* path;
        GtkTreeViewDropPosition pos;
        gtk_tree_view_get_dest_row_at_pos(tree, x, y, &path, &pos);
        gtk_tree_view_set_drag_dest_row(tree, path, pos);
        gtk_tree_path_free(path);

        GdkDragAction action = gdk_drag_context_get_suggested_action(ctx);
        gdk_drag_status(ctx, action, time);
        return TRUE;
    }
    gdk_drag_status(ctx, 0, time);

    return FALSE;
}

gboolean drag_drop(GtkTreeView* tree, GdkDragContext* ctx, UNUSED gint x,
UNUSED gint y, guint time, Tracklist* this)
{

    /* data-drop handler is connected to the drag destination and therefore
     * relevant to drops from within the tree as well as external files
     * we call get_data() here in order to override stock data-received handler
     * first we set the ListStore to un-sortable to allow drops in the tree
     */

    GdkAtom target = gtk_drag_dest_find_target(GTK_WIDGET(tree), ctx, NULL);
    if (target != GDK_NONE) {
        gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(this->list),
                GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID, 0);

        gtk_drag_get_data(GTK_WIDGET(tree), ctx, target, time);
        return TRUE;
    }
    return FALSE;
}

void drag_data_get (GtkTreeView* tree, UNUSED GdkDragContext* ctx,
GtkSelectionData* selection, guint info, UNUSED guint time, UNUSED gpointer dat)
{
    /* data-get handler is connected to the drag source and therefore
     * only relevant to drags from within the tree
     * we grab the row selected and load into selection
     * it will be picked up by data-received handler
     */

    switch (info) {
        case TRACKLIST_ENTRY_ROW: {

            GtkTreeModel* model;
            GtkTreeIter iter;
            GtkTreePath* path;
            GtkTreeSelection* tree_selection;

            tree_selection = GTK_TREE_SELECTION(gtk_tree_view_get_selection(tree));

            gtk_tree_selection_get_selected(tree_selection, &model, &iter);
            path = gtk_tree_model_get_path(model, &iter);
            gtk_tree_set_row_drag_data(selection, model, path);

            gtk_tree_path_free(path);
            break;
            }
        default: break;
    }
}


void drag_data_received(GtkTreeView* tree, GdkDragContext *ctx, gint x, gint y,
GtkSelectionData *selection, guint info, guint32 time, Tracklist* this)
{
    /* drag-data-received handler is connected to the drag-destination
     * the signal is invoked by drag-data-get and overrides the default handler
     * first we determine if the selection contains a TREE_ROW or STRING target
     * a TREE_ROW is moved in place, an external file (STR) is inserted in the
     * tracklist
     */

    switch (info) {

        /* external file handler */
        case TRACKLIST_ENTRY_STR: {

            GtkTreePath* path;
            GtkTreeViewDropPosition pos;
            char* uri, * str;
            const gchar* delim = "\r\n";
            const guchar* uris = gtk_selection_data_get_data(selection);

            /* the STRING recevied in "selection" is a list of uris provided by
             * the unerlying dnd handler
             * somehow the file uris are line separated with <CR><LF>
             * we split them here and create files from uri for each line
             *
             * the destination row position (path) is retrieved here but
             * free-ed by the async_loader
             */

            gtk_tree_view_get_dest_row_at_pos(tree, x, y, &path, &pos);

            str = g_strdup((const char*)uris);
            uri = strtok(str, delim);
            do {
                GFile* file = g_file_new_for_uri(uri);
                tracklist_insert_file(this, file, path, pos);
            } while ((uri = strtok(NULL, delim)));

            g_free(str);
            gtk_drag_finish(ctx, TRUE, FALSE, time);
            break;
        }

        /* TREE_ROW handler */
        case TRACKLIST_ENTRY_ROW: {

            GtkTreeModel* model;
            GtkTreePath* src_path, *dst_path;
            GtkTreeIter src_iter, dst_iter;
            GtkTreeIter* destination = NULL;
            GtkTreeViewDropPosition pos = GTK_TREE_VIEW_DROP_AFTER;

            /* row is manually moved to the proper destination
             * using the default hadler for dnd'ing TREE_ROWS could have been
             * an option but unfortunately buggish on MacOs
             */

            gtk_tree_get_row_drag_data(selection, &model, &src_path);
            gtk_tree_model_get_iter(model, &src_iter, src_path);

            if (gtk_tree_view_get_dest_row_at_pos(tree, x, y, &dst_path, &pos)) {
                gtk_tree_model_get_iter(model, &dst_iter, dst_path);
                destination = &dst_iter;
            }

            /* when no drop position found, (drop released underneath last row)
             * the row should be appended at the end of the list by calling
             * ..._move_before(..., ..., NULL) which is a bit counterinuitive
             */

            if (pos == GTK_TREE_VIEW_DROP_BEFORE || !destination) {
                gtk_list_store_move_before(this->list, &src_iter, destination);
            } else {
                gtk_list_store_move_after(this->list, &src_iter, destination);
            }

            gtk_tree_path_free(src_path);
            gtk_tree_path_free(dst_path);

            gtk_drag_finish(ctx, TRUE, FALSE, time);
            break;
        }

        default: printf("DEBUG: INVALID GDK ATOM\n"); break;
    }

    gtk_drag_finish(ctx, FALSE, FALSE, time);
}

void drag_leave(UNUSED GtkWidget* tree, UNUSED GdkDragContext* ctx,
UNUSED guint time, UNUSED gpointer user_data)
{
    /* printf("leave\n"); */
}

void drag_data_delete(UNUSED GtkWidget* tree, UNUSED GdkDragContext* ctx,
UNUSED gpointer user_data)
{
    /* printf("drag-delete\n"); */
}

gboolean drag_data_failed(UNUSED GtkWidget *tree, UNUSED GdkDragContext *ctx,
UNUSED GtkDragResult result, UNUSED gpointer user_data)
{
    /* printf("drag-failed: %d\n", result); */
    return FALSE;
}
