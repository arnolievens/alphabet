/**
 * @author      Arno Lievens (arnolievens@gmail.com)
 * @date        08/09/2021
 * @file        transport.c
 * @brief       transport buttons widget
 * @copyright   Copyright (c) 2021 Arno Lievens
 */

#include <assert.h>
#include <errno.h>
#include <gtk/gtk.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../include/config.h"
#include "../include/player.h"

#include "../include/transport.h"

/*
 * button eventlisteners
 */

static void on_clicked_forward(UNUSED GtkWidget *button, Transport* this);

static void on_clicked_backward(UNUSED GtkWidget *button, Transport* this);

static void on_clicked_play(UNUSED GtkWidget *button, Transport* this);

static void on_clicked_stop(UNUSED GtkWidget *button, Transport* this);

static void on_clicked_rtn(UNUSED GtkWidget *button, UNUSED Transport* this);

static void on_clicked_loop(UNUSED GtkWidget *button, UNUSED Transport* this);


Transport* transport_new(Player* player)
{
    GtkWidget* button;

    Transport* this = malloc(sizeof(Transport));
    this->player = player;

    this->box_movement = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, MARGIN/4);

    this->box_control = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, MARGIN/4);

    button = gtk_button_new_from_icon_name("media-seek-backward-symbolic", ICON_SIZE);
    gtk_box_pack_start(GTK_BOX(this->box_movement), button, FALSE, FALSE, 0);
    g_signal_connect(button, "clicked", G_CALLBACK(on_clicked_backward), this);
    this->backward = button;

    button = gtk_button_new_from_icon_name("media-playback-stop-symbolic", ICON_SIZE);
    gtk_box_pack_start(GTK_BOX(this->box_movement), button, FALSE, FALSE, 0);
    g_signal_connect(button, "clicked", G_CALLBACK(on_clicked_stop), this);
    this->stop = button;

    button = gtk_button_new_from_icon_name("media-playback-start-symbolic", ICON_SIZE);
    gtk_box_pack_start(GTK_BOX(this->box_movement), button, FALSE, FALSE, 0);
    g_signal_connect(button, "clicked", G_CALLBACK(on_clicked_play), this);
    this->play = button;

    button = gtk_button_new_from_icon_name("media-playback-pause-symbolic", ICON_SIZE);
    gtk_box_pack_start(GTK_BOX(this->box_movement), button, FALSE, FALSE, 0);
    g_signal_connect(button, "clicked", G_CALLBACK(on_clicked_play), this);
    this->pause = button;

    button = gtk_button_new_from_icon_name("media-seek-forward-symbolic", ICON_SIZE);
    gtk_box_pack_start(GTK_BOX(this->box_movement), button, FALSE, FALSE, 0);
    g_signal_connect(button, "clicked", G_CALLBACK(on_clicked_forward), this);
    this->forward = button;

    button = gtk_button_new_from_icon_name("mail-reply-symbolic", ICON_SIZE);
    gtk_box_pack_start(GTK_BOX(this->box_control), button, FALSE, FALSE, 0);
    g_signal_connect(button, "clicked", G_CALLBACK(on_clicked_rtn), this);
    this->rtn = button;

    button = gtk_button_new_from_icon_name("media-playlist-consecutive-symbolic", ICON_SIZE);
    gtk_box_pack_start(GTK_BOX(this->box_control), button, FALSE, FALSE, 0);
    g_signal_connect(button, "clicked", G_CALLBACK(on_clicked_rtn), this);
    this->ctd = button;

    button = gtk_button_new_from_icon_name("mark-location", ICON_SIZE);
    gtk_box_pack_start(GTK_BOX(this->box_control), button, FALSE, FALSE, 0);
    g_signal_connect(button, "clicked", G_CALLBACK(on_clicked_rtn), this);
    this->mark = button;

    button = gtk_button_new_from_icon_name("media-playlist-repeat-symbolic", ICON_SIZE);
    gtk_box_pack_start(GTK_BOX(this->box_control), button, FALSE, FALSE, 0);
    g_signal_connect(button, "clicked", G_CALLBACK(on_clicked_loop), this);
    this->loop = button;

    button = gtk_button_new_from_icon_name("media-playlist-no-repeat-symbolic", ICON_SIZE);
    gtk_box_pack_start(GTK_BOX(this->box_control), button, FALSE, FALSE, 0);
    g_signal_connect(button, "clicked", G_CALLBACK(on_clicked_loop), this);
    this->noloop = button;

    gtk_widget_show_all(this->box_movement);
    gtk_widget_show_all(this->box_control);

    transport_update(this);

    return this;
}

void transport_update(Transport* this)
{
    if (this->player->rtn && this->player->marker == 0.0) {
        gtk_widget_show(this->rtn);
        gtk_widget_hide(this->ctd);
        gtk_widget_hide(this->mark);
    } else if (this->player->marker != 0.0) {
        gtk_widget_hide(this->rtn);
        gtk_widget_hide(this->ctd);
        gtk_widget_show(this->mark);
    } else {
        gtk_widget_hide(this->rtn);
        gtk_widget_show(this->ctd);
        gtk_widget_hide(this->mark);
    }

    if (this->player->loop_start != 0.0 && this->player->loop_stop != 0.0) {
        gtk_widget_show(this->loop);
        gtk_widget_hide(this->noloop);
    } else {
        gtk_widget_hide(this->loop);
        gtk_widget_show(this->noloop);
    }

    if (this->player->play_state == PLAY_STATE_PLAY) {
        gtk_widget_show(this->pause);
        gtk_widget_hide(this->play);
    } else {
        gtk_widget_hide(this->pause);
        gtk_widget_show(this->play);
    }
}



void transport_free(Transport* this)
{
    if (!this) return;
    gtk_widget_destroy(this->box_control);
    gtk_widget_destroy(this->box_movement);
    g_free(this);
}


/*******************************************************************************
 * static functions
 *
 */

void on_clicked_forward(UNUSED GtkWidget *button, Transport* this)
{
    player_seek(this->player, 1);
}

void on_clicked_backward(UNUSED GtkWidget *button, Transport* this)
{
    player_seek(this->player, -1);
}

void on_clicked_play(UNUSED GtkWidget *button, Transport* this)
{
    player_toggle(this->player);
}

void on_clicked_stop(UNUSED GtkWidget *button, Transport* this)
{
    player_stop(this->player);
}

void on_clicked_rtn(UNUSED GtkWidget *button, UNUSED Transport* this)
{
    if (this->player->marker == 0.0) this->player->rtn = !this->player->rtn;
    this->player->marker = 0;
    transport_update(this);
}

void on_clicked_loop(UNUSED GtkWidget *button, UNUSED Transport* this)
{
    player_loop(this->player);
}
