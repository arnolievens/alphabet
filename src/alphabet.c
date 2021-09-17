/**
 * @author      : Arno Lievens (arnolievens@gmail.com)
 * @created     : 08/09/2021
 * @filename    : window.c
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

#ifdef MAC_INTEGRATION
#include <gtkosxapplication.h>
#endif

#include "../include/config.h"
#include "../include/config.h"
#include "../include/counter.h"
#include "../include/player.h"
#include "../include/timeline.h"
#include "../include/track.h"
#include "../include/tracklist.h"
#include "../include/transport.h"
#include "../include/varispeed.h"

Counter* counter;
Player* player;
Timeline* timeline;
Tracklist* tracklist;
Transport* transport;
Varispeed* varispeed;

static void on_activate(GtkApplication* alphabet);

void on_open(GApplication *alphabet, GFile **files, gint n_files, UNUSED const gchar *hint)
{
    for (gint i = 0; i < n_files; i++) {
        /* must - cannot unref file?? */
        tracklist_add_file(tracklist, files[i]);
    }
    on_activate(GTK_APPLICATION(alphabet));
}

#ifdef MAC_INTEGRATION
gboolean on_open_osx(UNUSED GtkosxApplication* app, char* path, UNUSED gpointer user_data)
{
    GFile* file = g_file_new_for_path(path);
    tracklist_add_file(tracklist, file);
    g_object_unref(file);
    return TRUE;
}
#endif

gboolean on_destroy(UNUSED GtkWidget* window, UNUSED GtkApplication* alphabet)
{
    printf("destroy_handler\n");
    tracklist_free(tracklist);
    transport_free(transport);
    /* player_free(player); */
    /* gtk_window_close(GTK_WINDOW(window)); */
    /* g_application_quit(G_APPLICATION(alphabet)); */
    return TRUE;
}

void on_click_add(GtkWindow* window)
{
    GtkFileChooserNative *chsr;
    GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_OPEN;

    chsr = gtk_file_chooser_native_new("Add file", window, action, "_Add", "_Cancel");
    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(chsr), TRUE);

    if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(chsr)) == GTK_RESPONSE_ACCEPT) {

        GSList* filelist = gtk_file_chooser_get_files(GTK_FILE_CHOOSER(chsr));
        do tracklist_add_file(tracklist, filelist->data);
        while ((filelist = filelist->next));

        g_slist_free(filelist);

    }
    g_object_unref(chsr);
}

gboolean keypress_handler(GtkWidget *window, GdkEventKey *event)
{
    switch (event->keyval) {

        case GDK_KEY_Delete:
            tracklist_remove_selected(tracklist);
            return TRUE;

        case GDK_KEY_q:
            gtk_window_close(GTK_WINDOW(window));
            return TRUE;

        case GDK_KEY_n:
            gtk_button_clicked(GTK_BUTTON(transport->rtn));
            return TRUE;

        case GDK_KEY_l:
            gtk_button_clicked(GTK_BUTTON(transport->loop));
            return TRUE;

        case GDK_KEY_space:
            gtk_button_clicked(GTK_BUTTON(transport->play));
            return TRUE;

        case GDK_KEY_Left:
            gtk_button_clicked(GTK_BUTTON(transport->backward));
            return TRUE;

        case GDK_KEY_Right:
            gtk_button_clicked(GTK_BUTTON(transport->forward));
            return TRUE;

        case GDK_KEY_m:
        case GDK_KEY_KP_Enter:
            player_mark(player);
            return TRUE;

        case GDK_KEY_Return:
            gtk_button_clicked(GTK_BUTTON(transport->stop));
            return TRUE;

        default:
            return FALSE;
    }
    return FALSE;
}

/**
 * update the UI elements
 * no data is passed, just a trigger to update
 */
static gboolean update_ui(UNUSED gpointer data)
{
    timeline_update(timeline);
    counter_update(counter);
    transport_update(transport);
    return FALSE;
}

/**
 * event callback
 *
 * connect the player callback method to the player event
 *
 * called functions must be short and non-blocking
 */
static void event_callback(gpointer data)
{
    g_idle_add(G_SOURCE_FUNC(player_event_handler), data);
    g_idle_add(G_SOURCE_FUNC(update_ui), data);
}

void on_activate(GtkApplication* alphabet)
{
    GtkWidget* window, * box, *scrolled;
    GtkWidget* bar, * button;

#ifdef MAC_INTEGRATION
    GtkosxApplication* osx = g_object_new(GTKOSX_TYPE_APPLICATION, NULL);
#endif

    window = gtk_application_window_new(alphabet);
    gtk_window_set_title(GTK_WINDOW(window), "Alphabet");
    gtk_container_set_border_width(GTK_CONTAINER(window), 0);
    gtk_window_set_default_size(GTK_WINDOW(window), 640, 320);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    gtk_widget_show_all(window);

    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), box);

    scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_box_pack_start(GTK_BOX(box), scrolled, TRUE, TRUE, GDK_ACTION_DEFAULT);

    bar = gtk_action_bar_new();
    gtk_box_pack_end(GTK_BOX(box), bar, FALSE, FALSE, 0);

    gtk_widget_show_all(box);

    tracklist_init(tracklist);
    gtk_container_add(GTK_CONTAINER(scrolled), tracklist->tree);

    /* button = gtk_button_new_with_mnemonic("_Add"); */
    button = gtk_button_new_from_icon_name("list-add-symbolic", ICON_SIZE);
    gtk_action_bar_pack_start(GTK_ACTION_BAR(bar), button);
    g_signal_connect_swapped(button, "clicked", G_CALLBACK(on_click_add), window);
    gtk_widget_show_all(button);

    counter = counter_new(player);
    /* gtk_box_pack_start(GTK_BOX(foo), counter->box, FALSE, FALSE, spacing*2); */
    gtk_action_bar_pack_start(GTK_ACTION_BAR(bar), counter->box);
    /* gtk_widget_set_margin_start(counter->box, MARGIN/2); */

    timeline = timeline_new(player);
    /* gtk_box_pack_start(GTK_BOX(foo), timeline->box, TRUE, TRUE, spacing*2); */
    gtk_action_bar_pack_start(GTK_ACTION_BAR(bar), timeline->box);

    varispeed = varispeed_new(player);
    /* gtk_box_pack_start(GTK_BOX(foo), varispeed->box, FALSE, FALSE, spacing*2); */
    gtk_action_bar_pack_end(GTK_ACTION_BAR(bar), varispeed->box);
    /* gtk_widget_set_margin_start(varispeed->box, MARGIN/2); */
    /* gtk_widget_set_margin_end(varispeed->box, MARGIN/2); */

    transport = transport_new(player);
    /* gtk_box_pack_end(GTK_BOX(foo),  transport->box, FALSE, FALSE, spacing*2); */
    gtk_action_bar_pack_end(GTK_ACTION_BAR(bar), transport->box_control);
    gtk_action_bar_pack_end(GTK_ACTION_BAR(bar), transport->box_movement);

    gtk_widget_add_events(window, GDK_KEY_PRESS_MASK);
    g_signal_connect(window, "key_press_event", G_CALLBACK(keypress_handler), NULL);
    g_signal_connect(window, "destroy", G_CALLBACK(on_destroy), alphabet);

    /* event_callback will be called whenever player received event */
    player_set_event_callback(player, event_callback);

#ifdef MAC_INTEGRATION
    g_signal_connect(alphabet, "NSApplicationOpenFile", G_CALLBACK(on_open_osx), NULL);
    gtkosx_application_set_use_quartz_accelerators(osx, TRUE);
    gtkosx_application_ready(osx);
#endif
}

int window_run(int argc, char** argv)
{
    int status;
    GtkApplication* alphabet;

    player = player_init();
    if (!player) exit(EXIT_FAILURE);

    alphabet = gtk_application_new("org.gtk.alphabet", G_APPLICATION_HANDLES_OPEN);

    /* create here because "open" dependes on tracklist */
    /* widgets are created later on in tracklist_init */
    tracklist = tracklist_new(player);

    g_signal_connect(alphabet, "activate", G_CALLBACK(on_activate), tracklist->list);
    g_signal_connect(alphabet, "open", G_CALLBACK(on_open), tracklist->list);

    status = g_application_run(G_APPLICATION(alphabet), argc, argv);

    g_application_quit(G_APPLICATION(alphabet));

    player_free(player);
    return status;
}

int main(int argc, char *argv[])
{
    return window_run(argc - optind, argv);
}
