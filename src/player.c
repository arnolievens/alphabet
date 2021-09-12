/**
 * @author      : Arno Lievens (arnolievens@gmail.com)
 * @created     : 08/09/2021
 * @filename    : player.c
 */

#include <assert.h>
#include <errno.h>
#include <gtk/gtk.h>
#include <mpv/client.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../include/track.h"

#include "../include/player.h"

#define UNUSED __attribute__((unused))


static void mpv_print_status(int status)
{
    fprintf(stderr, "mpv error: %s\n", mpv_error_string(status));
}

void player_toggle(Player* this)
{
    int status;
    const char* cmd[] = {"cycle", "pause", NULL};
    if ((status = mpv_command(this->mpv, cmd)) < 0) {
        mpv_print_status(status);
    }
}

void player_seek(Player* this, double secs)
{
    int status;
    char secstr[10];
    sprintf(secstr, "%f", secs);
    const char* cmd[] = {"seek", secstr, NULL};
    if ((status = mpv_command(this->mpv, cmd)) < 0) {
        mpv_print_status(status);
    }
}

void player_loop(Player* this)
{
    int status;
    const char* cmd[] = {"ab-loop", NULL};

    /* cancel loop */
    if (this->loop_start && this->loop_stop) {
        this->loop_start = 0.0;
        this->loop_stop = 0.0;

    /* mark loop stop (B) */
    } else if (this->loop_start) {
        this->loop_stop = player_update(this);

    /* mark loop start (A) */
    } else {
        this->loop_stop = 0.0;
        this->loop_start = player_update(this);
    }

    if ((status = mpv_command(this->mpv, cmd)) < 0) {
        mpv_print_status(status);
    }
}

void player_mark(Player* this)
{
    this->marker = player_update(this);
}

void player_goto(Player* this, double position)
{
    int status;
    if (position > this->current->length) position = this->current->length;
    if (position < 0) position = 0;

    char posstr[9];
    g_snprintf(posstr, sizeof(posstr)/sizeof(posstr[0]), "%f", position);

    const char* cmd[] = {"seek", posstr, "absolute+keyframes", NULL};
    if ((status = mpv_command_async(this->mpv, 0, cmd)) < 0) {
        mpv_print_status(status);
    }
}

/** deprecated */
double player_update(Player* this)
{
    mpv_get_property(this->mpv, "time-pos", MPV_FORMAT_DOUBLE, &this->position);
    return this->position;
}

void player_load_track(Player* this, Track* track, double position)
{
    int status;
    if (position < 0) position = 0;
    /* TODO clamp max when length is known*/

    /* compensation for time-gap? */
    if (position) position += 0.050;

    char posstr[15];
    g_snprintf(posstr, sizeof(posstr)/sizeof(posstr[0]), "start=%f", position);

    /* replace digit separator, always use dot regardless off locale */
    char *p = posstr;
    while (*p) { *p = *p == ',' ? '.' : *p; p++; }

    this->current = track;

    const char *cmd[] = {"loadfile", track->uri, "replace", posstr, NULL};
    if ((status = mpv_command_async(this->mpv, 0, cmd)) < 0) {
        mpv_print_status(status);
    }
}

int player_event_handler(Player* this)
{
	while (this->mpv) {

		mpv_event *event = mpv_wait_event(this->mpv, 0);

        switch (event->event_id) {
            case MPV_EVENT_PROPERTY_CHANGE: {
                mpv_event_property *prop = event->data;
                if (!prop->data) break;

                if (g_strcmp0(prop->name, "time-pos") == 0) {
                    this->position = *(double*)(prop->data);

                } else if (g_strcmp0(prop->name, "core-idle") == 0) {
                    int core_idle = *(int*)(prop->data);
                    this->play_state = core_idle ? PLAY_STATE_PAUSE : PLAY_STATE_PLAY;

                } else if (g_strcmp0(prop->name, "length") == 0) {
                    this->current->length = *(double*)(prop->data);
                }
                break;
            }
            case MPV_EVENT_LOG_MESSAGE: {
                printf("mpv log: %s", (char*)event->data);
                break;
            }
            case MPV_EVENT_SHUTDOWN:
            case MPV_EVENT_NONE: {
                return FALSE;
            }
            default: {
                break;
            };
        }
    }
    return FALSE;
}

void player_set_event_callback(Player* this, void(*event_callback)(void*))
{
	mpv_set_wakeup_callback(this->mpv, event_callback, this);
}

Player* player_init()
{
    int status;
    Player* this = malloc(sizeof(Player));

    this->current = NULL;
    this->loop_start = 0;
    this->loop_stop = 0;
    this->marker = 0;
    this->play_state = PLAY_STATE_STOP;
    this->position = 0;
    this->rtn = 0;

    this->mpv = mpv_create();
    if (!this->mpv) {
        fprintf(stderr, "failed creating context\n");
        return NULL;
    }
	mpv_observe_property(this->mpv, 0, "core-idle", MPV_FORMAT_FLAG);
    mpv_observe_property(this->mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);
	mpv_observe_property(this->mpv, 0, "length", MPV_FORMAT_DOUBLE);

    if ((status = mpv_initialize(this->mpv)) < 0) {
        mpv_print_status(status);
    }
    return this;
}

void player_free(Player* this)
{
    if (!this) return;
    mpv_terminate_destroy(this->mpv);
    free(this);
}
